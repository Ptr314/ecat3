// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: AZBK controller of the БК (MAXIOL): the microSD card of the AZ controller

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "azbk_sdcard.h"
#include "emulator/host_dir.h"
#include "emulator/state.h"
#include "emulator/utils.h"
#include "dsk_tools/core.h"

//------------------------------------------------------------------ имена ---

// Знаки короткого имени FAT, кроме букв и цифр
static const char SFN_EXTRA[] = "$%'-_@~`!(){}^#&";

static bool sfn_char(char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return true;
    return c != 0 && std::strchr(SFN_EXTRA, c) != nullptr;
}

// Компонент пути (уже большими буквами) - имя 8.3: до 8 знаков имени, точка
// и до 3 знаков типа, если тип есть. "." и ".." сюда не проходят
static bool valid_sfn(const std::string &s)
{
    const size_t dot = s.find('.');
    const std::string base = s.substr(0, dot);
    const std::string ext = (dot == std::string::npos) ? std::string() : s.substr(dot + 1);
    if (base.empty() || base.size() > 8 || ext.size() > 3) return false;
    if (dot != std::string::npos && ext.empty()) return false;
    for (size_t i = 0; i < base.size(); i++) if (!sfn_char(base[i])) return false;
    for (size_t i = 0; i < ext.size(); i++) if (!sfn_char(ext[i])) return false;   // вторая точка - тоже
    return true;
}

static char upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

// Имя файла компьютера так, как его видит карта, или "" - карта его не видит
static std::string card_name(const std::string &host)
{
    std::string u;
    for (size_t i = 0; i < host.size(); i++) {
        const unsigned char c = (unsigned char)host[i];
        if (c < 040 || c >= 0177) return std::string();
        u += upper((char)c);
    }
    return valid_sfn(u) ? u : std::string();
}

// Имена устройств Windows: файл с таким именем открыл бы устройство (COM1 -
// последовательный порт), а не файл
static bool reserved_name(const std::string &name)
{
    const std::string base = name.substr(0, name.find('.'));
    static const char * const RESERVED[] = { "CON", "PRN", "AUX", "NUL", "CLOCK$", nullptr };
    for (unsigned int i = 0; RESERVED[i] != nullptr; i++) if (base == RESERVED[i]) return true;
    return base.size() == 4 && (base.compare(0, 3, "COM") == 0 || base.compare(0, 3, "LPT") == 0)
        && base[3] >= '1' && base[3] <= '9';
}

static void split_path(const std::string &path, std::string &dir, std::string &name)
{
    const size_t p = path.rfind('/');
    if (p == std::string::npos) {
        dir.clear();
        name = path;
    } else {
        dir = path.substr(0, p);
        name = path.substr(p + 1);
    }
}

// Дата и время FAT по местному времени компьютера, как у файлов карты
static void fat_time(int64_t t, uint16_t &date, uint16_t &time)
{
    const time_t tt = (time_t)t;
    const std::tm * tm = std::localtime(&tt);
    if (tm == nullptr || tm->tm_year < 80 || tm->tm_year >= 80 + 128) {
        date = time = 0;
        return;
    }
    date = (uint16_t)(((tm->tm_year - 80) << 9) | ((tm->tm_mon + 1) << 5) | tm->tm_mday);
    time = (uint16_t)((tm->tm_hour << 11) | (tm->tm_min << 5) | (tm->tm_sec / 2));
}

static bool by_name(const HostDirEntry &a, const HostDirEntry &b)
{
    return a.name < b.name;
}

