// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: WASM entry point and C API bridge

#include <emscripten.h>
#include <emscripten/threading.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>
#include <stdexcept>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "emulator/emulator.h"
#include "emulator/config_ext.h"
#include "emulator/files.h"
#include "emulator/utils.h"
#include "emulator/devices/common/fdd.h"
#include "emulator/devices/common/tape.h"
#include "emulator/devices/common/hdd_image.h"
#include "dsk_tools/core.h"
//Часть помощников dsk_tools объявлена в его внутреннем заголовке, и звать
//его надо по полному пути: короткий "utils.h" из dsk_tools.h у MSVC попадает
//в emulator/utils.h - он ищет кавычечный include и по цепочке включающих
#include "libs/dsk_tools/src/utils.h"

#include <iterator>

// The same markdown renderer and settings the desktop chooser uses, see
// md2html() in qt_utils.cpp
#define MD4C_USE_UTF8
#include "libs/md4c/md4c-html.h"
#include "renderer_wasm.h"

static Emulator* g_emulator = nullptr;
static WasmRenderer* g_renderer = nullptr;
//What went wrong in the last load, for wasm_last_error()
static std::string g_load_error;

static const std::string WORK_PATH = "/computers/";
static const std::string DATA_PATH = "/data/";
static const std::string SOFTWARE_PATH = "/software/";
static const std::string INI_FILE = "/ecat.ini";

// ============================================================================
// Hard disk images from the visitor's computer
// ============================================================================
//
// A hard disk image can be hundreds of megabytes, and MEMFS is the tab's
// memory, so a picked image is not copied into it. The page hands the File to
// a worker of its own (hostFileWorker in ecat_wasm.js) together with the wasm
// memory and a mailbox in it, and the worker reads what is asked for with
// FileReaderSync, straight into the asker's buffer. The asker is whatever
// thread reads the disk: the emulation thread, or the browser's main thread
// when a controller looks at the image while it is being attached. None of
// them needs the main thread's event loop, so none of them can deadlock on
// it; the main thread waits by spinning, as emscripten_futex_wait does there.
// Nothing is ever written to the file: HddImage keeps the writes in memory.
//
// The mailbox, 32-bit words:
//   [0] request  sequence number, raised by the asker
//   [1] done     sequence number of the request answered
//   [2] length   bytes to read; -1 tells the worker to stop
//   [3] buffer   address in the wasm memory
//   [4] [5]      offset, low and high word
//   [6] result   bytes read, -1 on an error

class HostFile
{
public:
    HostFile(uint64_t size) : m_size(size)
    {
        m_box = static_cast<int32_t*>(calloc(8, sizeof(int32_t)));
    }
    ~HostFile()
    {
        // The worker answers the stop and closes itself. The mailbox is freed
        // only once it has answered: a worker still looking at it would take
        // whatever the memory gets reused for as a request
        const int32_t seq = post(0, nullptr, -1);
        const double deadline = emscripten_get_now() + 1000.0;
        while (__atomic_load_n(&m_box[1], __ATOMIC_SEQ_CST) != seq) {
            if (emscripten_get_now() > deadline) return;
            emscripten_futex_wait(&m_box[1], (uint32_t)(seq - 1), 100.0);
        }
        free(m_box);
    }

    uint64_t size() const { return m_size; }
    int32_t * box() { return m_box; }

    bool read(uint64_t offset, uint8_t * buffer, size_t length)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (offset + length > m_size) return false;
        while (length > 0) {
            const uint64_t base = offset - offset % BLOCK;
            const size_t from = (size_t)(offset - base);
            size_t n = BLOCK - from;
            if (n > length) n = length;
            if (from == 0 && length >= BLOCK) {
                // Whole blocks go straight to the asker, past the cache
                n = length - length % BLOCK;
                if (!request(offset, buffer, n)) return false;
            } else {
                const std::vector<uint8_t> * block = cached(base);
                if (block == nullptr) return false;
                memcpy(buffer, block->data() + from, n);
            }
            offset += n;
            buffer += n;
            length -= n;
        }
        return true;
    }

