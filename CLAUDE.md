# CLAUDE.md

Guidance for Claude Code in this repository. The details live in `docs/dev/` (links below); this file keeps
the rules whose violation breaks something silently. When a fact changes, update the `docs/dev/` file, and
this file only if a rule changes.

## Project

**eCat3** is a universal emulator of retro computers (Орион-128, Радио-86РК and family, Агат-7/9, Ириша,
БК0010/0011М with СМК-512 and А16М, УК-НЦ, ДВК-1..4, Поиск-1, Юниор, Арго) in C++. The core
(`src/emulator/`) is **Qt-free**, using only the standard library, so it also builds for the console
(`src/headless/`) and WebAssembly (`src/wasm/`). Qt is only the windowed GUI (`src/mainwindow.*`,
`src/dialogs/`, `src/renderers/`). Targets: Windows 7+ (XP kit too), macOS, Linux.

Machines are **configuration, not code**: `deploy/computers/**/*.cfg` declares devices (CPU, mapper, RAM,
ROM, peripherals) and wires their interfaces (`~line = dev.line`, `!` inverts, `[lo-hi]` picks bits).
Extensions (`.ext`, `.ext.zip`) are variants written as edits to a base. Snapshots (`.ecats`) are opened like
any machine file. Syntax: `docs/CONFIG.md`.

## Where things are documented

| Topic | File |
|---|---|
| Device architecture, interfaces, emulation loop, clock domains, speed, mapper and page cache, radix, keyboards, adding a CPU/device/machine, build structure | `docs/dev/core.md` |
| Configuration extensions, the editor, saved state | `docs/dev/extensions-and-state.md` |
| К1801ВМ1/ВМ2/ВМ3, УК-НЦ memory and 8088 bus timing | `docs/dev/cpu-timing.md` (and `tools/*-timing/README.md`) |
| Per-machine notes: ДВК, Поиск-1, БК, Агат, УК-НЦ, Орион/РК, ВГ75, Ириша; HFE and Агат 140 KB | `docs/dev/machines.md` |
| Window, web page, video recording, GUI record/replay, MCP server | `docs/dev/frontends.md` |
| Scripting gotchas, regression suite, debugging techniques | `docs/dev/testing-and-debugging.md` |
| Config syntax / script language / MCP / build / tests | `docs/CONFIG.md`, `docs/SCRIPTING.md`, `docs/MCP.md`, `docs/BUILD.md`, `tests/README.md` |

Read the matching `docs/dev/` file before changing a subsystem it covers: most of what is there was learned
from a regression.

## Building

- Build instructions, dependencies and release scripts: `docs/BUILD.md`, `.build/README.md`.
- Run from `deploy/` as the working directory (configs, ROMs, `data/` are there).
- Options in `src/CMakeLists.txt`: `ENABLE_GUI` (ON; OFF means no Qt at all), `ENABLE_HEADLESS` (console
  `eCat3-headless`, C++17 toolchain), `ENABLE_MCP`. A windowed build needs exactly one of `RENDERER_OPENGL`
  (Qt 6 only), `RENDERER_SDL2`, `RENDERER_QT`; Debug defaults to OpenGL on Qt 6, SDL2 on Qt 5.
- `src/cmake-build-mcp` is headless + MCP (`-DENABLE_GUI=OFF -DENABLE_HEADLESS=ON -DENABLE_MCP=ON`).
- **One core source list**: `src/emulator_sources.cmake`. A core file goes there and nowhere else;
  `GUI_SOURCES` is for files that need Qt.
- Only `dsk_tools_core` of the `src/libs/dsk_tools` submodule is built; more of the library means moving it
  into the core there, not linking the full library.
- `globals.h` is generated per build directory from `globals.h.in`; never edit it or put one in `src/`.
- Translations: `cmake --build <dir> --target update_translations` (on Windows with
  `__COMPAT_LAYER=RunAsInvoker`); Russian goes into `ru_ru.ts` by hand.

## Language constraints

- **C++11** in the core (MinGW 4.9.2 for XP). No C++14/17 there unless behind a guard.
- **No Qt headers under `src/emulator/`**, except `thread_compat.h`. Qt helpers go in `src/qt_utils.*`.
- Use `compat_mutex` from `thread_compat.h`, not raw `std::mutex`. Start threads as `emulator.cpp` does
  (`EmuThread` under `USE_QT_THREADING`, `std::thread` otherwise).
- `EmuKey` codes equal `Qt::Key`; non-Qt frontends use the same constants.
- **Some files are committed with CRLF** (`git ls-files --eol` shows `i/crlf`: `i8255.cpp`, `i8257.*`,
  `6502.*`, `i8080.*`, `z80.*`, `globals.h.in`). Keep their line endings, or the diff becomes the whole file.

## Testing

- `py tests/run_tests.py` (or `python`; the emsdk Python has no Pillow, so use `py` if it comes first on PATH). `-j N`, `-k substr`,
  `--all` adds the slow tests, `--update` re-records. Emulated time makes runs byte-identical, so
  screenshots are valid references. A behaviour change means re-recording the affected references **in the
  same commit**.