int AzSdCard::parse(const std::string &guest, std::string &path)
{
    path.clear();
    size_t start = 0;
    while (start < guest.size() && guest[start] == ' ') start++;
    size_t end = guest.size();
    while (end > start && guest[end - 1] == ' ') end--;
    if (end - start >= 2 && guest[start + 1] == ':') {
        const char d = upper(guest[start]);
        if (d == 'N' || d == 'R' || d == 'P' || d == 'S') return FR_NOT_READY;
        if (d != '0') return FR_INVALID_DRIVE;
        start += 2;
    }
    std::string comp, out;
    for (size_t i = start; i <= end; i++) {
        const char c = (i < end) ? guest[i] : '/';
        if (c == '/' || c == '\\') {
            if (comp.empty()) continue;
            if (!valid_sfn(comp)) return FR_INVALID_NAME;
            if (!out.empty()) out += '/';
            out += comp;
            comp.clear();
            continue;
        }
        const unsigned char u = (unsigned char)c;
        if (u < 040 || u >= 0177) return FR_INVALID_NAME;
        comp += upper(c);
    }
    path = out;
    return FR_OK;
}

//------------------------------------------------------- диск компьютера ---

void AzSdCard::configure(const std::string &root, bool write, unsigned int size_mb)
{
    m_root = root;
    if (!m_root.empty()) {
        const char last = m_root[m_root.size() - 1];
        if (last != '/' && last != '\\') m_root += '/';
    }
    m_write = write;
    m_size_mb = size_mb;
    m_overlay.clear();
    m_overlay_bytes = 0;
    m_dirs.clear();
    m_cache_path.clear();
    m_cache_host.clear();
}

// Первая по имени запись каталога компьютера, которую карта видит под этим
// именем: ссылки пропускаются
bool AzSdCard::host_entry(const std::string &host_dir, const std::string &name, std::string &host_name,
                          bool &is_dir, uint64_t &size, int64_t &modified) const
{
    if (name.empty()) return false;
    std::vector<HostDirEntry> items = host_dir_list(host_dir);
    std::sort(items.begin(), items.end(), by_name);
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].link || card_name(items[i].name) != name) continue;
        host_name = items[i].name;
        is_dir = items[i].dir;
        size = items[i].size;
        modified = (int64_t)items[i].modified;
        return true;
    }
    return false;
}

bool AzSdCard::host_dir(const std::string &dir, std::string &host) const
{
    if (m_root.empty()) return false;
    host = m_root;
    size_t pos = 0;
    while (pos < dir.size()) {
        size_t next = dir.find('/', pos);
        if (next == std::string::npos) next = dir.size();
        std::string hn;
        bool is_dir = false;
        uint64_t size = 0;
        int64_t modified = 0;
        if (!host_entry(host, dir.substr(pos, next - pos), hn, is_dir, size, modified) || !is_dir) return false;
        host += hn + "/";
        pos = next + 1;
    }
    return true;
}

bool AzSdCard::host_file(const std::string &path, std::string &host) const
{
    if (!m_cache_path.empty() && path == m_cache_path) {
        host = m_cache_host;
        return true;
    }
    std::string dir, name, hd, hn;
    split_path(path, dir, name);
    bool is_dir = false;
    uint64_t size = 0;
    int64_t modified = 0;
    if (name.empty() || !host_dir(dir, hd) || !host_entry(hd, name, hn, is_dir, size, modified) || is_dir) return false;
    host = hd + hn;
    m_cache_path = path;
    m_cache_host = host;
    return true;
}

//------------------------------------------------------------- команды ---

int AzSdCard::list(const std::string &dir, std::vector<Entry> &out) const
{
    out.clear();
    std::string hd;
    const bool on_host = host_dir(dir, hd);
    if (!on_host && !overlay_dir(dir)) return FR_NO_PATH;
    std::map<std::string, Entry> m;
    std::vector<HostDirEntry> items;
    if (on_host) items = host_dir_list(hd);
    std::sort(items.begin(), items.end(), by_name);
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].link) continue;
        const std::string name = card_name(items[i].name);
        if (name.empty() || m.count(name)) continue;
        Entry e;
        e.name = name;
        e.dir = items[i].dir;
        e.size = e.dir ? 0 : (uint32_t)std::min<uint64_t>(items[i].size, 0xFFFFFFFFu);
        fat_time((int64_t)items[i].modified, e.date, e.time);
        m[name] = e;
    }
    for (std::set<std::string>::const_iterator it = m_dirs.begin(); it != m_dirs.end(); ++it) {
        std::string d, n;
        split_path(*it, d, n);
        if (d != dir || m.count(n)) continue;
        Entry e;
        e.name = n;
        e.dir = true;
        m[n] = e;
    }
    for (std::map<std::string, File>::const_iterator it = m_overlay.begin(); it != m_overlay.end(); ++it) {
        std::string d, n;
        split_path(it->first, d, n);
        if (d != dir) continue;
        Entry e;
        e.name = n;
        e.size = (uint32_t)it->second.data.size();
        e.date = it->second.date;
        e.time = it->second.time;
        m[n] = e;
    }
    for (std::map<std::string, Entry>::const_iterator it = m.begin(); it != m.end(); ++it) out.push_back(it->second);
    return FR_OK;
}