private:
    // Sectors are read one at a time, and each request is a round trip to
    // another thread: a few blocks are kept instead
    static const size_t BLOCK = 64 * 1024;
    static const size_t BLOCKS = 16;

    int32_t * m_box;
    uint64_t m_size;
    std::mutex m_mutex;
    bool m_broken = false;
    std::vector<uint64_t> m_bases;
    std::vector<std::vector<uint8_t>> m_blocks;
    size_t m_next = 0;

    const std::vector<uint8_t> * cached(uint64_t base)
    {
        for (size_t i = 0; i < m_bases.size(); i++)
            if (m_bases[i] == base) return &m_blocks[i];
        size_t slot;
        if (m_bases.size() < BLOCKS) {
            slot = m_bases.size();
            m_bases.push_back(base);
            m_blocks.push_back(std::vector<uint8_t>());
        } else {
            slot = m_next;
            m_next = (m_next + 1) % BLOCKS;
        }
        const size_t n = (m_size - base < BLOCK) ? (size_t)(m_size - base) : BLOCK;
        m_blocks[slot].assign(n, 0);
        m_bases[slot] = base;
        if (!request(base, m_blocks[slot].data(), n)) {
            m_bases[slot] = UINT64_MAX;
            return nullptr;
        }
        return &m_blocks[slot];
    }

    int32_t post(uint64_t offset, uint8_t * buffer, int32_t length)
    {
        const int32_t seq = __atomic_load_n(&m_box[0], __ATOMIC_SEQ_CST) + 1;
        m_box[2] = length;
        m_box[3] = (int32_t)(uintptr_t)buffer;
        m_box[4] = (int32_t)(uint32_t)(offset & 0xFFFFFFFFu);
        m_box[5] = (int32_t)(uint32_t)(offset >> 32);
        m_box[6] = 0;
        __atomic_store_n(&m_box[0], seq, __ATOMIC_SEQ_CST);
        emscripten_futex_wake(&m_box[0], 1);
        return seq;
    }

    bool request(uint64_t offset, uint8_t * buffer, size_t length)
    {
        if (m_broken) return false;
        const int32_t seq = post(offset, buffer, (int32_t)length);
        // A worker that does not answer (the page closed it, the file went
        // away) must not hang the machine: the image just stops reading
        const double deadline = emscripten_get_now() + 10000.0;
        while (__atomic_load_n(&m_box[1], __ATOMIC_SEQ_CST) != seq) {
            if (emscripten_get_now() > deadline) {
                m_broken = true;
                return false;
            }
            emscripten_futex_wait(&m_box[1], (uint32_t)(seq - 1), 100.0);
        }
        return __atomic_load_n(&m_box[6], __ATOMIC_SEQ_CST) == (int32_t)length;
    }
};

class HostFileSource: public HddSource
{
public:
    explicit HostFileSource(const std::shared_ptr<HostFile> &file) : m_file(file) {}
    uint64_t size() const override { return m_file->size(); }
    bool read(uint64_t offset, uint8_t * buffer, size_t length) override
    {
        return m_file->read(offset, buffer, length);
    }
private:
    std::shared_ptr<HostFile> m_file;
};

// Files the page has handed over and no controller has taken yet. A
// controller takes its file out of the list: the file then lives as long as
// the image, and its worker stops with it
static std::map<int, std::shared_ptr<HostFile>> g_host_files;
static std::mutex g_host_files_mutex;
static int g_host_file_next = 1;

static const char HOST_FILE_PREFIX[] = "/host/";

// "/host/<id>/<name>" is a file of the list, anything else is not
static std::unique_ptr<HddSource> host_file_source(const std::string &file_name)
{
    const size_t prefix = sizeof(HOST_FILE_PREFIX) - 1;
    if (file_name.compare(0, prefix, HOST_FILE_PREFIX) != 0) return std::unique_ptr<HddSource>();
    const int id = atoi(file_name.c_str() + prefix);
    std::lock_guard<std::mutex> lock(g_host_files_mutex);
    auto it = g_host_files.find(id);
    if (it == g_host_files.end()) return std::unique_ptr<HddSource>();
    std::unique_ptr<HddSource> source(new HostFileSource(it->second));
    g_host_files.erase(it);
    return source;
}

// A hard disk controller by its device name
static HddImageOwner * wasm_hdd(const char * name)
{
    if (!g_emulator || !g_emulator->loaded || g_emulator->dm == nullptr || name == nullptr) return nullptr;
    return dynamic_cast<HddImageOwner*>(g_emulator->dm->get_device_by_name(std::string(name), false));
}

// A device command on the emulation thread: the controller resets itself on
// a new image, which must not happen under the running processor
static int wasm_hdd_command(const char * name, const std::string &command, const std::string &parameters)
{
    HddImageOwner * hdd = wasm_hdd(name);
    if (hdd == nullptr) return -2;
    ComputerDevice * device = dynamic_cast<ComputerDevice*>(hdd);
    int code = 0;
    try {
        g_emulator->invoke([&]() {
            emulator::Result res = device->send_command(command, parameters);
            if (!res) {
                g_load_error = res.message;
                code = -4;
            }
        });
    } catch (const std::exception &e) {
        g_load_error = e.what();
        code = -4;
    }
    return code;
}

