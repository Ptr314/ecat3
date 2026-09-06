# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**eCat3** is a universal emulator for retro computers (Orion-128, Radio-86RK, Agat-7, Agat-9, Apogey, Mikrosha, Irisha, and the 16-bit БК0010 / БК0010-01 / БК0011М) written in C++. The emulator core (`src/emulator/`) is **Qt-free** — it uses only the C++ standard library, making it portable to WebAssembly and other non-Qt targets. The Qt framework is used only for the desktop GUI layer (`src/mainwindow.*`, `src/dialogs/`, `src/renderers/`). The project targets Windows 7+, macOS, and Linux with support for multiple rendering backends (Qt, SDL2, OpenGL).

## Key Architecture

### Modular Device-Based Architecture

The emulator uses a device-based architecture where computers are built from interconnected components:

- **CPU Layer** (`emulator/devices/cpu/`): i8080, Z80, 6502, 65C02, К1801ВМ1 (PDP-11) - independent instruction execution engines
- **Memory Mapper** (`emulator/devices/common/page_mapper.h`): Central address-space manager that routes memory access to correct devices
- **Device Types**:
  - **Common Devices** (`emulator/devices/common/`): Generic peripherals (RAM, ROM, i8255, i8253, WD1793 and GMD70 disk controllers, etc.)
  - **Machine-Specific Devices** (`emulator/devices/specific/`): Agat/Orion/Irisha/БК display controllers, БК timer, FDC variants
- **Interface System**: Devices connect via interfaces (analogous to real hardware buses). Interfaces support bit-range connections and inversion.

### Configuration-Driven System Design

Computer configurations live in **`.cfg` files** (text format, see CONFIG.md for syntax). During startup:
1. Config parser reads device definitions and their parameter values
2. Devices are instantiated and registered in device list
3. Interface connections are established
4. System runs with defined architecture

This means most hardware behavior is **not hardcoded** - it's defined via config files. New machines can be added by creating `.cfg` files without code changes.

### Core Classes

- **`Core`** (`emulator/core.h`): Base device class with interface management, memory read/write hooks, message passing
- **`Emulator`** (`emulator/emulator.h`): Main orchestrator (plain C++ class, not QObject) - manages device lifecycle, CPU execution timing, rendering, keyboard input via `key_event(int key, int modifiers, bool press)`
- **`CPU`** (base), **`I8080`**, **`Z80`**, **`6502`**, **`65C02`**, **`k1801vm1`**: CPU implementations with instruction execution
- **`MemoryMapper`** (`emulator/devices/common/page_mapper.h`): Routes address space to devices
- **`GenericDisplay`**: Base for display devices; implementations handle video output
- **`InterfaceManager`**: Manages interface connections between devices (wires them together)
- **`ScriptEngine`** (`emulator/script/script_engine.h`): Runs `.ecat` test/automation scripts against the emulator (see Scripting below)

### Scripting

`src/emulator/script/` is a Qt-free script runner driving the emulator from a text file (`SCRIPTING.md` has the full command reference, in Russian). Launch with `eCat3 --script <file.ecat>` (or `--config <file.cfg>`; a positional argument is dispatched by extension) from the `deploy/` working directory.

Scripts load a machine (`MACHINE`), wait, press keys (`KEY`, `TYPE`), take screenshots (`SCREEN`), dump device state to a log (`LOG dev.field`, `LOGDEFS` picks width and base), send device commands (`COMMAND dev.cmd(args)`), and `EXIT`. This is the practical way to reproduce a bug or verify a machine without driving the GUI by hand.

