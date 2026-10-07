// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Main emulator class, header

#pragma once

#include <memory>
#include <map>
#include <array>
#include <string>
#include <functional>
#include <atomic>

#include "libs/ini_wrapper.h"

#include "thread_compat.h"

#ifdef RENDERER_SDL2
    #include <SDL.h>
#endif

#include "core.h"
#include "config_ext.h"
#include "emulator/devices/common/keyboard.h"
#include "emulator/devices/common/joystick.h"
#include "emulator/devices/common/mouse.h"
#include "emulator/script/script_engine.h"
#include "emulator/script/script_recorder.h"
#include "renderer.h"
#include "video_recorder.h"

class GenericSound;

//Every device type a configuration may name. Used by the emulator and by the
//configuration editor, which constructs the devices of a machine it does not
//run to ask them what can be changed (see config_fields.h)
void register_all_devices(DeviceManager * dm);

//What a drive holds: an image (empty: none) and its write protection
struct DiskChoice {
    std::string file;
    bool protect = false;
    bool operator==(const DiskChoice &o) const { return file == o.file && protect == o.protect; }
    bool operator!=(const DiskChoice &o) const { return !(*this == o); }
};

class Emulator
{
private:
    IniSettings settings;
    bool busy;
    InterfaceManager *im;
    SystemData sd;
    //The configuration of the machine that is loaded, already resolved. Lives
    //as long as the machine does: ComputerDevice::cd points into it
    EmulatorConfig m_config;
    MachineSource m_source;
    bool m_embedded_loaded = false;     //The script buffer holds m_source.script
    VideoRenderer * renderer;

    std::array<std::string, 256> charmap;

    //One clock domain per processor. A machine with two of them (the УК-НЦ:
    //8 MHz central and 6.25 MHz peripheral) runs them interleaved, each in its
    //own address space and with its own devices. With one processor scale is 1
    //and the whole thing collapses into the loop this used to be
    struct ClockDomain {
        CPU *    cpu;
        uint64_t scale;     // LCM(all frequencies) / this frequency
        uint64_t norm;      // position in that normalised time, carried between slices
    };
    std::vector<ClockDomain> domains;

    CPU * cpu;              // The master, domains[0].cpu - and the machine's time base
    MemoryMapper * mm;
    GenericDisplay * display;
    Keyboard * keyboard;
    std::vector<Joystick*> joysticks;   // every device of the joystick class, fed with the same keys
    std::vector<HostMouse*> mice;           // every device of the mouse class, moved together

    // Host keys held down, see host_key()
    struct HostKey {
        unsigned int scan;              // native scan code, 0 if the event has none
        int key;                        // the code the key went down as
    };
    std::vector<HostKey> m_host_keys;   // Guarded by m_host_keys_mutex
    //The window presses and releases on the GUI thread, a restored state
    //empties the list on the emulation thread
    compat_mutex m_host_keys_mutex;

    unsigned int clock_freq;

    unsigned int screen_sx;
    unsigned int screen_sy;
    double screen_scale = 1;
    double pixel_scale;
    int screen_ratio = SCREEN_RATIO_43;
    int screen_filtering = 0;

    void register_devices();
    //The two halves of load_config(): what is there goes, the new one is built
    void drop_machine();
    emulator::Result build_machine(const std::string &file_name);

    std::atomic<bool> m_running;
#if USE_QT_THREADING
    EmuThread* emulationThread = nullptr;
    EmuThread* renderThread = nullptr;
#else
    std::thread emulationThread;
    std::thread renderThread;
#endif
    void setThreadPriority(bool timeCritical);
    std::atomic<bool> m_ready;

    std::unique_ptr<ScriptEngine> script;
    std::unique_ptr<ScriptRecorder> recorder;
    //An empty file name no longer means "nothing was asked for": an external
    //driver wants the bytes without a file, so the request is a flag of its own
    std::string m_screenshot_file;              //Guarded by m_screenshot_mutex
    bool m_screenshot_requested = false;        //Guarded by m_screenshot_mutex
    std::vector<unsigned char> m_screenshot_png;//Guarded; the last image encoded
    uint64_t m_screenshot_serial = 0;           //Guarded; incremented for every image
    compat_mutex m_screenshot_mutex;
    //A snapshot asked for from another thread, taken at a slice boundary the
    //way a screenshot is. A script does not go through this: its tick() runs
    //on the emulation thread already, between two instructions
    std::string m_state_file;                   //Guarded by m_state_mutex
    bool m_state_requested = false;             //Guarded
    std::string m_state_error;                  //Guarded
    uint64_t m_state_serial = 0;                //Guarded; incremented for every request
    compat_mutex m_state_mutex;
    void store_state();