- **Any output on stderr fails every test.** The core prints no diagnostics: count them in a device field
  (`note_unsupported()`), or in `Emulator::state_report()` for state loading, never `cerr`. Machines
  without a sound card: the runner passes `--no-sound` to the headless exe. That run never enters
  `init_sound()`, so an audio setup fault shows only in the windowed build.
- **The runner prefers a windowed `eCat3.exe` over `eCat3-headless`** and prints the one it picked. Pass
  `--exe` when in doubt: recording references with a stale build bakes old behaviour into `expected/`.
- `--update -k X` re-records *every* artifact of X, including screenshots that passed within tolerance:
  `git checkout` the ones you did not mean to change.
- Tests run on `deploy/.ecat.ini` (the shipped defaults), not the developer's `deploy/ecat.ini`, which is not
  in git. A new default setting goes into `.ecat.ini`.
- Keep test assets tiny: every `"../results/…"` file is a compared artifact. Verify a disk write by reading
  it back on the machine plus a counter field, not by saving the image.
- A failure only under `-j 8` is a test or device reading host timing (a `SCREEN` at the wrong moment, a size
  applied by the render thread). Fix the cause; see `docs/dev/testing-and-debugging.md`.
- `py tests/run_mcp_tests.py --exe <MCP build>` replays the suite through MCP. The lines after `MACHINE` in
  one `ecat_run` start at the machine's first clock, so a replay matches the plain run; `# @nomcp <reason>`
  only for a script that still cannot repeat there.
- `eCat3-headless --selftest` checks extension, state, zip and timing invariants.

## Scripts (`.ecat`, `docs/SCRIPTING.md`)

- **`WAIT` counts emulated time.** A machine that fails to load or a stopped CPU hangs it: use a timeout.
- **Verb arguments use the machine's radix**: on a БК write `WAIT _1000`, `LOGDEFS _16,_8`.
- `SCREEN` waits for the render thread: take screenshots after timing-sensitive steps, or stop every
  processor around it.
- Load test machines with `MACHINE` from an `.ecat`, not through an `.ext`'s `@script` (it starts late).
- `COMMAND dev.set` writes with `force`, and the device decides what that means: an i8255 port and the
  Поиск FDC control only latch (no line moves), but DL11, 1801ВП1-033 and the УК-НЦ channels ignore it and
  act as on a guest write (`work-dvk4-printer`, `boot-uknc-channels` rely on that). Read the device first.
- On the БК hold `KEY` for `_50`: 200 ms already autorepeats in MK-DOS/ANDOS.
- Debug through scripts and MCP: `LOG mapper.map`, `LOG cpu.registers`, `LOG cpu.history(_40)` (1801
  processors, last 256 addresses), device fields. There is no compile-time logging, and none should be added.

## Core rules

**Devices**
- Two-phase init: the constructor creates `Interface` members and defaults and must not touch other devices,
  nor have side effects or leak (the extension editor constructs every device of a machine without
  `load_config`). `load_config(sd)` does the wiring, ROMs and cross-references.
- New device: factory `create_xxx()`, register in `Emulator::register_devices()`, add to
  `emulator_sources.cmake`. Expose state with `get_device_fields()`/`get_field()` and actions with
  `get_device_commands()`/`send_command()`. Add `save_state()`/`load_state()` for anything a cold start does
  not reproduce. A new CPU also names its disassembler (`disasm_table()` or `create_disasm()`).
- New machine: a `.cfg` (or an `.ext` on a base) plus a matching `.md` description, no code.
  `package_machines.py` skips an extension standing on another extension, so it silently drops out of the
  web build.
- Reserved names: `cpu` (master processor and time base), `mapper`, `display`, `keyboard`. Everything else
  is found by class.
- A guest action the device does not model is counted (`note_unsupported()`), never fatal.
  `DeviceManager::error()` is only for emulator faults.
- A register whose read has side effects overrides `get_direct()` (the debugger and `LOG` read with it).
- An interface line has **no wired-AND**: a second driver overwrites the first. Combine sources in a device.
  Propagation is synchronous, with no event queue. An unplugged `connector` device must drive 0 when it
  leaves.
- The bits of an i8255 upper half `CH` are numbered differently by direction. Driven into the port (input),
  by their place in the whole port: `~CH[4]` is PC4, and `~CH[0]` silently drives nothing. Taken from it
  (output), within the half: `dev.CH[3]` is PC7 (`~strobe = !pp-printer.CH[3]` of the УК-НЦ is right).
- A one-shot interrupt request (a 1801 vectored interrupt) is cleared by the acknowledge (`~iako`), not
  recomputed from the level.
- Devices are clocked in declaration order; a device that integrates another's fast state must follow it in
  the config. `DeviceManager::clock()` skips processors by class.
- `clock` is Hz for a processor and a multiplier/divider ratio for any other device.