int AzSdCard::file_size(const std::string &path, uint32_t &size) const
{
    size = 0;
    if (path.empty()) return FR_NO_FILE;
    std::map<std::string, File>::const_iterator it = m_overlay.find(path);
    if (it != m_overlay.end()) {
        size = (uint32_t)it->second.data.size();
        return FR_OK;
    }
    std::string dir, name, hd, hn;
    split_path(path, dir, name);
    if (!host_dir(dir, hd)) return overlay_dir(dir) ? FR_NO_FILE : FR_NO_PATH;
    bool is_dir = false;
    uint64_t bytes = 0;
    int64_t modified = 0;
    if (!host_entry(hd, name, hn, is_dir, bytes, modified) || is_dir) return FR_NO_FILE;
    // 051 отдает размер в 31 разряде
    if (bytes > 0x7FFFFFFFu) return FR_DENIED;
    size = (uint32_t)bytes;
    return FR_OK;
}

bool AzSdCard::read(const std::string &path, uint32_t offset, uint8_t * buffer, uint32_t count) const
{
    std::map<std::string, File>::const_iterator it = m_overlay.find(path);
    if (it != m_overlay.end()) {
        const std::vector<uint8_t> &d = it->second.data;
        if ((uint64_t)offset + count > d.size()) return false;
        if (count > 0) memcpy(buffer, d.data() + offset, count);
        return true;
    }
    std::string host;
    if (!host_file(path, host)) return false;
    std::FILE * f = host_fopen(host, "rb");
    if (f == nullptr) return false;
    const bool ok = std::fseek(f, (long)offset, SEEK_SET) == 0 && std::fread(buffer, 1, count, f) == count;
    std::fclose(f);
    return ok;
}

// Файл компьютера, в который пойдет запись пути карты. Новый - только если
// под этим именем на диске нет ничего, чего карта не видит: ссылки, длинного
// имени под псевдонимом 8.3 Windows (LONG-F~1.TXT), устройства (CON, COM1)
int AzSdCard::host_target(const std::string &path, std::string &host) const
{
    if (path.empty()) return FR_INVALID_NAME;
    std::string dir, name, hd, hn;
    split_path(path, dir, name);
    if (!host_dir(dir, hd)) return FR_NO_PATH;
    bool is_dir = false;
    uint64_t size = 0;
    int64_t modified = 0;
    const bool exists = host_entry(hd, name, hn, is_dir, size, modified);
    if (exists && is_dir) return FR_DENIED;
    if (!exists) {
        HostDirEntry st;
        if (host_stat(hd + name, st) || reserved_name(name)) return FR_DENIED;
    }
    host = hd + (exists ? hn : name);
    return FR_OK;
}