Two things surprise people:
- **`WAIT` counts emulated time, not wall-clock.** If the config fails to load or the CPU is stopped, the clock never advances and the script hangs forever. Wrap batch runs in a timeout.
- **The log file is created lazily** next to the script, named `<script>-YYYY-MM-DD-HH-mm-SS.log`; a script that prints nothing leaves no file behind.
- **Relative paths in `COMMAND dev.load("...")` resolve from the script's directory first**, then the config's directory / `files/` / `deploy/software/<machine>`, then the working directory (`deploy/`). Test assets therefore live in `tests/files/` and are loaded as `"../files/x.dsk"` from `tests/scripts/`.
- **`TYPE` cannot type every character on every machine**: a key that exists only with Shift in a Rus-mode map entry (`:` on Irisha) comes out wrong. Use `KEY 50,50,shift+colon` instead.

Devices expose themselves to scripts through `get_device_fields()` / `get_field()` (readable state) and `get_device_commands()` / `send_command()` (actions), declared in `ComputerDevice` and overridden down the hierarchy. `AddressableDevice` already provides `value`/`value16`/`size` plus `set`/`set16`/`fill` (`set` and `set16` take a list of values for consecutive addresses, so machine code is one line per instruction), and `CPU` provides `pc`/`registers`/`flags` plus `stop`/`run`/`step`/`breakpoint`/`setreg`, so a new device usually needs nothing to be scriptable.

**GUI record / replay** (toolbar block, menu Emulation > Action recording) uses the same `ScriptEngine`: it owns the command buffer, and `ScriptRecorder` (`emulator/script/script_recorder.h`, Qt-free) appends commands built from GUI events, timed by `Emulator::clock_counter`. A press/release pair becomes `KEY delay,hold,name`; overlapping presses become `KEYDOWN` / `KEYUP`. `format_script_command()` is the serializer; arguments are stored in file syntax (escapes included), so a value built from scratch goes through `escape_script_text()` first. Threading rule: the emulation thread only calls `tick()`, which has a lock-free fast path and takes the engine mutex only when a command actually runs; the GUI edits the buffer (`append`/`truncate`/`seek`) only while the engine is not active. New GUI actions that should be replayable call `e->record_command()` / `e->record_verb()` right after performing the action (see `fdd_open`, the device-option combo lambda, `TapeRecorderWindow`).

### Rendering System

Three rendering backends controlled by compile flags:
- `RENDERER_QT`: Software rendering via Qt painter
- `RENDERER_SDL2`: Hardware-accelerated via SDL2 (preferred for Windows 7)
- `RENDERER_OPENGL` (default for Qt6 debug): OpenGL rendering (best performance)

Selected at build time via `CMakeLists.txt` compilation flags.

## Building

### Quick Start (Development)

Windows with MinGW + Qt Creator:
```bash
# Install Qt (6.8+), MinGW, CMake, Ninja, SDL2 dev files
# In Qt Creator: Projects > Build & Run > Debug Kit > Run
# Set Working directory to: <repo>/deploy
# Then build & run
```

macOS:
```bash
# Install Xcode, HomeBrew, CMake, Ninja
brew install cmake ninja
# Download Qt and SDL2 frameworks (see BUILD.md section 4-7)
# Use CLion/Qt Creator to build
```

Linux (Ubuntu 20.04+):
```bash
# Qt 6, CMake, Ninja, SDL2 dev: apt install / Qt installer
# Set CMAKE_PREFIX_PATH and PATH for Qt tools
cd src && cmake -B build && cmake --build build
```

### Release Builds

See `BUILD.md` for detailed multi-platform release procedures. Key points:
- **Windows**: Static Qt linking via custom build scripts (`.build/build-win-*.bat`)
- **macOS**: Universal binary (x86_64+arm64) with static Qt
- **Linux**: Dynamic Qt linking via linuxdeployqt AppImage creation

## Development Workflow

### Running Debug Builds

1. **In Qt Creator/CLion**: Set working directory to `deploy/` (configs and ROM files are there)
2. **Command line**: `./src/cmake-build-debug/eCat3` (adjust path as needed)

### Key Development Files