int main()
{
    printf("eCat3 WASM: initializing...\n");

    g_renderer = new WasmRenderer();
    g_emulator = new Emulator(WORK_PATH, DATA_PATH, SOFTWARE_PATH, INI_FILE, g_renderer);
    // Inline data of a configuration and unpacked .ext.zip archives, in MEMFS
    g_emulator->cache_path = "/tmp/ecat3-cache/";
    // Hard disk images picked on the visitor's computer, read where they lie
    HddImage::source_factory = host_file_source;

    printf("eCat3 WASM: ready. Waiting for machine selection.\n");

    // With pthreads, main() returns and the browser event loop continues.
    // Emulation/render threads are spawned when wasm_load_machine() is called.
    return 0;
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
int wasm_load_machine(const char* cfg_path)
{
    if (!g_emulator || !g_renderer) return -1;

    g_load_error.clear();
    printf("eCat3 WASM: loading machine: %s\n", cfg_path);

    try {
        // Stop current emulation if running, and the demo script of the
        // previous machine with it
        g_emulator->stop_script();
        g_emulator->stop_emulation();

        // Load new configuration
        emulator::Result res = g_emulator->load_config(std::string(cfg_path));
        if (!res) {
            g_load_error = res.message;
            printf("eCat3 WASM: load_config failed: %s\n", res.message.c_str());
            return -2;
        }

        // A tape follows the motor line of the machine the way it does while
        // the desktop recorder window is open - on the page the recorder is
        // always in view. Its own sound gets the level that window gives it.
        // The callback runs on the emulation thread, the one that clocks the tape.
        for (ComputerDevice * dev : g_emulator->dm->find_devices_by_class("tape")) {
            TapeRecorder * tape = dynamic_cast<TapeRecorder*>(dev);
            if (tape == nullptr) continue;
            tape->on_mode_changed = [tape](unsigned int mode) {
                //Лентопротяжку может вести сама машина (Юниор): тогда окно -
                //и страница вместо него - только показывают, что происходит
                if (tape->is_machine_driven()) return;
                if (mode == TAPE_READ) tape->play(); else tape->stop();
            };
            tape->volume(10);
        }

        // Initialize video (nullptr for widget pointer - not used in WASM renderer)
        g_emulator->init_video(nullptr);

        // Start emulation
        g_emulator->run();

        // A configuration extension may carry a script: the start of a demo
        if (g_emulator->has_embedded_script()) {
            emulator::Result sres = g_emulator->load_embedded_script();
            if (sres) g_emulator->start_script();
            else printf("eCat3 WASM: embedded script failed: %s\n", sres.message.c_str());
        }

        printf("eCat3 WASM: machine loaded and running.\n");
        return 0;
    } catch (const std::exception& e) {
        g_load_error = e.what();
        printf("eCat3 WASM: C++ exception: %s\n", e.what());
        return -10;
    } catch (...) {
        printf("eCat3 WASM: unknown C++ exception\n");
        return -11;
    }
}

// The .cfg a machine file is built on, the way the loader resolves it
// ("/computers/uknc/UKNC.cfg"), or an empty string when the file cannot be read
// at all. The page asks before it starts an extension downloaded from a link:
// the base machine and its ROMs live in a bundle of their own, and only the
// manifest of the build knows which one
EMSCRIPTEN_KEEPALIVE
const char* wasm_machine_base(const char* file_path)
{
    static std::string result;
    result.clear();
    g_load_error.clear();
    if (file_path == nullptr) return result.c_str();

    MachinePaths paths;
    paths.computers_path = WORK_PATH;
    if (g_emulator) paths.cache_path = g_emulator->cache_path;

    std::string base;
    emulator::Result res = machine_base_file(std::string(file_path), paths, base);
    if (!res) {
        g_load_error = res.message;
        printf("eCat3 WASM: %s: %s\n", file_path, res.message.c_str());
        return result.c_str();
    }
    result = base;
    return result.c_str();
}

// 1 for a file that carries a whole machine inside itself - a saved state.
// The page then fetches nothing else: no base machine of this build, no bundle
// of ROMs, which is the one way a load= address fails today. Answered by the
// core rather than by a regular expression in the page, so that a format added
// later is recognised in one place
EMSCRIPTEN_KEEPALIVE
int wasm_machine_selfcontained(const char* file_path)
{
    if (file_path == nullptr) return 0;
    return is_state_file(std::string(file_path)) ? 1 : 0;
}

// The message of the last wasm_load_machine() or wasm_machine_base() that
// failed: the return code alone says nothing about a configuration the page
// downloaded itself
EMSCRIPTEN_KEEPALIVE
const char* wasm_last_error()
{
    return g_load_error.c_str();
}

EMSCRIPTEN_KEEPALIVE
void wasm_key_event(int key, int modifiers, int press)
{
    if (g_emulator && g_emulator->loaded) {
        g_emulator->key_event(key, modifiers, press != 0);
    }
}

// The on-screen keyboard speaks the machine's own key names, not host codes,
// so this path skips the ЙЦУКЕН -> ЯВЕРТЬ remap entirely.
EMSCRIPTEN_KEEPALIVE
void wasm_key_event_id(const char* id, int press)
{
    if (g_emulator && g_emulator->loaded && id != nullptr) {
        g_emulator->key_event_id(std::string(id), press != 0);
    }
}

// The mouse of the machine, moved by the host pointer the page has locked:
// steps, and the buttons (bit 0 left, bit 1 right) or -1 when they have not
// changed - as MainWindow::mouse_flush() hands them to the core
EMSCRIPTEN_KEEPALIVE
void wasm_mouse_event(int dx, int dy, int buttons)
{
    if (g_emulator && g_emulator->loaded) {
        g_emulator->mouse_event(dx, dy, buttons);
    }
}

// 0 the machine has no mouse, 1 it has one out of its socket, 2 one plugged in.
// The page offers the speed for the first, captures the pointer only for the last
EMSCRIPTEN_KEEPALIVE
int wasm_mouse_state()
{
    if (!g_emulator || !g_emulator->loaded || g_emulator->dm == nullptr) return 0;
    if (g_emulator->has_mouse()) return 2;
    return g_emulator->dm->find_devices_by_class("mouse").empty() ? 0 : 1;
}

// ccall(..., "string") copies the result at once, so one static buffer is safe
static Keyboard * wasm_keyboard()
{
    if (!g_emulator || !g_emulator->loaded || g_emulator->dm == nullptr) return nullptr;
    return dynamic_cast<Keyboard*>(g_emulator->dm->get_device_by_name("keyboard", false));
}

EMSCRIPTEN_KEEPALIVE
const char* wasm_keyboard_picture()
{
    static std::string result;
    Keyboard * k = wasm_keyboard();
    result = (k != nullptr) ? k->picture_file() : "";
    return result.c_str();
}

// 0 hold, 1 toggle, 2 tap -- see Keyboard::ClickMode. The policy belongs to the
// core so that the page and the desktop window treat a modifier the same way.
EMSCRIPTEN_KEEPALIVE
int wasm_key_click_mode(const char* id)
{
    Keyboard * k = wasm_keyboard();
    if (k == nullptr || id == nullptr) return 0;
    return static_cast<int>(k->click_mode(std::string(id)));
}

// The keys this machine actually has, so the page can light up and wire only
// those, the way the desktop window does.
EMSCRIPTEN_KEEPALIVE
const char* wasm_key_ids()
{
    static std::string result;
    result.clear();
    Keyboard * k = wasm_keyboard();
    if (k != nullptr) {
        const std::vector<std::string> &ids = k->key_ids();
        for (size_t i = 0; i < ids.size(); i++) {
            if (i > 0) result += ",";
            result += ids[i];
        }
    }
    return result.c_str();
}

EMSCRIPTEN_KEEPALIVE
const char* wasm_keys_pressed()
{
    static std::string result;
    result.clear();
    Keyboard * k = wasm_keyboard();
    if (k != nullptr) {
        const std::vector<std::string> held = k->ids_held();
        for (size_t i = 0; i < held.size(); i++) {
            if (i > 0) result += ",";
            result += held[i];
        }
    }
    return result.c_str();
}

// Indicator lamps that are not lit, so the page can black them out. The lit one
// is left exactly as the drawing has it, which is the whole point of a lamp.
EMSCRIPTEN_KEEPALIVE
const char* wasm_leds_dark()
{
    static std::string result;
    result.clear();
    Keyboard * k = wasm_keyboard();
    if (k != nullptr) {
        const std::vector<Keyboard::Indicator> leds = k->indicators();
        for (size_t i = 0; i < leds.size(); i++)
            if (!leds[i].lit) {
                if (!result.empty()) result += ",";
                result += leds[i].id;
            }
    }
    return result.c_str();
}

// Grows by one on every reset of the keyboard. The panel latches modifiers on
// its own, so it watches this and starts over: after a reset the machine holds
// nothing, and a panel still showing УПР latched would send control codes.
EMSCRIPTEN_KEEPALIVE
int wasm_kbd_reset_count()
{
    Keyboard * k = wasm_keyboard();
    return (k != nullptr) ? static_cast<int>(k->reset_count()) : 0;
}

// "led_rus=key_rus,led_lat=key_lat": which key each lamp makes redundant. Read
// once, when the drawing is put into the page: a key whose lamp is there stops
// being highlighted, so the alphabet is shown in one place, not two.
EMSCRIPTEN_KEEPALIVE
const char* wasm_led_keys()
{
    static std::string result;
    result.clear();
    Keyboard * k = wasm_keyboard();
    if (k != nullptr) {
        const std::vector<Keyboard::Indicator> leds = k->indicators();
        for (size_t i = 0; i < leds.size(); i++) {
            if (leds[i].key.empty()) continue;
            if (!result.empty()) result += ",";
            result += leds[i].id + "=" + leds[i].key;
        }
    }
    return result.c_str();
}

EMSCRIPTEN_KEEPALIVE
void wasm_reset(int cold)
{
    if (g_emulator) {
        g_emulator->reset(cold != 0);
    }
}

EMSCRIPTEN_KEEPALIVE
void wasm_set_volume(int value)
{
    if (g_emulator) {
        g_emulator->set_volume(value);
    }
}

EMSCRIPTEN_KEEPALIVE
void wasm_set_muted(int muted)
{
    if (g_emulator) {
        g_emulator->set_muted(muted != 0);
    }
}

EMSCRIPTEN_KEEPALIVE
int wasm_get_screen_width()
{
    unsigned int sx = 0, sy = 0;
    if (g_emulator) {
        g_emulator->get_screen_constraints(&sx, &sy);
    }
    return static_cast<int>(sx);
}

EMSCRIPTEN_KEEPALIVE
int wasm_get_screen_height()
{
    unsigned int sx = 0, sy = 0;
    if (g_emulator) {
        g_emulator->get_screen_constraints(&sx, &sy);
    }
    return static_cast<int>(sy);
}

static void md_append(const MD_CHAR* text, MD_SIZE size, void* result)
{
    static_cast<std::string*>(result)->append(text, size);
}

// A markdown file of the virtual FS as HTML, rendered the way the desktop
// chooser renders a machine description: md4c with tables. Empty when the
// file is not there.
EMSCRIPTEN_KEEPALIVE
const char* wasm_md2html(const char* file_path)
{
    static std::string result;
    result.clear();
    if (file_path == nullptr) return result.c_str();

    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return result.c_str();
    const std::string md((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    md_html(md.c_str(), static_cast<MD_SIZE>(md.size()), md_append, &result, MD_FLAG_TABLES, 0);
    return result.c_str();
}

// The device options of the machine, the ones the desktop puts on its toolbar:
// one line per option, the fields separated by tabs:
//   device  option_id  type  title  current_value_id  value_id  value_title  [value_id  value_title ...]
// type is "list" for a dropdown and "toggle" for a button that stays pressed
// (values 0 and 1).
// The titles are the untranslated source strings; the page translates them.
EMSCRIPTEN_KEEPALIVE
const char* wasm_device_options()
{
    static std::string result;
    result.clear();
    if (!g_emulator || !g_emulator->loaded || g_emulator->dm == nullptr) return result.c_str();

    for (unsigned int i = 0; i < g_emulator->dm->device_count; i++) {
        ComputerDevice * dev = g_emulator->dm->get_device(i)->device.get();
        DeviceOptions options = dev->get_device_options();
        for (size_t j = 0; j < options.size(); j++) {
            const DeviceOption & opt = options[j];
            if ((opt.type != DEVICE_OPTION_DROPDOWN && opt.type != DEVICE_OPTION_TOGGLE) || opt.values.empty()) continue;
            result += dev->name + "\t" + std::to_string(opt.id)
                    + (opt.type == DEVICE_OPTION_TOGGLE ? "\ttoggle\t" : "\tlist\t")
                    + opt.title + "\t" + std::to_string(opt.current);
            for (size_t v = 0; v < opt.values.size(); v++)
                result += "\t" + std::to_string(opt.values[v].id) + "\t" + opt.values[v].title;
            result += "\n";
        }
    }
    return result.c_str();
}

EMSCRIPTEN_KEEPALIVE
int wasm_set_device_option(const char* device_name, int option_id, int value_id)
{
    if (!g_emulator || !g_emulator->loaded || g_emulator->dm == nullptr || device_name == nullptr) return -1;
    ComputerDevice * dev = g_emulator->dm->get_device_by_name(std::string(device_name), false);
    if (dev == nullptr) return -2;
    dev->set_device_option(static_cast<unsigned>(option_id), static_cast<unsigned>(value_id));
    return 0;
}

// The file filter of the system section ("files"), the one the desktop gives
// its "Load a file" dialog. Empty when the machine takes no files directly, and
// then the page shows no such button.
EMSCRIPTEN_KEEPALIVE
const char* wasm_system_files()
{
    static std::string result;
    result.clear();
    if (!g_emulator || !g_emulator->loaded) return result.c_str();
    result = g_emulator->get_system_data()->allowed_files;
    return result.c_str();
}

// Puts a file of the virtual FS straight into the memory of the machine, the
// way the desktop "Load a file" does; the extension picks the format. Answers
// an empty string on success, the message of the core otherwise.
EMSCRIPTEN_KEEPALIVE
const char* wasm_open_file(const char* file_path)
{
    static std::string result;
    result.clear();
    if (!g_emulator || !g_emulator->loaded || file_path == nullptr) {
        result = "No machine";
        return result.c_str();
    }

    try {
        emulator::Result res = HandleExternalFile(g_emulator, std::string(file_path));
        if (!res) {
            result = res.message.empty() ? "Error" : res.message;
            printf("eCat3 WASM: loading '%s' failed: %s\n", file_path, result.c_str());
        }
    } catch (const std::exception& e) {
        result = e.what();
        printf("eCat3 WASM: loading '%s' failed: %s\n", file_path, e.what());
    }
    return result.c_str();
}

// A tape recorder by its device name, or nullptr when there is no such device
static TapeRecorder * wasm_tape(const char * name)
{
    if (!g_emulator || !g_emulator->loaded || g_emulator->dm == nullptr || name == nullptr) return nullptr;
    return dynamic_cast<TapeRecorder*>(g_emulator->dm->get_device_by_name(std::string(name), false));
}

// One line per tape recorder, the fields separated by tabs:
//   name  mode  position  total  recorded  record_name  files  loaded_name
// mode is TAPE_STOPPED, TAPE_READ, TAPE_FORWARD or TAPE_BACK, position and
// total are in seconds, recorded is the size of what the machine has written
// so far. loaded_name is the file on the tape, whoever put it there - the page
// or the machine's own configuration.
EMSCRIPTEN_KEEPALIVE
const char* wasm_tape_info()
{
    static std::string result;
    result.clear();
    if (!g_emulator || !g_emulator->loaded || g_emulator->dm == nullptr) return result.c_str();

    for (ComputerDevice * dev : g_emulator->dm->find_devices_by_class("tape")) {
        TapeRecorder * tape = dynamic_cast<TapeRecorder*>(dev);
        if (tape == nullptr) continue;
        result += tape->name + "\t"
                + std::to_string(tape->get_mode()) + "\t"
                + std::to_string(tape->get_position()) + "\t"
                + std::to_string(tape->get_total()) + "\t"
                + std::to_string(tape->get_record_size()) + "\t"
                + tape->get_record_name() + "\t"
                + tape->files + "\t"
                + tape->get_loaded_name() + "\n";
    }
    return result.c_str();
}

// Puts a file of the virtual FS on the tape. How it goes there is the
// [TapeFiles] entry for its extension, a machine specific one winning over the
// generic one, exactly as in the desktop recorder window.
EMSCRIPTEN_KEEPALIVE
int wasm_tape_load(const char* device_name, const char* file_path)
{
    TapeRecorder * tape = wasm_tape(device_name);
    if (tape == nullptr || file_path == nullptr) return -2;

    std::string ext = dsk_tools::get_file_ext(file_path);
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    if (ext.empty()) return -3;

    SystemData * sd = g_emulator->get_system_data();
    std::string fmt = g_emulator->read_setup("TapeFiles", sd->system_type + "." + ext, "");
    if (fmt.empty()) fmt = g_emulator->read_setup("TapeFiles", ext, "");
    if (fmt.empty()) return -3;

    emulator::Result res = tape->load_file(std::string(file_path), fmt);
    if (!res) {
        printf("eCat3 WASM: tape load_file failed: %s\n", res.message.c_str());
        return -4;
    }
    return 0;
}

// 0 play, 1 stop, 2 rewind, 3 recording (value 0/1), 4 mute (value 0/1),
// 5 wind to the end
EMSCRIPTEN_KEEPALIVE
int wasm_tape_control(const char* device_name, int action, int value)
{
    TapeRecorder * tape = wasm_tape(device_name);
    if (tape == nullptr) return -2;
    switch (action) {
        case 0: tape->play(); break;
        case 1: tape->stop(); break;
        case 2: tape->rewind(); break;
        case 3: tape->set_recording(value != 0); break;
        case 4: tape->mute(value != 0); break;
        case 5: tape->wind_to_end(); break;
        default: return -3;
    }
    return 0;
}

// Writes what the machine has recorded to a file of the virtual FS, for the
// page to hand to the browser. Answers the size, 0 when nothing was recorded.
EMSCRIPTEN_KEEPALIVE
int wasm_tape_save(const char* device_name, const char* file_path)
{
    TapeRecorder * tape = wasm_tape(device_name);
    if (tape == nullptr || file_path == nullptr) return -2;

    const std::vector<uint8_t> * data = tape->get_record_data();
    if (data->empty()) return 0;

    std::ofstream file(file_path, std::ios::binary);
    if (!file.is_open()) return -4;
    file.write(reinterpret_cast<const char*>(data->data()), static_cast<std::streamsize>(data->size()));
    return static_cast<int>(data->size());
}

// A drive by its device name, or nullptr when there is no such drive
static FDD * wasm_fdd(const char * name)
{
    if (!g_emulator || !g_emulator->loaded || g_emulator->dm == nullptr || name == nullptr) return nullptr;
    return dynamic_cast<FDD*>(g_emulator->dm->get_device_by_name(std::string(name), false));
}

// The name a drive goes by on the page, "title" of its configuration (MY0, A:),
// empty when the configuration gives none and the page numbers the drives
static std::string wasm_drive_title(const ComputerDevice * device)
{
    std::string title = str_trim(device->config_parameter("title"));
    for (size_t j = 0; j < title.size(); j++)
        if (title[j] == '\t' || title[j] == '\n') title[j] = ' ';
    return title;
}

// One line per drive of the machine, the fields separated by tabs, because the
// file filters themselves carry '|', ';' and spaces:
//   name  loaded  protected  led  file_name  files  files_save  title
// The page builds a block per line and polls this for the lamp and the disk.
EMSCRIPTEN_KEEPALIVE
const char* wasm_fdd_info()
{
    static std::string result;
    result.clear();
    if (!g_emulator || !g_emulator->loaded || g_emulator->dm == nullptr) return result.c_str();

    std::vector<ComputerDevice*> devices = g_emulator->dm->find_devices_by_class("fdd");
    for (size_t i = 0; i < devices.size(); i++) {
        FDD * fdd = dynamic_cast<FDD*>(devices[i]);
        if (fdd == nullptr) continue;
        std::string file = fdd->get_loaded() ? fdd->file_name : "";
        for (size_t j = 0; j < file.size(); j++)
            if (file[j] == '\t' || file[j] == '\n') file[j] = ' ';
        result += fdd->name + "\t"
                + (fdd->get_loaded() ? "1" : "0") + "\t"
                + (fdd->is_protected() ? "1" : "0") + "\t"
                + (fdd->is_led_on() ? "1" : "0") + "\t"
                + file + "\t"
                + fdd->files + "\t"
                + fdd->files_save + "\t"
                + wasm_drive_title(fdd) + "\n";
    }
    return result.c_str();
}

EMSCRIPTEN_KEEPALIVE
int wasm_fdd_eject(const char* device_name)
{
    FDD * fdd = wasm_fdd(device_name);
    if (fdd == nullptr) return -2;
    fdd->unload();
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int wasm_fdd_protect(const char* device_name, int on)
{
    FDD * fdd = wasm_fdd(device_name);
    if (fdd == nullptr) return -2;
    if (fdd->is_protected() != (on != 0)) fdd->change_protection();
    return 0;
}

// The image goes to a file of the virtual FS; its extension picks the format,
// exactly as on the desktop. The page reads it back and hands it to the browser.
EMSCRIPTEN_KEEPALIVE
int wasm_fdd_save(const char* device_name, const char* file_path)
{
    FDD * fdd = wasm_fdd(device_name);
    if (fdd == nullptr || file_path == nullptr) return -2;
    if (!fdd->get_loaded()) return -3;

    emulator::Result res = fdd->save_image(std::string(file_path));
    if (!res) {
        printf("eCat3 WASM: save_image failed: %s\n", res.message.c_str());
        return -4;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int wasm_load_file(const char* device_name, const char* file_path)
{
    FDD * fdd = wasm_fdd(device_name);
    if (fdd == nullptr || file_path == nullptr) {
        printf("eCat3 WASM: drive '%s' not found\n", device_name ? device_name : "");
        return -2;
    }

    emulator::Result res = fdd->load_image(std::string(file_path));
    if (!res) {
        printf("eCat3 WASM: load_image failed: %s\n", res.message.c_str());
        return -4;
    }

    printf("eCat3 WASM: loaded disk image '%s' into %s\n", file_path, device_name);
    return 0;
}

// A file of the visitor's computer, of the given size, is about to be read by
// a worker of the page. Answers its number; the image is then attached as
// "/host/<number>/<name>". The page gives the worker the wasm memory, which
// only code inside the module sees, and the mailbox
EMSCRIPTEN_KEEPALIVE
int wasm_host_file_create(double size)
{
    if (size <= 0) return 0;
    EM_ASM({ Module.ecatMemory = wasmMemory; });
    std::lock_guard<std::mutex> lock(g_host_files_mutex);
    const int id = g_host_file_next++;
    g_host_files[id] = std::make_shared<HostFile>((uint64_t)size);
    return id;
}

EMSCRIPTEN_KEEPALIVE
int wasm_host_file_box(int id)
{
    std::lock_guard<std::mutex> lock(g_host_files_mutex);
    auto it = g_host_files.find(id);
    return (it == g_host_files.end()) ? 0 : (int)(uintptr_t)it->second->box();
}

// The page is done with the attach: a file no controller took is dropped,
// which stops its worker
EMSCRIPTEN_KEEPALIVE
void wasm_host_file_release(int id)
{
    std::shared_ptr<HostFile> file;
    {
        std::lock_guard<std::mutex> lock(g_host_files_mutex);
        auto it = g_host_files.find(id);
        if (it == g_host_files.end()) return;
        file = it->second;
        g_host_files.erase(it);
    }
}

// One line per hard disk of the machine, tab separated:
//   name  loaded  protected  led  file_name  files  title
EMSCRIPTEN_KEEPALIVE
const char* wasm_hdd_info()
{
    static std::string result;
    result.clear();
    if (!g_emulator || !g_emulator->loaded || g_emulator->dm == nullptr) return result.c_str();

    std::vector<ComputerDevice*> devices = g_emulator->dm->find_devices_by_class("hdd");
    for (size_t i = 0; i < devices.size(); i++) {
        HddImageOwner * owner = dynamic_cast<HddImageOwner*>(devices[i]);
        if (owner == nullptr) continue;
        HddImage &image = owner->hdd_image();
        std::string file = image.attached() ? image.file_name() : "";
        for (size_t j = 0; j < file.size(); j++)
            if (file[j] == '\t' || file[j] == '\n') file[j] = ' ';
        std::string files = owner->hdd_files();
        for (size_t j = 0; j < files.size(); j++)
            if (files[j] == '\t' || files[j] == '\n') files[j] = ' ';
        result += devices[i]->name + "\t"
                + (image.attached() ? "1" : "0") + "\t"
                + (image.write_protect() ? "1" : "0") + "\t"
                + (image.is_led_on() ? "1" : "0") + "\t"
                + file + "\t"
                + files + "\t"
                + wasm_drive_title(devices[i]) + "\n";
    }
    return result.c_str();
}

EMSCRIPTEN_KEEPALIVE
int wasm_hdd_load(const char* device_name, const char* file_path)
{
    if (file_path == nullptr) return -2;
    std::string path(file_path);
    for (size_t i = 0; i < path.size(); i++)
        if (path[i] == '"' || path[i] == ',') path[i] = '_';
    g_load_error.clear();
    return wasm_hdd_command(device_name, "load", "\"" + path + "\"");
}

EMSCRIPTEN_KEEPALIVE
int wasm_hdd_eject(const char* device_name)
{
    return wasm_hdd_command(device_name, "eject", "");
}

EMSCRIPTEN_KEEPALIVE
int wasm_hdd_protect(const char* device_name, int on)
{
    return wasm_hdd_command(device_name, "protect", on ? "1" : "0");
}

// The image as the machine sees it, written sectors included, into a file of
// the virtual FS for the page to hand to the browser
EMSCRIPTEN_KEEPALIVE
int wasm_hdd_save(const char* device_name, const char* file_path)
{
    HddImageOwner * hdd = wasm_hdd(device_name);
    if (hdd == nullptr || file_path == nullptr) return -2;
    if (!hdd->hdd_image().attached()) return -3;
    std::vector<uint8_t> image;
    if (!hdd->hdd_image().contents(image)) return -4;
    std::ofstream out(file_path, std::ios::binary);
    if (!out) return -4;
    out.write(reinterpret_cast<const char*>(image.data()), (std::streamsize)image.size());
    return out ? 0 : -4;
}

//--------------------------- Remembered disks ------------------------------//
// The page keeps the entries of [Disks] in the browser's storage, the floppy
// images themselves too, since a file of the visitor's computer cannot be
// opened again by its name. The ini of the core lives in MEMFS and is gone
// with the tab: before every load the page empties its [Disks], puts back the
// entries of the machine it is about to start and says whether to restore.
// The core then restores them exactly as on the desktop, before the machine runs

EMSCRIPTEN_KEEPALIVE
void wasm_disks_prepare(int restore)
{
    if (!g_emulator) return;
    g_emulator->forget_all_disks();
    g_emulator->set_restore_disks(restore != 0);
}

EMSCRIPTEN_KEEPALIVE
void wasm_disk_set_entry(const char* key, const char* value)
{
    if (!g_emulator || key == nullptr || value == nullptr) return;
    g_emulator->write_setup("Disks", key, value);
}

static ComputerDevice * wasm_disk_device(const char * name)
{
    if (!g_emulator || !g_emulator->loaded || g_emulator->dm == nullptr || name == nullptr) return nullptr;
    return g_emulator->dm->get_device_by_name(std::string(name), false);
}

// Writes what the drive holds into [Disks] (or drops the entry when it is the
// configuration's) and answers "key<TAB>value", the value empty for no entry
EMSCRIPTEN_KEEPALIVE
const char* wasm_disk_remember(const char* device_name)
{
    static std::string result;
    result.clear();
    ComputerDevice * dev = wasm_disk_device(device_name);
    if (dev == nullptr) return result.c_str();
    g_emulator->remember_disk(dev);
    const std::string key = g_emulator->disk_key(dev->name);
    result = key + "\t" + g_emulator->read_setup("Disks", key, "");
    return result.c_str();
}

// The configuration's image back into the drive; answers the key of the entry
// it dropped, empty when there is no such drive
EMSCRIPTEN_KEEPALIVE
const char* wasm_disk_default(const char* device_name)
{
    static std::string result;
    result.clear();
    ComputerDevice * dev = wasm_disk_device(device_name);
    if (dev == nullptr) return result.c_str();
    try {
        g_emulator->restore_default_disk(dev);
    } catch (const std::exception &) {
    }
    result = g_emulator->disk_key(dev->name);
    return result.c_str();
}

// The key a drive of the machine that is loaded is remembered under
EMSCRIPTEN_KEEPALIVE
const char* wasm_disk_key(const char* device_name)
{
    static std::string result;
    result.clear();
    ComputerDevice * dev = wasm_disk_device(device_name);
    if (dev != nullptr) result = g_emulator->disk_key(dev->name);
    return result.c_str();
}

} // extern "C"