int AzSdCard::create(const std::string &path, uint16_t date, uint16_t time)
{
    if (overlay_dir(path)) return FR_DENIED;
    if (m_write) {
        std::string dir, name, host;
        split_path(path, dir, name);
        materialize(dir);
        const int result = host_target(path, host);
        if (result != FR_OK) return result;
        std::FILE * f = host_fopen(host, "wb");
        if (f == nullptr) return FR_DENIED;
        std::fclose(f);
        // В каталоге он теперь новый: прежний файл слоя под этим путем забыт
        std::map<std::string, File>::iterator old = m_overlay.find(path);
        if (old != m_overlay.end()) {
            m_overlay_bytes -= old->second.data.size();
            m_overlay.erase(old);
        }
        m_cache_path = path;
        m_cache_host = host;
        return FR_OK;
    }
    if (path.empty()) return FR_INVALID_NAME;
    std::string dir, name, hd, hn;
    split_path(path, dir, name);
    if (host_dir(dir, hd)) {
        bool is_dir = false;
        uint64_t size = 0;
        int64_t modified = 0;
        if (host_entry(hd, name, hn, is_dir, size, modified) && is_dir) return FR_DENIED;
    } else if (!overlay_dir(dir))
        return FR_NO_PATH;
    std::map<std::string, File>::iterator it = m_overlay.find(path);
    if (it != m_overlay.end()) m_overlay_bytes -= it->second.data.size();
    File &f = m_overlay[path];
    f.data.clear();
    f.date = date;
    f.time = time;
    return FR_OK;
}

bool AzSdCard::append(const std::string &path, const uint8_t * data, uint32_t count)
{
    if (m_write) {
        std::string host;
        if (!host_file(path, host)) return false;
        std::FILE * f = host_fopen(host, "ab");
        if (f == nullptr) return false;
        const bool ok = count == 0 || std::fwrite(data, 1, count, f) == count;
        return (std::fclose(f) == 0) && ok;
    }
    std::map<std::string, File>::iterator it = m_overlay.find(path);
    if (it == m_overlay.end()) return false;
    std::vector<uint8_t> &d = it->second.data;
    if ((uint64_t)d.size() + count > MAX_FILE || m_overlay_bytes + count > MAX_OVERLAY) return false;
    d.insert(d.end(), data, data + count);
    m_overlay_bytes += count;
    return true;
}

unsigned int AzSdCard::flush()
{
    const std::set<std::string> dirs = m_dirs;
    for (std::set<std::string>::const_iterator it = dirs.begin(); it != dirs.end(); ++it) materialize(*it);
    std::vector<std::string> done;
    for (std::map<std::string, File>::const_iterator it = m_overlay.begin(); it != m_overlay.end(); ++it) {
        std::string host;
        if (host_target(it->first, host) != FR_OK) continue;
        std::FILE * f = host_fopen(host, "wb");
        if (f == nullptr) continue;
        const std::vector<uint8_t> &d = it->second.data;
        bool ok = d.empty() || std::fwrite(d.data(), 1, d.size(), f) == d.size();
        ok = (std::fclose(f) == 0) && ok;
        if (ok) done.push_back(it->first);
    }
    for (size_t i = 0; i < done.size(); i++) {
        m_overlay_bytes -= m_overlay[done[i]].data.size();
        m_overlay.erase(done[i]);
    }
    m_cache_path.clear();
    m_cache_host.clear();
    return (unsigned int)m_overlay.size();
}

int AzSdCard::make_dir(const std::string &path)
{
    if (m_root.empty()) return FR_NOT_READY;
    if (path.empty() || overlay_dir(path) || m_overlay.count(path)) return FR_EXIST;
    std::string dir, name, hd, hn;
    split_path(path, dir, name);
    if (host_dir(dir, hd)) {
        bool is_dir = false;
        uint64_t size = 0;
        int64_t modified = 0;
        if (host_entry(hd, name, hn, is_dir, size, modified)) return FR_EXIST;
    } else if (!overlay_dir(dir))
        return FR_NO_PATH;
    if (m_write) {
        materialize(dir);
        if (!host_dir(dir, hd)) return FR_NO_PATH;
        // Под этим именем на диске нет ничего, чего карта не видит
        HostDirEntry st;
        if (host_stat(hd + name, st) || reserved_name(name)) return FR_DENIED;
        return host_mkdir(hd + name) ? FR_OK : FR_DENIED;
    }
    m_dirs.insert(path);
    return FR_OK;
}