- **CMakeLists.txt** (`src/CMakeLists.txt`): Build configuration, renderer selection, Qt module setup
- **Configuration Files**: `deploy/computers/*.cfg` - machine definitions
- **Scripts**: `deploy/scripts/*.ecat` - automation/test scripts (`SCRIPTING.md`)
- **Translations**: `src/translations/*.ts` - i18n files (update via `.build/update_translations.bat`)

### Common Tasks

**Add a new CPU type:**
1. Create `emulator/devices/cpu/newcpu.cpp/h` inheriting from `CPU`
2. Implement instruction decode/execute cycle
3. Add to **both** source lists: `src/CMakeLists.txt` and `src/wasm/CMakeLists.txt` (the wasm list is maintained by hand — forgetting it breaks the web build only)
4. Register the factory in `Emulator::register_devices()` and add a `DisAsm` subclass plus a `debugwindow.cpp` branch if the instruction set does not fit the table-driven `deploy/data/*.dis` format (see `disasm_pdp11.cpp`)
5. Reference in `.cfg` files via `device_id: newcpu {}`

**Add a new device:**
1. Create `emulator/devices/common/newdevice.cpp/h` (or `specific/` for machine-specific)
2. Inherit from `ComputerDevice` (generic) or `AddressableDevice` (memory-mapped/port-mapped)
3. Declare `Interface` members in the class body; they self-register on construction
4. Implement `get_value(addr)` / `set_value(addr, val, force)` for addressable devices, `clock(counter)` for clocked devices, and `interface_callback(id, new_val, old_val)` for reactive devices
5. Add a `create_xxx(InterfaceManager*, EmulatorConfigDevice*)` factory function in the `.h`, implement it in the `.cpp`
6. Register the factory in `Emulator::register_devices()` in `emulator.cpp`
7. Add source files to **both** `src/CMakeLists.txt` and `src/wasm/CMakeLists.txt`
8. Optional: override `get_device_fields()` / `get_field()` and `get_device_commands()` / `send_command()` to expose state and actions to scripts

**Reserved device names:** Every config MUST have exactly one device named `"cpu"` and one named `"mapper"`. The emulator also looks up `"display"` and `"keyboard"` by name. These four names are hardcoded in `Emulator`.

**Add a new computer configuration:**
1. Create `.cfg` file in `deploy/computers/` subdirectory (e.g., `my_machine/config.cfg`)
2. Define system, CPU, memory, devices, and interface connections
3. Place ROM/disk images in same directory
4. Create matching `.md` file with user-facing description
5. No code recompilation needed

**Update translations:**
```bash
cd .build
./update_translations.bat  # Or update_translations.sh on Unix
# Edit .ts files in Qt Linguist
```

**Build variants** (`src/CMakeLists.txt`):
- `ENABLE_GUI` (ON): the windowed `eCat3`. With `OFF` no `find_package(Qt)` runs at all, so a console-only build configures on a machine without Qt.
- `ENABLE_HEADLESS` (OFF): a second target `eCat3-headless` built from the same `EMULATOR_SOURCES` plus `src/headless/`. Console subsystem, links no Qt, draws into `NullRenderer`. Needs a C++17 toolchain, so the XP/Win7 kits refuse it. It reproduces the regression suite byte for byte, references included.
- `ENABLE_MCP` (OFF): the MCP server (`src/mcp/`, `src/mcp_bridge.*`, vendored `libs/picojson`). Without the option not one of those files is compiled. A windowed MCP build requires `RENDERER_OPENGL` and fails configuration otherwise; see `MCP.md`.
- The release scripts take `mcp` and `headless` as arguments, see `.build/README.md`.