    //Calls handed over by invoke(), waiting for the emulation thread to reach
    //the gap between two time slices. Each entry lives on the stack of the
    //thread that waits for it
    struct PendingCall {
        const std::function<void()> * fn;
        std::atomic<bool> done;
        bool failed;
        std::string error;
        explicit PendingCall(const std::function<void()> * f) : fn(f), done(false), failed(false) {}
    };
    std::vector<PendingCall*> m_calls;          //Guarded by m_calls_mutex
    bool m_calls_open = false;                  //Guarded; an emulation thread is there to take them
    compat_thread_id m_calls_thread = compat_thread_id();   //Guarded; that thread
    std::atomic<bool> m_calls_pending;          //The queue is not empty: all the loop looks at
    compat_mutex m_calls_mutex;
    void run_pending_calls();
    void close_calls();
    //Closes the queue when the emulation thread leaves, however it leaves
    struct CallsGuard {
        Emulator * e;
        explicit CallsGuard(Emulator * emulator) : e(emulator) {}
        ~CallsGuard() { e->close_calls(); }
    };

    //Why the machine stopped on its own, see machine_error()
    std::string m_machine_error;                //Guarded by m_error_mutex
    bool m_machine_fatal = false;               //Guarded
    mutable compat_mutex m_error_mutex;
    void set_machine_error(const std::string &message, bool fatal);

    bool m_settings_readonly = false;

    //Remembered disks, see DiskChoice
    bool m_restore_disks = false;
    std::map<std::string, DiskChoice> m_disk_defaults;
    std::vector<ComputerDevice*> disk_devices();
    std::string machine_disk_key(const std::string &device) const;
    std::string root_path() const;
    std::string disk_path(const std::string &file);
    void apply_disk_choice(ComputerDevice * dev, const DiskChoice &c);
    void record_disk_choice(ComputerDevice * dev, const DiskChoice &from, const DiskChoice &to);
    void restore_disks();
    void store_screenshot();

    //The video being recorded. The render thread feeds it the machine's
    //picture (unless the frontend sends the picture of the window) and the
    //sound of every audio output, see VideoRecorder
    VideoRecorder m_video;
    std::atomic<bool> m_video_from_screen;
    //Samples of each audio output not yet mixed: the outputs may be clocked
    //by different processors and hand out their samples unevenly
    std::vector<std::vector<int16_t> > m_video_audio;
    compat_mutex m_video_audio_mutex;
    bool m_video_capturing = false;     //The sound outputs capture for it; GUI thread
    void collect_video_audio(bool flush);
    std::vector<GenericSound*> sound_outputs();

    //Applies the @state section of a .ecats. Called by run() on the emulation
    //thread, after reset(true) and before the loop starts, so that no device
    //is clocked between the reset and the restore
    emulator::Result apply_state();
    //What the state file said that this build could not use: unknown devices,
    //unknown keys, sections with no device. Never printed - any output on
    //stderr fails every regression test - but kept for whoever asks
    std::string m_state_report;

public:
    DeviceManager *dm;
    std::string work_path;
    std::string data_path;
    std::string software_path;
    //Where inline data of a configuration and unpacked .ext.zip archives are
    //written. Set by the frontend before the first load_config()
    std::string cache_path;
    //Configurations of the user (.ext), listed by the machine chooser next to
    //those of computers/
    std::string user_ext_path;

    bool loaded;

    //CPU cycles since the machine was started. Written on the emulation
    //thread for every instruction; the interface thread reads it to time
    //stamp recorded events, a stale or torn value there costs at most one
    //delay that is a few milliseconds off
    uint64_t clock_counter;

    Emulator(std::string work_path, std::string data_path, std::string software_path, std::string ini_file, VideoRenderer * renderer);
    ~Emulator();

    emulator::Result load_config(std::string file_name);
    void apply_saved_device_options();