// Каталог слоя (и каталоги слоя над ним) - на диск компьютера: туда идет запись
bool AzSdCard::materialize(const std::string &dir)
{
    if (dir.empty() || !overlay_dir(dir)) return true;
    std::string parent, name, hd;
    split_path(dir, parent, name);
    if (!materialize(parent) || !host_dir(parent, hd)) return false;
    HostDirEntry st;
    if (host_stat(hd + name, st) || reserved_name(name) || !host_mkdir(hd + name)) return false;
    m_dirs.erase(dir);
    return true;
}

bool AzSdCard::store(const std::string &path, const std::vector<uint8_t> &data, uint16_t date, uint16_t time)
{
    if (path.empty() || data.size() > MAX_FILE) return false;
    std::map<std::string, File>::iterator it = m_overlay.find(path);
    const uint64_t old = (it != m_overlay.end()) ? it->second.data.size() : 0;
    if (m_overlay_bytes - old + data.size() > MAX_OVERLAY) return false;
    File &f = m_overlay[path];
    f.data = data;
    f.date = date;
    f.time = time;
    m_overlay_bytes = m_overlay_bytes - old + data.size();
    return true;
}

bool AzSdCard::mount_source(const std::string &path, std::string &host, const std::vector<uint8_t> * &data) const
{
    host.clear();
    data = nullptr;
    std::map<std::string, File>::const_iterator it = m_overlay.find(path);
    if (it != m_overlay.end()) {
        data = &it->second.data;
        return true;
    }
    return host_file(path, host);
}

// Сколько занято в каталоге: обходит не глубже 16 уровней и не больше 100000
// записей - каталогом карты бывает и большая библиотека
uint64_t AzSdCard::host_used(const std::string &host_dir, unsigned int depth, unsigned int &budget) const
{
    uint64_t used = 0;
    const std::vector<HostDirEntry> items = host_dir_list(host_dir);
    for (size_t i = 0; i < items.size() && budget > 0; i++) {
        budget--;
        if (items[i].link) continue;
        if (items[i].dir) {
            if (depth < 16) used += host_used(host_dir + items[i].name + "/", depth + 1, budget);
        } else
            used += items[i].size;
    }
    return used;
}

void AzSdCard::sizes(unsigned int &total_mb, unsigned int &free_mb) const
{
    unsigned int budget = 100000;
    const uint64_t used = m_overlay_bytes + (m_root.empty() ? 0 : host_used(m_root, 0, budget));
    const uint64_t used_mb = (used + (1u << 20) - 1) >> 20;
    total_mb = std::min(m_size_mb, 65535u);
    free_mb = (used_mb < m_size_mb) ? (unsigned int)std::min<uint64_t>(m_size_mb - used_mb, 65535u) : 0;
}

//---------------------------------------------------------------- снимок ---

void AzSdCard::save_state(StateWriter &w) const
{
    w.n("sd_dirs", (uint32_t)m_dirs.size());
    unsigned int di = 0;
    for (std::set<std::string>::const_iterator it = m_dirs.begin(); it != m_dirs.end(); ++it, di++)
        w.s(("sd_dir_" + std::to_string(di)).c_str(), *it);
    w.n("sd_files", (uint32_t)m_overlay.size());
    unsigned int i = 0;
    for (std::map<std::string, File>::const_iterator it = m_overlay.begin(); it != m_overlay.end(); ++it, i++) {
        const std::string k = std::to_string(i);
        w.s(("sd_path_" + k).c_str(), it->first);
        w.u(("sd_date_" + k).c_str(), it->second.date, 16);
        w.u(("sd_time_" + k).c_str(), it->second.time, 16);
        w.blob(("sd_data_" + k).c_str(), "sdcard-" + k + ".bin", it->second.data.data(), it->second.data.size());
    }
}