**Test changes:**
- Most behavior testing is via `.cfg` file modifications
- **Regression suite**: `python tests/run_tests.py` runs an `.ecat` script per machine and diffs the log, the screenshots and any file the script writes against `tests/expected/` (`--all` adds the slow tape/CPU tests, `-k <substr>` filters, `--update` re-records references, `-j N` parallelises). Emulated time makes every run byte-identical, so a screenshot is a valid reference. See `tests/README.md`; a machine or device change that alters behaviour means re-recording the affected references in the same commit
- **The same scripts through MCP**: `python tests/run_mcp_tests.py` replays every test against `<exe> --mcp` (`ecat_machine` plus one `ecat_run`) and diffs the answer against the same `expected/*.txt`, which is the only check of the `ScriptSink` path. A live session runs the machine between loading it and the first command, so anything sampled from absolute emulated time (`ay.level`, a generator phase) will not repeat: such a script declares `# @nomcp <reason>` in its header and is skipped
- **Any stderr output fails every test**: the runner treats a non-empty stderr as a problem, so the core must not print diagnostics. Real discrepancies are listed per test in `tests/results/report.txt`. On Windows without `python` on PATH use `py tests/run_tests.py`
- **A machine without a sound card is such an stderr source**: the audio driver says so and keeps going. `eCat3-headless --no-sound` (`SystemData::audio_enabled`, checked in `GenericSound::load_config`) opens no audio device at all, and the runner passes it whenever the chosen exe is the headless one — which is also the only build it finds on a machine without Qt. `sound.active` reports whether audio is really open
- **Keep test assets tiny**: every `"../results/…"` name in a script becomes a compared artifact, so do not save whole disk images from a test. Verify a write by reading it back on the machine (`DIR`, `TYPE`) plus a counter field such as `fdc.writes`. Synthetic images go in `tests/files/` next to the Python that generates them (an image shorter than the geometry is zero-padded on load)
- **The runner prefers a windowed build over the headless one**: `tests/run_tests.py` scans
  `src/cmake-build-*/eCat3.exe` first and only falls back to `eCat3-headless`, and it prints the
  executable it picked. Rebuilding one directory and recording references with another silently bakes
  the old behaviour into `expected/`; pass `--exe` when in doubt
- **The MCP server runs behind `.build/mcp_reload.py`**, and that is not decoration: an `eCat3.exe --mcp`
  started directly outlives every rebuild, so it holds the exe open (`cannot open output file eCat3.exe:
  Permission denied`) and keeps answering from the core it was launched with - plausible answers from a
  stale build. The wrapper runs the emulator from a temporary copy and restarts it whenever the built exe
  gets newer, so a rebuild is picked up by the next tool call; it says so in the answer, and the emulated
  machine has to be loaded again. `ECAT3_MCP_NO_RELOAD=1` keeps only the copy. See `MCP.md`
- **Manual `eCat3.exe` runs rewrite `deploy/ecat.ini`**; the test runner restores it, a hand-run does not. Check `git status` and `git checkout -- deploy/ecat.ini` before finishing
- CPU tests: `src/tests/*.asm` and the `slow-*` scripts that run them (6502 functional test, zexall, Orion memory test)
- Debug configurations (`debug = 1` in the `system` section) are hidden from the machine chooser unless `show_debug_versions=1` in the ini, and `src/wasm/package_machines.py` keeps them and their ROMs out of the web build entirely

### Debugging Tips

