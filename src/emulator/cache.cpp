// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: The cache of unpacked archives and inline data, source

#include "cache.h"

#include <cstring>
#include <ctime>
#include <vector>

#include "dsk_tools/core.h"
#include "libs/dsk_tools/src/utils.h"
#include "host_dir.h"

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include "libs/dsk_tools/src/utils.h"
#else
    #include <dirent.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #include <utime.h>
#endif

namespace {

#ifdef _WIN32

void remove_file(const std::string &path) { DeleteFileW(dsk_tools::utf8_to_wide(path).c_str()); }
void remove_dir(const std::string &path) { RemoveDirectoryW(dsk_tools::utf8_to_wide(path).c_str()); }
//The link itself, never what it points to: a directory link goes the way of a
//directory
void remove_link(const std::string &path, bool dir) { if (dir) remove_dir(path); else remove_file(path); }

#else

void remove_file(const std::string &path) { unlink(path.c_str()); }
void remove_dir(const std::string &path) { rmdir(path.c_str()); }
void remove_link(const std::string &path, bool) { unlink(path.c_str()); }

#endif

void remove_entry(const std::string &dir, const HostDirEntry &e);

//A directory with everything in it. What cannot go (a file open elsewhere)
//stays, and so does the directory holding it
void remove_tree(const std::string &dir)
{
    const std::vector<HostDirEntry> items = host_dir_list(dir);
    for (size_t i = 0; i < items.size(); i++) remove_entry(dir, items[i]);
    remove_dir(dir.substr(0, dir.size() - 1));
}

void remove_entry(const std::string &dir, const HostDirEntry &e)
{
    if (e.link) remove_link(dir + e.name, e.dir);
    else if (e.dir) remove_tree(dir + e.name + "/");
    else remove_file(dir + e.name);
}

void clean(const std::string &dir, time_t older_than)
{
    const std::vector<HostDirEntry> items = host_dir_list(dir);
    for (size_t i = 0; i < items.size(); i++)
        if (items[i].modified < older_than) remove_entry(dir, items[i]);
}

} // namespace

void cache_touch(const std::string &path)
{
#ifdef _WIN32
    std::string p = path;
    //A directory opens without its trailing separator
    while (p.size() > 3 && (p[p.size() - 1] == '/' || p[p.size() - 1] == '\\')) p.erase(p.size() - 1);
    HANDLE h = CreateFileW(dsk_tools::utf8_to_wide(p).c_str(), FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    SetFileTime(h, nullptr, nullptr, &now);
    CloseHandle(h);
#else
    utime(path.c_str(), nullptr);
#endif
}

void cache_cleanup(const std::string &cache_path, unsigned int max_age_days)
{
    if (cache_path.empty()) return;
    const time_t older_than = time(nullptr) - static_cast<time_t>(max_age_days) * 24 * 60 * 60;
    clean(cache_path + "ext/", older_than);
    clean(cache_path + "inline/", older_than);
    clean(cache_path + "launch/", older_than);
}

void cache_make_dirs(const std::string &path)
{
    for (size_t i = 1; i <= path.size(); i++)
        if (i == path.size() || path[i] == '/' || path[i] == '\\')
        {
            const std::string dir = path.substr(0, i);
            //A drive letter is not a directory to create
            if (dir.size() == 2 && dir[1] == ':') continue;
            dsk_tools::utf8_mkdir(dir);
        }
}

bool cache_write(const std::string &path, const uint8_t * data, size_t size)
{
    if (dsk_tools::file_exists(path) && dsk_tools::utf8_file_size(path) == static_cast<long long>(size))
    {
        const std::string old = dsk_tools::utf8_read_file(path);
        if (old.size() == size && (size == 0 || memcmp(old.data(), data, size) == 0)) return true;
    }
    dsk_tools::UTF8_ofstream f(path, std::ios::binary);
    if (!f.good()) return false;
    if (size > 0) f.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
    f.close();
    return true;
}