void AzSdCard::load_state(const StateReader &r)
{
    m_overlay.clear();
    m_overlay_bytes = 0;
    m_dirs.clear();
    m_cache_path.clear();
    m_cache_host.clear();
    unsigned int dirs = 0;
    r.u("sd_dirs", dirs);
    for (unsigned int i = 0; i < dirs && i < 4096; i++) {
        std::string path, check;
        if (r.s(("sd_dir_" + std::to_string(i)).c_str(), path) && parse(path, check) == FR_OK && check == path && !path.empty())
            m_dirs.insert(path);
    }
    unsigned int count = 0;
    r.u("sd_files", count);
    for (unsigned int i = 0; i < count; i++) {
        const std::string k = std::to_string(i);
        std::string path, check;
        if (!r.s(("sd_path_" + k).c_str(), path) || parse(path, check) != FR_OK || check != path || path.empty()) continue;
        File f;
        unsigned int v = 0;
        if (r.u(("sd_date_" + k).c_str(), v)) f.date = (uint16_t)v;
        if (r.u(("sd_time_" + k).c_str(), v)) f.time = (uint16_t)v;
        r.blob(("sd_data_" + k).c_str(), f.data);
        if (f.data.size() > MAX_FILE || m_overlay_bytes + f.data.size() > MAX_OVERLAY) continue;
        m_overlay_bytes += f.data.size();
        m_overlay[path] = f;
    }
}

//---------------------------------------------------------------- гнездо ---

// Каталог по имени: абсолютный, рядом с расширением, с файлом машины
// (снимок состояния - рядом с собой), со сценарием или в рабочем каталоге;
// у команды load сценария - сначала рядом со сценарием. Полный путь; нет
// такого каталога - пусто
static std::string find_dir(SystemData * sd, const std::string &name, bool script_first)
{
    std::vector<std::string> c;
    if (is_absolute_path(name))
        c.push_back(name);
    else {
        if (script_first && !sd->script_path.empty()) c.push_back(sd->script_path + name);
        if (!sd->ext_path.empty()) c.push_back(sd->ext_path + name);
        const size_t slash = sd->system_file.find_last_of("/\\");
        if (slash != std::string::npos) c.push_back(sd->system_file.substr(0, slash + 1) + name);
        if (!script_first && !sd->script_path.empty()) c.push_back(sd->script_path + name);
        c.push_back(name);
    }
    for (size_t i = 0; i < c.size(); i++) {
        HostDirEntry e;
        if (host_stat(c[i], e) && e.dir && !e.link) return host_absolute(c[i]);
    }
    return std::string();
}

AZBKSdCard::AZBKSdCard(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)
{
    device_class = "sdcard";
}

emulator::Result AZBKSdCard::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;
    m_size_mb = read_confg_value(cd, "size", false, 4096u);
    // Как написано: read_confg_value() приводит строку к нижнему регистру
    const std::string folder = str_trim(cd->get_parameter("folder", false).value);
    // Каталога нет - гнездо пустое, как у накопителя без образа
    m_folder = folder.empty() ? std::string() : find_dir(sd, folder, false);
    m_card.configure(m_folder, read_confg_value(cd, "write", false, false), m_size_mb);
    return emulator::Result::ok();
}

// Другая карта: файлы прежней, записанные в память, пропадают
void AZBKSdCard::insert(const std::string &folder)
{
    m_folder = folder;
    m_card.configure(m_folder, m_card.writes_host(), m_size_mb);
    if (m_listener != nullptr) m_listener->card_changed(true);
}

ConfigFields AZBKSdCard::get_config_fields()
{
    ConfigField f;
    f.name = "folder";
    f.title = QT_TRANSLATE_NOOP("ConfigFields", "microSD card folder");
    f.type = CONFIG_FIELD_DIR;

    ConfigField w;
    w.name = "write";
    w.title = QT_TRANSLATE_NOOP("ConfigFields", "microSD card writes");
    w.type = CONFIG_FIELD_CHOICE;
    w.def = "0";
    w.values.push_back({"0", QT_TRANSLATE_NOOP("ConfigFields", "Into memory only, the folder stays as it was")});
    w.values.push_back({"1", QT_TRANSLATE_NOOP("ConfigFields", "Into the files of the folder")});
    return {f, w};
}