- **`globals.h` is generated**: Produced by `configure_file(globals.h.in ...)` at CMake configure time. Do NOT edit `globals.h` directly; edit `globals.h.in` or CMake variables instead.
- **A halted CPU still runs the script engine**: `Emulator::timer_proc()` calls `script->tick()` on the zero-cycle path too, so `LOG`, `COMMAND` and the other instant verbs work while `cpu.stop()` holds the processor. The delays cannot - they are counted in emulated time and that has stopped, which is what an MCP session reports as a stall instead of hanging forever.
- **A renderer's `get_screenshot()` must return raw RGBA**, `sx*sy*4` bytes: `Emulator::store_screenshot()` encodes the PNG itself and drops a buffer of any other size (with a line in `cerr`). The contract is written next to the declaration in `renderer.h`; `WasmRenderer` used to return an encoded PNG, which is why it is written down at all. Nothing in the web frontend requests a screenshot yet - there is no C API for scripts there - so `SCREEN` in the browser is correct but unreachable.
- **`boot-agat-6502-test` is flaky**, on unmodified code too (roughly one run in six). Its screen is repainted while the CPU runs NOPs across video RAM, so `SCREEN` can catch a half-updated frame. Re-run it before believing a failure.
- **No compile-time logging**: the old `LOGGER` / `LOG_*` macros and the `Logger` class are gone. To trace a device, expose its state through `get_device_fields()` / `get_field()` and read it from an `.ecat` script (`LOG dev.field`), like `bk-fdc` does with its `trace` field
- **Debug Windows**: GUI provides disassembler, memory dump, port inspector, CPU state viewer
- **Breakpoints**: Debug menu supports execution breakpoints and step modes
- **Scripts beat GUI automation**: `LOG mapper.map` prints the memory and port map in the order ranges are matched (with `[off]` on ranges disabled by `cancelinit`) — usually the fastest way to see that a window or page is wired to the wrong device. `LOG cpu.registers`, `LOG ram0.value(from,to)` and `SCREEN` cover most of the rest

## Project Structure

```
eCat3/
├── src/
│   ├── emulator/           # Core emulation engine
│   │   ├── devices/        # Device implementations
│   │   │   ├── cpu/        # CPU cores (i8080, Z80, 6502, 65C02, К1801ВМ1)
│   │   │   ├── common/     # Generic peripherals
│   │   │   └── specific/   # Machine-specific devices
│   │   ├── audio/          # Audio driver abstraction (miniaudio, SDL2)
│   │   ├── script/         # .ecat script parser, engine and log
│   │   ├── core.h/cpp      # Base device class & interface manager
│   │   ├── config.h/cpp    # Config file parsing
│   │   ├── emulator.h/cpp  # Main orchestrator
│   │   ├── disasm*.h/cpp   # Disassemblers (table-driven + PDP-11)
│   │   ├── result.h        # Error handling (replaces QMessageBox in core)
│   │   └── renderer.h      # Rendering abstraction
│   ├── dialogs/            # UI debug windows (debugger, disassembler, etc.)
│   ├── renderers/          # Rendering implementations (Qt, SDL2, OpenGL)
│   ├── mainwindow.h/cpp    # Main UI window
│   ├── qt_utils.h/cpp      # Qt-dependent utilities (moved out of emulator core)
│   ├── tests/              # CPU test suites (i8080, Z80, 6502)
│   ├── headless/           # Console frontend (no Qt) + null renderer
│   ├── mcp/                # MCP server: JSON-RPC, session, base64 (Qt-free)
│   ├── wasm/               # WebAssembly frontend + package_machines.py
│   ├── libs/               # Third-party (dsk_tools, lodepng, md4c, miniaudio, mfm_tools, crc16, audio_filters)
│   └── CMakeLists.txt
├── deploy/
│   ├── computers/          # Computer configuration files (.cfg)
│   ├── scripts/            # .ecat automation scripts (demo.ecat)
│   └── software/           # Default software/disk image directory
├── nuvola/                 # Nuvola icon theme
├── screenshots/            # Screenshot images for docs
├── docs/                   # Hardware documentation & references
├── .build/                 # Build scripts and CI config
├── BUILD.md                # Platform-specific build instructions
├── CONFIG.md               # Configuration file format reference
├── SCRIPTING.md            # .ecat script and command line reference (Russian)
├── MCP.md                  # External control over stdin/stdout (Russian)
├── LINKS.md                # Reference links
├── MANUAL.md               # User manual (Russian)
└── README.md               # Project overview

```

## Important Implementation Details

### Interface System

Interfaces are declared as `Interface` member variables in device classes. The `Interface` constructor auto-registers with `InterfaceManager`:
```cpp
Interface(ComputerDevice *device, InterfaceManager *im,
          unsigned int size, const std::string &name, unsigned int mode,
          unsigned int callback_id = 0)
```
The `callback_id` is the key reactive mechanism: when a connected interface changes value, `device->interface_callback(callback_id, new_value, old_value)` fires. A `callback_id` of 0 means no callback.