    //------------------------ Remembered disks -----------------------------//
    //The third step after .cfg and .ext: what the user put into a drive (an
    //image, nothing, write protection) is kept in [Disks] of the ini, keyed by
    //the file that was opened - not by the base of its chain - and the drive
    //name. Only the windowed frontend turns the restore on, and only when no
    //script, replay or MCP client drives the machine
    //Applies to the next load_config()
    void set_restore_disks(bool on) { m_restore_disks = on; }
    //What the drive holds now, and what the machine's configuration put in it
    DiskChoice disk_state(ComputerDevice * dev);
    DiskChoice disk_default(const std::string &device) const;
    //Writes the drive's state into the ini, or removes the entry when the
    //state is the configuration's own
    void remember_disk(ComputerDevice * dev);
    bool has_remembered_disk(const std::string &device);
    //The key of the drive's entry in [Disks], for a frontend that keeps the
    //entries elsewhere (the web page: in the browser's storage)
    std::string disk_key(const std::string &device) const { return machine_disk_key(device); }
    //Puts the configuration's image back (on the emulation thread) and drops
    //the entry. Also records the commands while recording
    void restore_default_disk(ComputerDevice * dev);
    void forget_all_disks();
    //Commands that turn the configuration's drives into what they hold now.
    //A recording starts with them, since a replay restores nothing
    void record_disk_changes();

    //--------------------------- Saved state ------------------------------//
    //Writes everything the machine is into file_name: a .ecats.zip archive,
    //or a .ecats text file with a <stem>.files/ directory beside it. Runs on
    //the emulation thread - from a script verb, which is already there, or
    //through request_state() from any other
    emulator::Result save_state(const std::string &file_name);
    //Asks for a snapshot at the next safe point. Thread safe
    void request_state(const std::string &file_name);
    //True while a requested snapshot has not been taken yet. False means it is
    //finished, and take_state_error() then says whether it worked
    bool state_pending();
    //Empty until a requested snapshot has failed, and cleared by reading it
    std::string take_state_error();
    //What the state that was loaded did not fit into this build
    const std::string & state_report() const { return m_state_report; }

    std::string read_setup(std::string section, std::string ident, std::string def_val);
    void write_setup(std::string section, std::string ident, std::string new_val);

    //The directory the file dialogs open in. The distributed ini carries the
    //entry with an empty value, which is not a directory any more than a
    //missing entry is: both mean the software directory rather than whatever
    //the current one happens to be
    std::string get_last_path();
    void set_last_path(const std::string &path);

    //An external driver runs many short sessions in the working tree, and
    //every one of them would otherwise rewrite the ini file
    void set_settings_readonly(bool on) { m_settings_readonly = on; }

    //Silences the machine outright, audio device included. Has to be called
    //before load_config(): a sound device opens its driver while it loads
    void set_audio_enabled(bool on) { sd.audio_enabled = on; }
    void load_charmap();
    const std::string & translate_char(unsigned int system_code);

    void init_video(void *p);
    void stop_video();

    void run();

    // SURFACE * get_surface();
    void get_screen_constraints(unsigned int * sx, unsigned int * sy);

    SystemData * get_system_data();

    void set_scale(int scale);
    void set_ratio(int ratio);
    void set_filtering(int filtering);

    int get_scale();
    int get_ratio();
    int get_filtering();

    //Resolution of the machine's screen, 0 until the first frame. Written by
    //the render thread; a stale value only makes one mouse movement off
    void get_screen_size(unsigned int * sx, unsigned int * sy) const { *sx = screen_sx; *sy = screen_sy; }

    void timer_proc(uint64_t time_ticks);
    void render_screen();

    void key_event(int key, int modifiers, bool press);
    void key_event_id(const std::string &id, bool press);

    // A key of the host keyboard as a window of the frontend gets it, with the
    // physical key's scan code (0 if there is none). The windows share the list
    // of held keys: the main window and the on-screen keyboard both take the
    // focus, and a key pressed in one of them is released by whichever hears
    // the release. The release goes out as the code the key went down as - Qt
    // names a key after the layout and the modifiers of the moment, so Shift
    // let go before 2 turns the release of @ into one of 2. Returns that code,
    // the one to record. GUI thread only.
    int host_key(int key, unsigned int scan, int modifiers, bool press);
    // Releases, and records, every host key still held: a window that loses
    // the focus (Alt-Tab) never hears the releases, they go elsewhere
    void release_host_keys();