**Threads**
- Devices belong to the emulation thread. A window changes a device only through `e->invoke(fn)`. `fn`
  must not wait for the caller. The debugger reads memory with `peek_mem()`/`get_direct()`, never a bus
  cycle.
- Host key events arrive on the GUI thread. A keyboard that drives an interrupt queues codes and hands them
  over in `clock()` (`UKNCKeyboard`).
- `MainWindow::e` is null after `closeEvent()`; event handlers check it.

**Emulation loop** (`docs/dev/core.md`)
- **The loop spins (`yield()`), and catch-up is not capped.** Both are deliberate. A sleep costs audio
  quality, and a cap breaks parallel test runs. Never wait for something short with `Sleep`/`sleep_for` on
  Windows (it may take 15.6 ms). Use an event.
- Speed scales only the wall time of a slice; never raise a processor's `clock` to speed a machine up.
- A display either calls `frame_complete()` and answers `own_frames()` true, or the emulator ticks it at
  50 Hz. A field read by a script must not come from the render thread.
- A display whose beam draws lines on the emulation thread sets `screen_valid` when a line lands on the
  surface; otherwise `render_all()` on the render thread wipes the frame (a black or one-palette `SCREEN`
  under `-j 8`). A screenshot is stored only by a pass that began with the request pending.
- Clock domains: one per processor, positions are 64-bit LCM-scaled. With one processor the arithmetic is
  the old loop, so the whole suite must pass unchanged after touching it.

**Mapper**
- The page cache keys on the config value. Anything that changes `ranges[]` or `first_range` calls
  `invalidate_pages()`.
- A `routed = 1` device's `route()` is side-effect free, uniform within a 512-byte page, and calls
  `routing_changed()` when its answers change.
- A write reaches only the first matching range unless `through = 1`. A bus timeout on a 1801 is a feature
  (trap through 004).

**Numbers**
- `radix = 8` (every БК config) makes unprefixed numbers octal: mark decimals `_123` in configs **and**
  scripts. Prefixes: `$` hex, `&` octal, `#` binary, `k` multiplier.
- `parse_numeric_value()` reads the machine radix; pass `10` for numbers that are not the machine's (ini,
  tape formats, bit ranges).

**Saved state** (`docs/dev/extensions-and-state.md`)
- `load_state()` must not drive a line. Work that needs every device restored goes into `state_restored()`.
  It must also re-derive anything the device cached from a line, since `Interface::restore()` fires no
  callback.
- Media are state (written live into the bundle); host input and breakpoints are never restored. A `.ecats`
  carries its own `@config` (a `.cfg` fix never reaches an old snapshot; patch the `@config` in a copy to
  test one) and cannot carry `@script`.
- Clear `CPU::reset_mode` in `load_state()`.

**Extensions** (`docs/dev/extensions-and-state.md`)
- Every machine file goes through `load_machine_description()`. A missing `X.cfg` with an `X.ext` beside it
  means `X.ext` (`cfg_or_ext()`), so old names keep working.
- A `.cfg` rewritten as an extension is checked with `eCat3-headless --print-config` before and after.
- `-device` removes a device without following its references (mapper ranges, links, `mix`); remove those
  too. A key (`name + left_range`) is not unique in a base: remove by value, and never replace an
  ambiguous key.
- `ConfigReader::next()` drops the last character of a text without a final line break.

**Frontend gotchas** (`docs/dev/frontends.md`)
- A renderer's `get_screenshot()` returns raw RGBA `sx*sy*4`. `VideoRenderer::fill()` under OpenGL detaches
  the surface: write `render_pixels` directly.
- Every copy of the ini writes the whole file; save through `e->write_setup()`.
- The MCP server runs behind `.build/mcp_reload.py` (restarts on rebuild). A directly started `--mcp` exe
  holds the binary and answers from a stale core.
- `Emulator`'s constructor takes work, *data*, software, ini, in that order. Swapping the middle two is
  silent.

## Bus timing in one paragraph

БК machines use `timing = vm1`, the КЦГД and УК-НЦ `vm2`, the ДВК-4 `vm3`, and the ДВК-3 `legacy`. The tables
`vm*_timing_table.inc` are **generated**: edit `tools/bk-timing` (ВМ1), `tools/vm2-timing`, `tools/vm3-timing`,
not the `.inc`. A memory answers through the `bus-timing`/`WaitSource` device its `bus_timing` (or `wait`)
parameter names. Every device the processor can see answering must name one, and on the БК0011М that
includes the `page-mapper` windows, since `MemoryMapper::last_device` is the window. `tests/scripts/timing-*` and `--selftest` hold the
measured numbers of real machines. Read `docs/dev/cpu-timing.md` before touching any of it.

## Git

- Branches: `master` (releases), `dev` (integration, PRs from forks), feature branches per machine or
  subsystem. Tags are versions (`v3.5.0`). Changelog: `docs/HISTORY.md` (Russian).
- Do not commit unless asked; leave changes in the working tree.