Interface wiring happens in `ComputerDevice::load_config()`, not constructors. Config parameters prefixed with `~` are parsed as connections. The `!` prefix inverts the signal. Bit-range syntax `[lo-hi]` handles width differences via mask/shift.

Propagation is **synchronous**: calling `interface.change(value)` immediately cascades through all linked interfaces and triggers callbacks. No event queue.

Config example:
```
memory_mapper: page-mapper {
  ~address = cpu.address
  ~data_read = cpu.data_read
}
ram1: ram {
  size = 64k
  ~address[0-15] = memory_mapper.address[0-15]
  ~data = memory_mapper.data
}
```

### Two-Phase Device Initialization

1. **Constructor**: Creates `Interface` members (auto-registered), sets defaults. Must NOT reference other devices (they may not exist yet).
2. **`load_config(SystemData *sd)`**: Base class processes `~` interface connections. Subclasses load ROM images, resolve cross-device references by name, call `init_sound()`, etc.

### Key Constants and Sentinel Values

- **`_FFFF`** (`(unsigned int)(-1)` from `utils.h`): Represents "all bits set / no device responded / high-impedance". Returned by `MemoryMapper::read()` on miss; initial value of all interfaces; broadcast by `MODE_OFF` interfaces.
- **Fixed-size limits**: `MAX_DEVICES = 100`, `MAX_INTERFACES = 200`, `MAX_LINKS = 100` per interface.

### CPU Execution Model

Emulation runs in a dedicated high-priority thread (not QTimer). `Emulator::run()` spawns two raw threads: `emulationThread` (TIME_CRITICAL priority on Windows, SCHED_FIFO on POSIX) and `renderThread` (50fps target). The emulation loop polls wall-clock time with microsecond resolution, converts elapsed time to CPU cycles, and calls `cpu->execute()` which returns cycle count. `DeviceManager::clock()` then clocks all non-CPU devices (starting at index 1, since index 0 is the CPU).

### Memory Access

CPU holds a direct pointer `mm` to `MemoryMapper` (set in `CPU::load_config()`). All memory/port access goes through this pointer, **not** through the interface system:
- `read_mem(addr)` → `mm->read(addr)` → `MemoryMapper::map()` finds matching `AddressableDevice` → `device->get_value(addr)`
- `write_mem(addr, val)` → `mm->write(addr, val)` → `device->set_value(addr, val, false)`
- Port I/O: `read_port()`/`write_port()` → `mm->read_port()`/`mm->write_port()` (same dispatch via separate port map)

`MemoryMapper::map()` does linear scan of ranges checking: config mask match, address in range, address mask filter, and read/write mode. Returns `_FFFF` (all bits set) when no range matches.

**Word access**: CPUs with a 16-bit bus (the К1801ВМ1 family) use `mm->read_word()` / `write_word()`, which map once and call `AddressableDevice::get_value_word()` / `set_value_word()`. The default implementation composes the word from two little-endian byte accesses, so byte-oriented devices need no changes; `Port` and `MemoryMapper` override it to stay atomic. A `port` with `size > 8` is a true two-byte register: byte accesses pick a lane by address bit 0, word accesses see the whole thing.

### Display Rendering

Display devices accumulate pixel data into buffer. Rendering happens on the render thread (typically 50fps). Multiple render backends supported via interface abstraction.

## Configuration File Format (Quick Reference)