// Каталог - полным путем: его нет в архиве снимка. Снимок, открытый там, где
// каталога нет, берет каталог из своей конфигурации, если тот находится
void AZBKSdCard::save_state(StateWriter &w)
{
    ComputerDevice::save_state(w);
    w.s("folder", m_folder);
    w.b("write", m_card.writes_host());
    if (m_card.present()) m_card.save_state(w);
}

emulator::Result AZBKSdCard::load_state(const StateReader &r)
{
    emulator::Result res = ComputerDevice::load_state(r);
    if (!res) return res;
    bool write = m_card.writes_host();
    r.b("write", write);
    std::string folder;
    if (r.s("folder", folder)) {
        HostDirEntry e;
        if (folder.empty())
            m_folder.clear();
        else if (host_stat(folder, e) && e.dir && !e.link)
            m_folder = folder;
    }
    m_card.configure(m_folder, write, m_size_mb);
    if (m_card.present()) m_card.load_state(r);
    return emulator::Result::ok();
}

std::vector<DeviceFieldInfo> AZBKSdCard::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"attached",  "1 if a card (a folder) is in",                       false});
    r.push_back({"path",      "The folder of the card",                             false});
    r.push_back({"file",      "The name of the folder",                             false});
    r.push_back({"protected", "1 if the machine's writes stay in the card's memory", false});
    r.push_back({"files",     "Files the machine has written into the card's memory", false});
    return r;
}

bool AZBKSdCard::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 1;
    if (field == "attached")  { out.values.push_back(m_card.present() ? 1 : 0); return true; }
    if (field == "protected") { out.values.push_back(m_card.writes_host() ? 0 : 1); return true; }
    out.width = 32;
    if (field == "files")     { out.values.push_back(m_card.overlay_files()); return true; }
    out.numeric = false;
    out.width = 0;
    if (field == "path") { out.text = m_folder; return true; }
    if (field == "file") { out.text = m_folder.empty() ? std::string() : dsk_tools::get_filename(m_folder); return true; }
    return ComputerDevice::get_field(field, from, to, out);
}

std::vector<DeviceCommandInfo> AZBKSdCard::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = ComputerDevice::get_device_commands();
    r.push_back({"load",    "folder",  "Inserts a card: the folder; files written into the memory of the old one are lost"});
    r.push_back({"eject",   "",        "Takes the card out"});
    r.push_back({"protect", "[1|0]",   "1 - the machine's writes stay in the card's memory, 0 - they go into the folder; no argument toggles"});
    r.push_back({"flush",   "",        "Writes the files in the card's memory into the folder"});
    return r;
}

emulator::Result AZBKSdCard::send_command(const std::string &command, const std::string &parameters)
{
    std::vector<std::string> p = split_params(parameters);
    if (command == "load") {
        const std::string dir = p.empty() ? std::string() : find_dir(sd, p[0], true);
        if (dir.empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Incorrect parameters for")) + "} " + name
                + ": " + (p.empty() ? std::string() : p[0]));
        insert(dir);
        return emulator::Result::ok();
    }
    if (command == "eject") {
        insert(std::string());
        return emulator::Result::ok();
    }
    if (command == "protect") {
        const bool protect = (p.empty() || p[0].empty()) ? m_card.writes_host() : parse_numeric_value(p[0], 10) != 0;
        if (protect == !m_card.writes_host()) return emulator::Result::ok();
        m_card.set_write(!protect);
        if (m_listener != nullptr) m_listener->card_changed(false);
        return emulator::Result::ok();
    }
    if (command == "flush") {
        m_card.flush();
        return emulator::Result::ok();
    }
    return ComputerDevice::send_command(command, parameters);
}

ComputerDevice * create_azbk_sdcard(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new AZBKSdCard(im, cd);
}