    // Movement of the host mouse in steps of the machine's mouse (right and
    // down are positive) and its buttons as a mask, a negative mask keeping
    // them as they are. Reaches only a mouse that is plugged in.
    void mouse_event(int dx, int dy, int buttons);
    bool has_mouse() const;             // A mouse is plugged in right now

    // The keyboard of the running machine, or nullptr between machines. Never
    // cache it: load_config() deletes the whole device manager.
    Keyboard * get_keyboard() const { return keyboard; }

    //Runs fn on the emulation thread, between two time slices, and returns
    //when it has run. This is how any other thread changes the machine: a
    //device is not locked, it belongs to the emulation thread, and a disk
    //swapped or a reset done from the window would otherwise land in the
    //middle of an instruction. With no emulation thread running - nothing
    //loaded, the machine stopped - and on that thread itself, fn runs at once.
    //An exception fn throws comes out of invoke() as std::runtime_error.
    //fn must not wait for the calling thread: that one is waiting here
    void invoke(const std::function<void()> &fn);

    void set_volume(int value);
    void set_muted(bool muted);
    //From any thread, see invoke()
    void reset(bool cold);

    //Why the machine stopped by itself: a device refused what the guest asked
    //of it, the saved state could not be applied, or - fatal - the emulation
    //thread ended on an exception and nothing runs any more. Empty when none
    //of that happened. The core never prints it (stderr fails every test):
    //the frontend decides whom to tell. Thread safe
    std::string machine_error(bool * fatal = nullptr) const;
    void clear_machine_error();
    void resize_screen();
    void stop_emulation();

    //--------------------------- Scripting --------------------------------//
    //A script is parsed before the machine is loaded, because its MACHINE
    //command may select the configuration to start with
    emulator::Result load_script(const std::string &file_name);

    //The @script part of the loaded configuration extension. The frontend
    //loads and starts it when nothing else drives the machine: an explicit
    //script, a replayed recording and an MCP client all take precedence
    bool has_embedded_script() const;
    emulator::Result load_embedded_script();
    const MachineSource & machine_source() const { return m_source; }
    std::string script_machine() const;
    void start_script();
    void stop_script();
    bool script_active() const;
    bool script_finished() const;
    bool script_exit_requested() const;
    int  script_exit_code() const;

    //The engine and the recorder are created on first use and live as long
    //as the emulator: the GUI record / replay controls work on them directly
    ScriptEngine * script_engine();
    ScriptRecorder * script_recorder();
    void set_script_file(const std::string &file_name);     //Names a recorded buffer after a saved file

    uint64_t clock_now() const { return clock_counter; }
    uint64_t ticks_per_ms() const;                          //CPU cycles per emulated millisecond

    //Recording hooks for the GUI, no-ops unless a recording is in progress
    void record_key(unsigned int code, unsigned int native, bool press);
    void record_command(const std::string &device, const std::string &member, const std::string &params);
    void record_verb(unsigned int verb, const std::vector<std::string> &args);
    void record_verb_at(unsigned int verb, const std::vector<std::string> &args, uint64_t clock);

    //Asks the render thread to store the current screen as a PNG. An empty
    //file name keeps the image in memory only, see take_screenshot_png()
    void request_screenshot(const std::string &file_name);
    bool is_screenshot_pending();

    //Copies the last image the render thread produced. The serial tells the
    //caller whether it is looking at its own image or at an older one
    bool take_screenshot_png(std::vector<unsigned char> &out, uint64_t * serial = nullptr);

    //------------------------- Video recording ----------------------------//
    //Starts a video of the running machine. from_screen: the frontend sends
    //the picture as the window shows it with video_frame(), otherwise the
    //render thread takes the machine's own pixels. GUI thread
    bool start_video_recording(VideoSettings s, bool from_screen, std::string &error);
    //Ends it; the file is finished in the background, see video_state()
    void stop_video_recording();
    //RGBA, any thread
    void video_frame(const uint8_t * rgba, unsigned int w, unsigned int h, bool bottom_up = false);
    //The machine's picture, RGBA, taken under the surface lock: the render
    //thread replaces the surface when the video mode changes
    std::vector<uint8_t> grab_screen(unsigned int * sx, unsigned int * sy);
    bool video_recording() const { return m_video.recording(); }
    VideoRecorder::State video_state() const { return m_video.state(); }
    std::string video_message() { return m_video.message(); }

private:
public:


};