See `CONFIG.md` for complete spec. Key points:
- `device_id: device_type { ... }` declares device
- `~interface_name = other_device.interface_name` connects devices
- `parameter = value` sets device parameters
- Hex prefix: `$` (e.g., `$FF`), Octal: `&` (e.g., `&177714`), Binary: `#` (e.g., `#1010`), Multiplier: `k` (e.g., `64k`). The same prefixes `format_number()` writes, so a value from a `LOG` line can be typed back in. Octal is what every К1801ВМ1 (БК) document uses; note that the `#` of a PDP-11 listing means an immediate operand, not binary
- **`radix` in the `system` section sets the base of every unprefixed number of that machine** (`radix = 8` in every BK config), and `_` marks the decimals — `clock = _4000000` next to `start_address = 140000`. It is ambient state: `set_default_radix()` (`utils.cpp`) is called by `Emulator::load_config()` from `EmulatorConfigDevice::radix`, and by the script verb `RADIX`. `parse_numeric_value(s)` therefore reads it everywhere; pass `10` explicitly for a number that is **not** the machine's — ini settings, tape format strings, the base of `RADIX` itself, and interface bit ranges in `convert_range()`. Converting a machine to a non-decimal radix means marking every count, size, frequency and delay in its configs **and its scripts** with `_`; the regression suite is what catches a missed one
- **The arguments of a script verb are read in the machine's radix too**, which is easiest to forget when typing commands by hand into an MCP session rather than into a `.ecat` file. On a БК `LOGDEFS 16,8` fails outright (`Invalid numeric value: 8` — an octal number has no digit 8), and `WAIT 1000` silently waits 512 ms; write `LOGDEFS _16,_8` and `WAIT _1000`. The base of `LOGDEFS` is itself such a number, so the octal output of a БК is asked for with `_8`

## Non-Obvious Design Patterns

- **`DeviceManager::clock()` starts at index 1**: Index 0 is the CPU (clocked separately via `cpu->execute()`). Clocking it again would double-count cycles.
- **`clock` parameter has dual meaning**: For the `"cpu"` device, it's the absolute frequency in Hz. For all other devices, `clock = 1/2` sets a multiplier/divider ratio relative to the system clock. Parsed in different code paths.
- **Port access strobe**: `Port::get_value()`/`set_value()` toggle `i_access` (0→1 pulse) on every access. Devices connected to `port_name.access` receive this as a trigger. Used for keyboard reset, sound toggle, etc.
- **`PortAddress` stores address, not data**: `PortAddress::set_value(addr, val)` drives `i_data` with the *address*, not the value. Models Apple II / Agat soft-switches where the accessed address encodes the command.
- **`force` parameter**: `set_value(addr, val, force=true)` updates internal state without driving bus interfaces. Used to initialize registers without triggering connected devices.
- **ROM has `auto_output = true`, RAM does not**: ROM responds to address bus changes by outputting data on `i_data` automatically (combinatorial lookup). RAM requires explicit `get_value()` calls through the mapper.
- **Sound uses `AudioDriver` abstraction**: `GenericSound` uses a pluggable `AudioDriver` (`emulator/audio/audio_driver.h`). Default backend is **miniaudio** (`audio_driver_miniaudio`); SDL2 audio is optional via `USE_SDL_AUDIO` CMake flag. Sound devices inherit from `GenericSound` and override `calc_sound_value()`.
- **`cancelinit`**: MemoryMapper feature where range index 0 (marked `[*]` in config) is active only after reset. Flips off when CPU reads an address matching the cancelinit mask. Models Agat boot-ROM-only-at-reset behavior.
- **`portstomemory = 1`**: Routes port I/O to the memory address map instead of the port map. Used by Agat-9 (6502 has no separate I/O space).
- **Device lookup is O(N)**: `DeviceManager::get_device_by_name()` does linear scan. Use `required=false` for optional devices to get `nullptr` instead of exception.
- **Bus timeout is a feature, not an error**: `k1801vm1` aborts the current instruction and traps through vector 004 when an address matches no range. The БК0011М ROM probes for installed memory blocks exactly this way, and without the trap it never starts БЕЙСИК. `MemoryMapper::responds()` answers whether anything is mapped.
- **`-N` entries in `get_registers()` / `get_flags()`**: pairs whose name starts with a dash carry no value — they are blank separators between register groups, drawn (skipped) by `KeyValueArea::paintEvent` and filtered out by the script engine's `pairs_to_string`. Anything else consuming those lists must skip them too.
- **Two source lists**: `src/CMakeLists.txt` and `src/wasm/CMakeLists.txt` are separate and hand-maintained. A file added only to the first builds fine on the desktop and breaks the web build. Inside `src/CMakeLists.txt` the list is split into `EMULATOR_SOURCES` (Qt-free, shared by the windowed and the console targets) and `GUI_SOURCES`; a core file belongs in the first, or the headless build loses it.
- **`Emulator`'s constructor takes work, *data*, software, ini** - not the order the members are declared in. Swapping the middle two is silent until a machine whose images live under `software/` reports "Disk image file not found".
- **A machine may carry several disk controllers of different types**: `Irisha-kngmd.cfg` has `fdc : wd1793` (5.25", drives A:/B:) and `fdc2 : gmd70` at ports 50-51 (8", drives E:/F:). Nothing is special-cased by name: the GUI finds drives by class `fdd`, the scripts by device name (`fdd2.track`). Only `cpu`, `mapper`, `display` and `keyboard` are reserved names.
- **Wait states are modelled by running the device forward, not by stalling the CPU**: `wd1793` `sync = <address mask>` marks a data-register copy whose access (Irisha KNGMD port 37) advances the controller state machine until DRQ/INTRQ before returning. Use the same trick for any device whose READY line holds the CPU.
- **`fdd` `layout` picks the track order by file extension** (`cpm:sides|dsk:cylinders`); `save` to an extension with the other order reshuffles tracks on the way out, so the drive doubles as an image converter. Only the sector (`logical`) path honours it; MFM/whole-track paths index `track_indexes[track*sides+side]` directly.
- **Some files are committed with CRLF** (`git ls-files --eol` shows `i/crlf`: `i8255.cpp`, `6502.*`, `i8080.*`, `z80.*`, `globals.h.in`). A script that rewrites such a file with LF turns the diff into the whole file; preserve the existing line endings.

## Language and Compiler Constraints

- **C++11 baseline** (`CMAKE_CXX_STANDARD 11`): The emulator core targets C++11 for MinGW 4.9.2 (Windows XP) compatibility. Modern compilers (MSVC, MinGW 13+) build with C++17 features enabled selectively (e.g., `USE_MINI_INI` requires `<filesystem>`).
- **Threading**: `thread_compat.h` provides `EmuThread` and `compat_mutex` — use these instead of raw `std::thread`/`std::mutex`. On MinGW < 5, these map to QThread/QMutex; otherwise to std equivalents. CMake options `FORCE_QT_THREADING` / `FORCE_STD_THREADING` override auto-detection.
- **Qt-free core boundary**: Code under `src/emulator/` must not include Qt headers directly. The only exception is `thread_compat.h`, which uses Qt threading behind `#if USE_QT_THREADING`. Qt-dependent utilities live in `src/qt_utils.h/cpp` (outside the core).
- **Keyboard codes**: `EmuKey` namespace in `keyboard.h` defines key constants with values matching `Qt::Key`, so Qt frontends pass `event->key()` directly. Non-Qt frontends (Wasm, SDL) use the same `EmuKey::` constants.

## Dependencies

- **Qt** 5.15.2 (Windows 7) or 6.8+ (modern platforms)
- **SDL2** (optional: video rendering via `RENDERER_SDL2`, audio via `USE_SDL_AUDIO`)
- **miniaudio** (bundled, default audio backend)
- **CMake** 3.21+, **Ninja** or **Make**
- **Compiler**: MinGW 8.1 (Windows 7 i386), MinGW 13+ (Windows x86_64), GCC 9.4+ (Linux), Clang (macOS)

## Git Workflow

- **Main branch**: `master` (stable releases)
- Feature branches per machine/subsystem (e.g., `agat-9`, `irisha`)
- Tag releases with version numbers (e.g., `v3.5.0`)

See `HISTORY.md` for changelog.