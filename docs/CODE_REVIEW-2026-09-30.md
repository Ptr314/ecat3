# eCat3: ревью всего проекта, 2026-09-30

Ветка `dev`, HEAD `950d74c`. Шесть независимых проходов только на чтение: ничего не собиралось и не
запускалось. Ниже сводка и затем полные отчёты проходов в том виде, в каком они были получены
(часть на английском, часть на русском).

Пометки в отчётах: **CONFIRMED** - проход проследил путь по коду, **PLAUSIBLE** - механизм есть,
исход требует прогона. Это оценки самих проходов. Что из этого перепроверено отдельно, сказано в
сводке.

## Сводка

Архитектура ядра здравая и принцип «машина в конфигурации» в основном выдержан. Дефекты сидят в
четырёх местах: на границе потоков GUI и эмуляции, в доверии к чужим файлам, в нескольких индексах
без проверки и в отсутствии повторного использования у самих `.cfg`.

### Перепроверено по коду после проходов

- `agat_fdc140.cpp:87,94,150,221`, `agat_fdc840.cpp:211` - `drives[selected_drive]` без проверки,
  в пяти конфигурациях Агата один привод.
- `i8253.cpp:206-224` - управляющее слово с номером канала 3 пишет за массивы на три элемента.
- `pdp11core.cpp:1110` - DIV: деление раньше проверки диапазона, `-2^31 / -1`.
- `i8080.cpp:130` - `save_state` пишет восемь байт, `F` лежит девятым.
- `disasmarea.h:53`, `disasmarea.cpp:179` - `lines[1000]` без проверки границы.
- `mainwindow.cpp:1886` - поиск устройства `tape` по имени с исключением.
- `mainwindow.cpp:1316-1324`, `emulator.cpp:148-168` - неудачная загрузка: висячие указатели в окне
  и утечка недостроенной машины; `emulator.cpp:487` - сброс без проверки `loaded`.
- `disasmarea.cpp:74,167`, `debugwindow.cpp:230` - чтение памяти через `cpu->read_mem()`;
  `k1801vm1.cpp:29-30` - проверка таймаута сразу после чтения.
- `fdd.cpp:743-759`, `:346` - геометрия из снимка не сверяется с размером буфера.
- `emulator.cpp:699` - `apply_state()` до `try`; `script_engine.cpp:1019` - разбор операнда
  `WAITFOR` в `waitfor_step()`; `emulator.cpp:757` - общий `catch` без сообщения.
- `utils.cpp:308-314` - `resolve_output_path()` принимает абсолютные имена и `..`.
- `utils.cpp:381` - строковая `read_confg_value()` приводит к нижнему регистру;
  `uknc_hdd.cpp:282` читает через неё `image`.
- `i8080.cpp:189` - остановленный процессор возвращает 10 тактов.
- `6502.cpp:56,62`, `core.cpp:111-120` - `i_address.change()` на каждое обращение, без сравнения
  со старым значением.
- `agat_9_display.cpp:214` - виртуальный вызов на пиксель; `:445,450` - `v & 0x3F + 0x80`.
- `emulator.cpp:498` - ленивое создание `ScriptEngine` без синхронизации.
- `emulator.cpp:794-796` - `sleep_for(20 мс - время кадра)` в потоке отрисовки (код; частота кадров
  не измерялась).
- `CMakeLists.txt:101,177` - `if(USE_MINI_INI)` проверяет незаданную переменную; флагов
  предупреждений нет.

### Опровергнуто

- Отчёт по процессорам, п. 3: ссылки на поля структур под `#pragma pack(1)`. Проверено на
  MinGW 13.1 (`-std=c++11 -fsyntax-only`): компилируется без ошибок и предупреждений. Ошибку GCC
  даёт только `__attribute__((packed))`.

### Остальное

Всё, чего нет в списке «перепроверено», взято из отчётов как есть.

---

# Проход 1. Ядро и горячие пути

Просмотрено: `src/emulator/core.h`, `core.cpp` (целиком), `emulator.h/.cpp` (целиком), `devices/common/page_mapper.*`, `thread_compat.h`, `renderer.h`, `devices/common/sound.*`, `audio/*`, плюс вызывающий код в `dialogs/disasmarea.cpp`, `dialogs/debugwindow.cpp`, `mainwindow.cpp`, `devices/cpu/k1801vm1.cpp`, `devices/cpu/6502.cpp`, `devices/common/speaker.cpp`, `ay8910.cpp` и все `deploy/computers/*.cfg` (подсчёт диапазонов и связей).

Общий вердикт: ядро спроектировано трезво, горячие пути (страничный кэш маппера, lock-free `tick()`, списки `clocked_devices`) сделаны правильно. Основные дефекты — не в архитектуре, а на границе потоков: три пути из GUI-потока лезут в состояние машины без синхронизации, и один из них (отладчик) на БК способен подсунуть работающей программе ложный таймаут магистрали.

---

**[Критично] `src/dialogs/disasmarea.cpp:74,167`, `src/emulator/core.cpp:2323-2363`, `src/emulator/devices/cpu/k1801vm1.cpp:27-31` — окно дизассемблера читает память через `cpu->read_mem()` из GUI-потока на работающей машине** — CONFIRMED (механизм), PLAUSIBLE (частота проявления)
- `DebugWindow::track()` (`debugwindow.cpp:177-190`) перерисовывает `codeview` каждые 200 мс, пока `m_debug != DEBUG_STOPPED`, то есть и в режиме `DEBUG_BRAKES`, когда процессор бежит. `DisAsmArea::update_data()`/`area_crc16()` зовут `cpu->read_mem()`, а это полноценный `MemoryMapper::read()`: пишет `no_device`, `last_device`, мемо `wm_*` (шесть несвязанных полей), заполняет `pages[]` в `fill_page()` (сначала `tag`, затем поля) и щёлкает `cancelinit`.
- Проявление 1 (самое тяжёлое): на БК `K1801VM1Core::read_word()` делает `read_mem_word()` и следом `if (bus_timeout()) m_abort = true`, где `bus_timeout()` — просто `mm->no_device`. Если между этими двумя строками GUI прочитал неотображённый адрес (дизассемблер проскроллен в 160000-177000 на БК0010 или в любую дыру), `no_device` станет `true`, и работающая программа получит ловушку через вектор 4 на исправной команде. Комментарий в `core.h:849-852` признаёт только «неверный reply на один цикл», ложный трап там не описан.
- Проявление 2: `fill_page()` пишет `e->tag = tag` до `device_r`; эмуляционный поток, увидев совпавший тег, прочитает `device_r == nullptr, timeout_r == true` (строка 1943) — `_FFFF` и таймаут на одну выборку. Проявление 3: разорванное мемо `wm_*` — `map_write()` вернёт `wm_device/wm_offset` от другого адреса, запись уйдёт не в то устройство.
- Правка: отладочные окна должны читать через `cpu->mm->get_direct(a)` (уже без побочных эффектов, без кэша и без мемо), как это делает дамп памяти. Две строки в `disasmarea.cpp` и одна в `debugwindow.cpp:230`. На 6502 это заодно убирает `i_address.change()` из GUI-потока.

**[Важно] `src/emulator/emulator.cpp:487-490`, `src/mainwindow.cpp:1203,1210`, `emulator.cpp:1083-1085` — сброс из GUI-потока выполняется прямо на работающей машине** — CONFIRMED
- `Emulator::reset()` → `dm->reset_devices()` без какой-либо синхронизации с эмуляционным потоком. Меню «Холодный/тёплый рестарт» и Ctrl+Break (`key_event`, `EmuKey::Cancel`) идут этим путём; скриптовый `RESET` (`script_engine.cpp:640`) — на эмуляционном потоке и безопасен.
- Что происходит: `RAM::reset(true)` делает `memset`/случайное заполнение 64К+ (`core.cpp:1184-1191`), пока `execute()` выполняет команду из этой памяти; `Port::reset()` → `write_register()` → каскад `Interface::change()` параллельно с каскадами от процессора — рваные значения интерфейсов; `GenericSound::reset()` → `refresh_sources()` перестраивает `m_active`, по которому в этот момент идёт цикл в `clock()`. Процессор сбрасывается лишь на следующем `execute()` (`reset_mode`), так что до целого слайса (≥1 мс, тысячи команд) он исполняет мусор поверх уже сброшенных устройств — с `fill = random` это случайные записи в порты КНГМД.
- Правка: флаг-запрос по образцу `m_state_requested`: `Emulator::reset()` из GUI кладёт `m_reset_requested/m_reset_cold` под мьютекс, а `timer_proc()` перед `store_state()` его потребляет. Скриптовый путь может звать `dm->reset_devices()` напрямую, как сейчас.

**[Важно] `src/emulator/emulator.cpp:662,669,916` — `get_device_by_name("display")`/`"keyboard"` с `required=true` бросают исключение, которое никто не ловит** — CONFIRMED
- `get_device_by_name()` при `required` (значение по умолчанию) делает `throw std::runtime_error` (`core.cpp:293`). В `run()` вызов стоит в лямбде потока вне блока `try` (тот охватывает только `timer_proc()`), в `init_video()` — на GUI-потоке в Qt-слоте. Проверки `if (!display)` сразу за ними мертвы: до них не доходит.
- Проявление: конфигурация без устройства `display` или `keyboard` (пользовательский `.ext` с `-keyboard`, тестовый `.cfg`) проходит `load_config()` без ошибки, а на `run()` процесс завершается через `std::terminate` без сообщения.
- Правка: `get_device_by_name("display", false)` и `get_device_by_name("keyboard", false)` в трёх местах; проверки уже написаны.

**[Важно] `src/emulator/emulator.cpp:146-169` — после неудачной загрузки `dm`/`im` предыдущей попытки утекают вместе с открытым звуковым устройством** — CONFIRMED
- `delete dm; delete im;` стоят под `if (loaded)`. Если `load_config()` вернул ошибку на полпути (нет ПЗУ, опечатка в `.ext`), `loaded == false`, а `dm` уже содержит созданные и частично сконфигурированные устройства. Следующий `load_config()` делает `dm = new DeviceManager()`, старый никем не удаляется.
- Проявление: у машин, где `sound` объявлен раньше упавшего устройства, `GenericSound::init_sound()` уже вызвал `ma_device_start()`; поток miniaudio и его колбэк живут до выхода из процесса, каждая неудачная загрузка (например, перебор `.ext` в редакторе конфигураций или MCP-сессия) добавляет ещё один поток с открытым аудиоустройством. Плюс `cd` утёкших устройств указывают в `m_config`, которую следующий `load_machine_description()` перезапишет.
- Правка: убрать `if (loaded)` вокруг `delete dm; delete im;` (оба инициализированы `nullptr` в конструкторе, `delete nullptr` законен), а `m_config.free_devices()` звать всегда.

**[Важно] `src/emulator/emulator.cpp:487-490`, `src/emulator/core.cpp:205-233,308-326` — «Холодный рестарт» после неудачной загрузки разыменовывает null** — CONFIRMED (путь), PLAUSIBLE (сценарий)
- `add_device()` увеличивает `device_count` (строка 215) до проверки `create_func`; при неизвестном типе устройства слот остаётся с пустым `unique_ptr`. `Emulator::reset()` не проверяет `loaded`, `on_action_Cold_restart_triggered` (`mainwindow.cpp:1201`) — тоже, в отличие от соседних действий с `if (!e->loaded) return;`. `reset_devices()` делает `devices[...].device->reset_priority` → обращение по nullptr. То же, если машина ни разу не загружалась: `dm == nullptr`.
- Правка: `if (!loaded || dm == nullptr) return;` в начале `Emulator::reset()`; в `add_device()` увеличивать `device_count` только после успешного `create_func`.

**[Важно] `src/emulator/devices/cpu/6502.cpp:56,62`, `src/emulator/core.cpp:111-140,1009-1013` — 6502 дёргает `i_address.change()` на каждое обращение к памяти; на Агат-7 это каскад до ПЗУ с `auto_output`** — CONFIRMED (механизм), стоимость — оценка
- Единственный процессор, который так делает (i8080/Z80/ВМ1 — нет). `Agat-7*.cfg:109` вешает на `cpu.address[12-13]` `rom-card-mapper : rom`, поэтому каждое обращение (~3 на команду, ~1 МГц шины) — это `change()` → `changed()` → виртуальный `Memory::interface_callback()` → `create_mask()` → `i_data.change()` → ещё один `changed()` у потребителя. `Interface::changed()` не сравнивает новое значение со старым, так что каскад идёт и когда биты A12-A13 не менялись (подавляющее большинство обращений). На Agat-9 связей нет, стоимость — только пустой цикл.
- Правка (поведение не меняется, потребитель `~page` без колбэка читает только значение): в `mos6502::read_mem/write_mem` звать `change()` только при изменении подключённых битов:
  ```cpp
  if (((address ^ m_last_address) & i_address.linked_bits) != 0) i_address.change(address);
  m_last_address = address;
  ```
  Проверить `work-agat-7*` в наборе; скриншоты не должны сдвинуться.

**[Важно] `src/emulator/emulator.cpp:494-506`, `src/mainwindow.cpp:1295` — `ScriptEngine` создаётся лениво из GUI-потока, пока эмуляционный поток читает `script` на каждой команде** — CONFIRMED (гонка), PLAUSIBLE (последствие)
- `script_engine()` делает `script.reset(new ScriptEngine(this))` без синхронизации. `mainwindow.cpp:1295` зовёт `e->script_recorder()->invalidate()` **до** `e->stop_emulation()` (1298): при загрузке второй машины в сессии, где запись/воспроизведение не трогали, движок конструируется параллельно с `if (script) script->tick()` в `timer_proc()`. Тот же паттерн в `mainwindow.cpp:2069` (таймер метки записи).
- На x86 практически безвредно (порядок записей сохраняется), на arm64 (macOS universal) читатель может увидеть ненулевой указатель до записей конструктора и прочитать `m_state`/взять несконструированный мьютекс. Окно — наносекунды, один раз за сессию.
- Правка: создать `script`/`recorder` в конструкторе `Emulator` (дёшево) и убрать ленивую инициализацию, либо в `mainwindow.cpp` перенести `invalidate()` после `stop_emulation()`.

**[Мелочь] `src/emulator/core.h:30-32,464,673,680,771,785-786`, `page_mapper.h:17` — фиксированные массивы без проверки границ** — CONFIRMED
- `Interface::connect()` (`core.cpp:65`) не проверяет `linked < MAX_LINKS`; `MemoryMapper::load_config()` пишет `ranges[++ranges_count]` и `ports[ports_count++]` (`core.cpp:2082,2167`) без проверки 100; `PageMapper` пишет `pages[page_id]` (`page_mapper.cpp:51`) при `pages[32]`; `register_device()` (`core.cpp:197`) — без проверки 100 при 62 зарегистрированных. `MAX_INTERFACES` мёртв (вектор). Сегодняшние максимумы: 45 `@memory` (УК-НЦ), 7 связей на интерфейс (`pp-sys.value`), запас есть, но переполнение `ranges` молча ляжет в `ports[0]` (массивы соседние), а не упадёт с ошибкой конфигурации.
- Побочный эффект того же массива: `Interface` весит ~4 КБ (`LinkData[100]` × 40 байт), горячие поля `mode/old_value` и `value/linked` разнесены по разные стороны массива — каждый `change()` трогает 2-3 кэш-линии вместо одной. Достаточно `std::vector<LinkData>` (заполняется в `load_config()`, в горячем пути только `data()`) плюс `return error` при выходе за 100 диапазонов.

**[Мелочь] `src/emulator/core.cpp:56-72` — `Interface::connect()` дедуплицирует по указателю приёмника, не по битовому диапазону** — CONFIRMED
- Две строки `~data[0-3] = x.y[0-3]` и `~data[4-7] = x.y[4-7]` внутри одного устройства: вторая молча отбрасывается (`linked_interfaces[i].d.i == d.i`). В текущих конфигурациях таких пар нет (проверено скриптом по всем `.cfg`), но ошибка конфигурации будет выглядеть как «половина битов не проходит» без диагностики. Сравнивать ещё и `s.mask`/`d.mask`, либо возвращать ошибку.

**[Мелочь] `src/emulator/core.cpp:328-340,441-454` — два виртуальных вызова на устройство на каждую команду в `DeviceManager::clock()`** — CONFIRMED
- `system_clock()` виртуальный (переопределён только в `Generator` и `Mouse`) и внутри зовёт виртуальный `clock()`; для устройств с `clock = 1/N` ещё целочисленное деление на каждую команду. На БК это 5-6 устройств × 2 виртуальных вызова × ~1 М команд/с. Можно при построении `clocked_devices` разложить на два списка: «1:1 без переопределения `system_clock`» → прямой `clock()`, остальные — как сейчас.

**[Мелочь] `src/emulator/audio/audio_driver_miniaudio.cpp:83` — `getObtainedBufferSamples()` возвращает запрошенный размер периода, а не полученный** — PLAUSIBLE
- `m_bufferSamples = bufferSamples`; фактический — `m_device->playback.internalPeriodSizeInFrames`. `GenericSound` от этого числа размечает буфер (`m_buffer.resize(m_samples_per_buffer*2)`, `overflow_delta`). Если бэкенд выдал период больше 4096 кадров, каждый колбэк будет недополнять и `underruns` вырастет постоянно. Проверяется `LOG sound.underruns` под разными бэкендами; правка — одна строка.

**[Мелочь] `src/emulator/emulator.cpp:444,923-925` — `std::stoul`/`std::stod`/`std::stoi` на значениях из `ecat.ini` без `try`** — CONFIRMED (код), PLAUSIBLE (срабатывание)
- Файл редактируется пользователем; нечисловое значение `[DeviceOptions]` или `[Video] scale` бросает `std::invalid_argument` из Qt-слота → `terminate`. `parse_numeric_value(s, 10)` с `try` и значением по умолчанию.

**[Мелочь] `src/emulator/emulator.cpp:788-796` — `sleep_for(20 - elapsed)` в потоке отрисовки под Windows без `timeBeginPeriod`** — PLAUSIBLE
- Гранулярность системного таймера 15,6 мс превращает 20 мс в ~31 мс → ~32 кадра/с вместо 50, если ничто в процессе не подняло разрешение таймера (Qt делает это только при активных таймерах). Проверяется счётчиком вызовов `render_screen()` за секунду в окне без открытых отладочных окон. Если подтвердится — `std::this_thread::sleep_until(next_frame)` с `steady_clock` плюс однократный `timeBeginPeriod(1)` на этот поток.

---

**Архитектурная оценка области.** Синхронное каскадирование интерфейсов — правильный выбор для эмулятора без очереди событий, но у него две неписаные обязанности, которые ядро не проверяет: (1) все `change()` должны происходить на эмуляционном потоке — нарушают отладчик и GUI-сброс, см. выше; (2) отсутствие фильтра «значение не изменилось» перекладывает ответственность на вызывающего — задокументировано в CLAUDE.md, но 6502 её не выполняет. Страничный кэш маппера сделан аккуратно: семантика `strict`/`through`/`or_read`/`routed` совпадает с линейным сканом во всех проверенных ветках (сравнил `fill_page()` с `map_read()`+`responds()` для пустой страницы, W-only нестрогого диапазона, routed с TIMEOUT/ANSWER по направлениям), мемо `wm_*` корректно исключает routed и «грязные» префиксы. Тактовые домены через НОК арифметически чисты (64-битные позиции, `norm -= target` без переполнения при нормальной работе, рабочий случай `execute() == 0`). Зарезервированных имён фактически пять, а не четыре: `Emulator::set_volume/set_muted` (`emulator.cpp:1152,1160`) ищут устройство `"sound"` — все 31 конфигурация подчиняются, но CLAUDE.md об этом не говорит; регулятор громкости у машины с динамиком под другим именем молча не работает. Поиск по имени и строковые `get_field`/`send_command` — только на инициализации и в скриптах, в горячих путях не встречаются. Нарушений C++11 в области не нашёл; Qt-заголовков в `src/emulator/` вне `thread_compat.h` нет; `stderr` из ядра пишут только `store_screenshot()` (по делу) и звуковые драйверы при отказе устройства (задокументировано, `--no-sound`).

Проверено и вопросов нет: контракт `get_value/set_value/force` у `Port`, `PortAddress`, `Memory`, `PageMapper`; строб `i_access`; `auto_output` только у ROM; `Interface::restore()` без каскада и порядок пяти проходов `apply_state()`; ребейз `norm` при сохранении; `reset_mode` в `CPU::load_state()`; `store_state()`/`store_screenshot()` под мьютексами с серийными номерами; lock-free путь `ScriptEngine::tick()`; `GenericSound` — накопление уровня по тактам, блокировка буфера только раз на сэмпл, `handle_audio_callback()` под тем же замком, подсчёт `underruns/overflows`, запись WAV; `MiniaudioDriver` — порядок `uninit`/`delete cbd`; уничтожение `dm` до `im` в `load_config()`; виртуальные деструкторы у всех полиморфных баз; `create_mask()` — интерфейсов шириной 32 бита в дереве нет (сдвиг на 32 не достигается); `find_page()` при адресах ≥ 64К; `read_port()/write_port()` при `ports_count == 0`.

---

# Проход 2. Специфика машин против конфигурации

## Verdict

The principle holds well in the core and frontends, and less well in three places: the config language's own reuse mechanism, the quick-load code, and a handful of devices.

- **Orchestrator, mapper, CPU cores, frontends:** essentially clean. I found one `system_type ==` comparison in the whole tree (`src/emulator/files.cpp:422`).
- **Rough proportion:** `devices/specific/` is about 11.9k of about 48k core lines (25%). I judge roughly 70–75% of that irreducible (chip models, display inner loops, FDC/HDD state machines). About 15% is duplication between sibling devices. About 10–15% is tables and constants that belong in data files or config.
- **Overall:** about 85–90% of machine knowledge that could live in config or data already does.
- **Biggest violation is on the config side.** `.cfg` has no include, and `.ext` cannot add a device, so machine variants are whole-file copies. About 80% of the config text in the four multi-variant families is duplicated.

## Findings, ranked by payoff

### 1. SHOULD MOVE — `.cfg` has no reuse, so variants are full copies

`.ext` exists but cannot express the commonest variant, "base plus a board":
- It cannot add a device: `docs/CONFIG.md:208` says editing a non-existent device is an error.
- It cannot extend another extension (`docs/CONFIG.md:188`).
- New mapper ranges can only be appended at the end (`docs/CONFIG.md:207`), while order is significant.

Evidence of the cost:
- `deploy/computers/bk/BK-0011M-fdd.cfg` (462 lines) differs from `BK-0011M.cfg` by one header comment, three added devices and two mapper lines. It is a pure addition.
- `radio-86rk/Spektr001.cfg` differs from `Radio-86rk.cfg` by 6 lines; `Orion-128-Z80-zexall.cfg` from `Orion-128-Z80.cfg` by 10; `Agat-9-pp.cfg` from `Agat-9.cfg` by 33.

| Family | Non-blank lines | Distinct lines |
|---|---|---|
| bk | 2906 | 488 |
| agat | 952 | 227 |
| orion-128 | 826 | 172 |
| irisha | 667 | 252 |

- Inside one file, `BK-0011M.cfg:99-125` repeats the 8-entry `@page` table for `win0` and `win1`, and `:54-92` declares eight identical `ram` blocks.
- Only one `.ext` ships (`uknc/UKNC-basic.ext`).

Proposal:
1. Let `.ext` declare a new device (`name : type { ... }` block).
2. Give mapper lines a position (`mapper:@memory[...] = dev {before = win1}` or a `^` prefix for "insert first").
3. Allow `@extends` of an `.ext`, or add `@include file.inc` to `.cfg`.
4. Optionally add a device template (`ram0..ram7 : ram {...}`).

Every fix to a БК mapper line or comment must currently be made in up to seven files, and snapshots embed whichever copy was used.

### 2. SHOULD MOVE — `src/emulator/files.cpp` is a nest of machine knowledge in the orchestrator layer

- `:422` — `system_type == "bk"` decides what `.bin` means.
- `:42` — `find_ram()` guesses device names `ram`, `ram0`, `ram1`.
- `:243-247` — looks up a device named `bios` and compares its CRC16 with `0xA85E` ("Orion-128 M1") to choose between memory load and ROM-disk append.
- `:320-336` — devices `ram1`, `ram2`, `ram3` with hardcoded RAM-disk sizes of 48 KB and 60 KB (Орион ORDOS layout).
- `:425-428` — extension-to-loader table compiled in.

Proposal: a `loader` block in the `system` section, e.g. `load[.bin] = bk {target = mapper}`, `load[.rko] = ordos {pages = ram1:48k|ram2:60k|ram3:60k}`, `load[.rk] = rk {target = ram}`. The format parsers (`load_rk`, `load_rko`, `load_bk`) stay in C++; the device names, sizes and the per-machine choice move to config. That removes the only `if machine ==` and the CRC sniffing.

### 3. SHOULD MOVE — `raster_display.cpp` hardcodes per-machine video standards, picked by subclass

- `src/emulator/devices/common/raster_display.cpp:26-63` — three named standards (`625/50`, `vp1-037`, `uknc`), each a set of seven numbers.
- The name is not even a config parameter: `bk_display.cpp:66` and `uknc_display.cpp:68` assign `m_standart` in the constructor.
- `:76` and `:97` hardcode 312/313/624 for interlace regardless of `m_lines`.
- More raster constants in subclasses: `bk_display.cpp:52,204` (scroll latch line 23, frame pulse line 21) and `uknc_display.cpp:11-15`.

Proposal: numeric parameters on the base class (`lines`, `line_us` or `line_clocks`, `top_blank`, `bottom_blank`, `interlaced`, `sample_us`, `frame_pulse_line`). Keep the named presets as shorthand defaults. A new raster machine then needs no edit to a "common" file.

### 4. SHOULD MOVE — `agat_9_mapper.cpp` embeds two PROM dumps and absolute addresses

- `src/emulator/devices/specific/agat_9_mapper.cpp:9-26` (D03, 256 bytes) and `:28-93` (D14, 1024 bytes) are ROM contents as C arrays.
- `:208-231` hardcode `$C000`, `$D000`, `$C080-$C09F`, although `Agat-9.cfg:156-167` already maps the device in three ranges with offsets.
- `:119` infers the ROM mask from the BIOS size.

The project already has the right pattern: `argo-memory` takes `map = memcfg-rom` (`Argo.cfg:66-69`, `argo_memory.cpp:112`).

Proposal: two `rom` devices (`d3 : rom {image = a9-d3.rom}`, `d14 : rom {...}`) named by parameters, and address-relative decoding with the control register as a separate mapper entry. The PROM lookup itself is JUSTIFIED in C++ (hot path).

### 5. SHOULD MOVE — `zx_keyboard.cpp` is an Арго device under a generic name

- `src/emulator/devices/specific/zx_keyboard.cpp:41-50` — the 8×5 matrix is written in Арго key ids (`key_dop`, `key_dot`, `key_comma`).
- `:229-233` — `key_f10` and the arrows are compiled in.
- `:188-191` — the tape-control bits are compiled in.
- `set_default_matrix()` suggests an override was planned, but `load_config` (`:56-68`) reads none.

Proposal: `matrix = argo-zx.kbd` (the existing `.kbd` format), `toggle = key_f10`, `control = up:$10|down:$80|left:$20|right:$40`. The device then really is a reusable ZX half-row keyboard.

### 6. COULD MOVE — duplication between sibling specific devices

- **`unior_memory.cpp:111-154` vs `argo_memory.cpp:193-247`.** The same ВТ57 channel-0/1 block transfer, each poking `I8257` internals (`RgA`, `RgC`, `RgMode`, `RgState`). They disagree on the terminal count: Юниор leaves `0`, Арго leaves `$3FFF` "as `dma_next()` does". One of them is wrong about the chip. Move the transfer into `I8257` (`transfer(src_fn, dst_fn)`) or a `dma-mover` device.
- **`unior_memory.cpp:19,71-85`.** `block2` and the "у Арго" direction logic look like leftovers from before `argo-memory` existed. `Unior.cfg` does not mention `block2`.
- **`agat_7_display.cpp:181-200` vs `agat_9_display.cpp:211-230`.** `convert_rgba` is identical. The palette-card `load_config` (`:63-81` vs `:62-80`), blink counter, `i_50hz`/`i_500hz`/`ints_en`, the `M_256_*` defines and АЦР-32/64 rendering are copies. An `AgatDisplayBase` would remove about 250 lines. Trade-off: the two 7-pixel font paths differ by one bit shift (`k=1..7` vs `0..6`), so the merge needs care.
- **Boilerplate in every display.** `get_screen_constraints`, `set_renderer` + `FillRGB`, and the "mark valid before repaint" idiom repeat in `o128display.cpp:63-109`, `irisha_display.cpp:109-211`, `bk_display.cpp`, `uknc_display.cpp`. The colour-option enumeration is written twice per device: `bk_display.cpp:140-154` and `:325-335`; `uknc_display.cpp:159-172` and `:377-388`; the palette rebuild at `uknc_display.cpp:109-120` and `:136-146`. Derive `get_device_options()` from `get_config_fields()` in the base.

### 7. COULD MOVE — palettes as data

Compiled-in colour tables:
- `o128display.cpp:12-24`
- `irisha_display.cpp:12-72` (with a `TODO: check if it's correct` at `:61`)
- `bk_display.cpp:18-35` (16×4)
- `agat_7_display.cpp:11-16`
- `agat_common.h:8-64` (8×16, plus more below)
- `uknc_display.cpp:21-43`
- `i8275display.cpp:12-23`

The mechanism exists: `i8275-display` takes `palette = <rom>` plus `rgb = 031` (`Argo.cfg:137-140,180-185`), and `rom` accepts inline `data = {...}` (`core.cpp:1285`).

Proposal: an optional `palette = <rom>` or `colors = {...}` on the other displays, with the compiled tables as defaults. Trade-off: low performance risk (tables are converted once in `set_renderer`), modest payoff. The real win is that alternative palettes (БК gid vs BKBTL levels, Агат "experimental") become `.ext` edits instead of recompiles.

### 8. COULD MOVE — Арго's ZX Spectrum mode lives in the common `i8275display`

- `src/emulator/devices/common/i8275display.cpp:16-23` (`ZX_16Colors`), `:30-31` (`~zx`, `~border`), `:134-137`, `:190-191` (`zx_bitmap`, `zx_attr`), `:428` onward (`draw_row_zx`).
- This is one machine's hardware hack inside a device shared by РК86, Апогей, Микроша and Юниор.

Proposal: an `argo-display` subclass in `specific/` overriding the row draw. Trade-off: the snapshot and threading plumbing is shared, so a subclass needs a virtual hook in the row loop. The cost is one extra type, not performance.

### 9. COULD MOVE — board identity strings inside devices

- `smk512.cpp:73-74` (`machine == "bk11"`), with the three disable tables at `:50-52` and the branch at `:127-137`. Could be `disable = ...` masks in config. The mode table `SEGMENTS` (`:35-45`) is the board's own definition and is JUSTIFIED.
- `smk512.cpp:78` fetches `"mapper"` by literal name. It should be a `mapper =` parameter, as the CPU has.
- `smk512.cpp:10-14` — offsets `077130`/`077740` assume the device is mapped at 100000.
- `uknc_hdd.cpp:270` (`board == "bk"`) selects register-layout differences at `:146,185,591-593,657-690`. Better as named capabilities (`lanes = word|byte`, `max_cylinders`). The file and type name `uknc-hdd` for an IDE controller also used on the БК is misleading.
- `ay8910.cpp:61,333-375` — `bus = bk` bundles three things: inverted lines, word-selects/byte-writes, and the TurboSound/Gryphon chip select. `bus = word` plus `invert = 1` would cover the first two. The 2xAY decode (`:362-372`) is a real board protocol and is JUSTIFIED, but deserves a neutral name such as `select = turbosound`.
- `bk-fdc` is a К1801ВП1-128 and is reused unchanged by the УК-НЦ (`UKNC.cfg:522`). Rename to `vp1-128` (keep the old type as an alias, as was done for `unior-tape`, `emulator.cpp:1337-1340`).

### 10. COULD MOVE — tape formats and codec dispatch

- `[TapeFiles]` in `deploy/.ecat.ini:6-22` holds machine knowledge in the user's ini, keyed by `system_type` (`tape.cpp:426`, `dialogs/taperecorder.cpp:274`, `wasm/wasm_main.cpp:512`). Examples: `bk.bin="bk:9464;data"`, `unior.tap="zx:tap;data"`. A user ini that predates a machine lacks its lines. Move to the `taperecorder` device as `format[.bin] = "bk:9464;data"` and keep the ini as an override.
- `tape.cpp` switches on the `TapeEnc` enum in about a dozen places (`:79-87,192-213,325-350,388-390,470-492,678-715`). A `TapeCodec` interface (`encode`, `decoder`, `name`, `size`) would make a new format one class, not twelve edits. The codecs themselves (`tape_bk.h`, `tape_rk86.h`, `tape_uknc.h`, `tape_zx.h`, `tape_bt.h`) are JUSTIFIED: signal formats, not machines.

### 11. SHOULD MOVE (small) — undocumented reserved names and type switches

- `emulator.cpp:1152,1160` — volume and mute reach only a device named `sound`. `UKNC.cfg:363` has to say so in a comment. Use class `sound` on `GenericSound` and iterate.
- `mainwindow.cpp:1886` opens the tape window via the name `tape`, while `:822` finds the tape by class. A machine whose recorder has another name gets a button that throws or does nothing.
- `files.cpp` names as in finding 2. CLAUDE.md documents four reserved names; the code actually has at least eight (`cpu`, `mapper`, `display`, `keyboard`, `sound`, `tape`, `bios`, `ram`/`ram0-3`).
- `dialogs/debugwindow.cpp:33-48` — CPU type to disassembler switch. Give `CPU` a `virtual DisAsm* create_disasm(data_path)` or a `disasm_table()` string. CLAUDE.md already lists this as a step a new CPU needs.
- `mainwindow.cpp:309-322` — debug windows registered by type string (six CPU lines). Register by class or capability (`cpu`, `Memory`, `Port`).

### 12. COULD MOVE — registration cost

Adding a device touches six places:
- `emulator.cpp` include (`:33-87`)
- `register_all_devices` (`:1276-1340`)
- `src/CMakeLists.txt`
- `src/wasm/CMakeLists.txt` (hand-maintained, a different path prefix; the device lists match today, I diffed them)
- `docs/CONFIG.md`
- optionally the debug-window registry

Proposal:
1. One `src/emulator/sources.cmake` defining the core list relative to a variable, included by both CMake files.
2. Move the 55 includes and the list into `devices/registry.cpp`.

Static self-registration is not recommended: the linker drops unreferenced objects from static libraries, and the explicit list is what `collect_config_fields()` relies on.

### 13. COULD MOVE — small devices the config could almost express

- `argo_keyboard.cpp` (50 lines): a read port that drives its scan lines from the high byte of the I/O address. `zx_keyboard.cpp:74` does the same thing. A generic option on `port` (`~strobe_out = address[8-11]`) or an `address-latch` device would remove the type.
- `uknc_keyboard.cpp:203`: the one УК-НЦ-specific fact is the release code (bit 7 plus low 4 bits). Otherwise it is "map-keyboard in scancode mode with make/break" plus a VIRQ chain. Trade-off: `map-keyboard` is already heavily parameterised (`alt-codes`, `case-latch`, `rusmode`), and a further mode might cost more clarity than a 390-line sibling.
- `mux` used as an AND gate (`BK-0011M.cfg:45-49,355-359`, `UKNC.cfg:194-204`; 17 uses, all БК/УК-НЦ/Ириша). A `gate` device (`op = and|or|nand|xor`, N inputs) would read as the schematic does. `speaker.cpp:26-29` grew its own `~enable` AND for the Микроша for the same reason.
- `mouse.cpp:42-51`: the defaults are the БК «Марсианка» bits. Harmless, but defaults in a generic device should be neutral or required.

## JUSTIFIED in C++

- **Display inner loops:** `bk_display.cpp:301-323`, `uknc_display.cpp:288-338` (three-plane spread table, per-line display list), `agat_9_display.cpp:241-510`, `o128display.cpp:111-183`, `irisha_display.cpp:213-244`. A table-driven generic pixel decoder would cover БК, Орион, Ириша and Агат graphics but not УК-НЦ or Apple hi-res artifacts, and would put a descriptor interpreter in the hottest path outside the CPU.
- **`uknc_display` display-list walk** (`:199-276`): real controller behaviour with mid-frame state.
- **Chip and board models:** `bk_timer`, `uknc_timer`, `uknc_channels` (one-shot IRQ on `iako`), `uknc_graphics`, `bk_fdc`, `agat_fdc140/840`, `uknc_hdd`, `gmd70`, `wd1793` `sync`.
- **`uknc_sound`** depending on `UKNCTimer` (`:44-49`): integration over the timer's step log cannot be wiring.
- **`argo_memory` translate path** (`:109-136`): per-access, cached per page, PROM already external — the model to copy.
- **`smk512` `route()`**: a mode matrix with per-address answers.
- **К1801ВМ1 console addresses** (`pdp11core.cpp:394,496-499,533-539`: 0177716, 0177674/6, 0160002): chip microcode, not БК knowledge. What differs per machine is already config (`halt_vector`, `halt_sel`, `start_vector`, `reply_delay`, `timing`).
- **`fdd.cpp:72-74,265-268`** `mfm_agat_*` modes: track encodings, delegated to `dsk_tools`.
- **`vm1_timing_table.inc`**: generated chip data.

## What the project does well

- **Address decoding is entirely config.** `@memory[cfg:mask][range] = dev[offset] {mode, through, or_read, strict, routed, addr_mask}` expresses БК0011М ROM/RAM banking, УК-НЦ cartridge slots and OR-ed ROM/RAM windows with no code (`UKNC.cfg:569-643`, `BK-0011M.cfg:379-414`).
- **Glue logic is wiring.** Keyboard-ready flip-flop as `register` (`BK-0011M.cfg:346-349`), IRQ masks as `mux`, frame interrupt as `generator` one-shot (`Argo.cfg:43-48`), loopback plugs as `port` + `mux` (`UKNC.cfg:234-242,394-408`).
- **Peripherals are parameterised, not forked.** `port` (`alt_bit`, `mask`, `constant_return`), `page-mapper`, `plane-pair`, `indirect-memory {scale, width}`, `connector`, `bus-timing` (fitted numbers in config, `BK-0011M.cfg:32-38`), `dl11` (vectors, `xbuf_read`, `station`), `i8275-display` (`font_address = cba65-43210`, `rgb`, `cursor_left`).
- **Frontends discover by class:** `fdd`, `hdd`, `tape`, `joystick`, `mouse`, `connector` (`mainwindow.cpp:783-822`, `wasm_main.cpp:87,209,483,578`), and toolbar options come from `get_device_options()`. No frontend includes a `specific/` header.
- **Keyboards are data:** `.map`, `.kbd`, `.keys`, `.svg`.
- **Multi-CPU** is `clock_source` / `mapper =` in config with no machine branch in `Emulator::timer_proc`.
- **`register_all_devices()`** is one shared list used by both the emulator and the extension editor.
- **The `.cfg` comments** record why each line exists, with ROM addresses as evidence.

## Suggested order of work

1. Extend `.ext` (add device, positioned ranges, chained `@extends`) and convert the variant `.cfg` files.
2. Add the loader table in `system` and delete the machine branches in `files.cpp`.
3. Parameterise `RasterDisplay`.
4. Move the Агат-9 PROMs to `rom` devices; move the ZX matrix to a file.
5. Class-based lookup for `sound` and `tape`; `CPU::create_disasm()`.
6. Shared `sources.cmake`.
7. Opportunistic: Агат display base class, ВТ57 transfer in `I8257`, palettes as data, `TapeCodec`.

---

# Проход 3. Процессорные ядра

**Объём:** `src/emulator/devices/cpu/*` (кроме содержимого `vm1_timing_table.inc`), класс `CPU` в `src/emulator/core.h/.cpp`, `src/emulator/disasm*.cpp`; плюс точки взаимодействия — `Emulator::timer_proc()`, `StateReader`, `debugwindow.cpp`, конфиги и фикстуры состояний.

**Вердикт:** документированные пути всех четырёх ядер аккуратны (такты, флаги, DAA, EI-задержка, NMI-защёлка Z80, MOVB как DATIOB и т.д.). Реальные дефекты сосредоточены там, куда стандартные тесты не смотрят: сохранение состояния i8080, недокументированные коды 6502, флаги 3/5 у `BIT n,(IX+d)`, EIS у ВМ2, и один аварийный путь (DIV). Плюс расхождение поведения «остановленного» процессора между ядрами, которое противоречит документации проекта.

## Находки

**1. [Критично] `src/emulator/devices/cpu/pdp11core.cpp:1108-1117` — DIV: `INT32_MIN / -1` до проверки диапазона** — CONFIRMED
- `int32_t quotient = dividend / divisor;` выполняется раньше, чем проверка `quotient > 32767 || quotient < -32768`. При `R[r]:R[r|1] = 0x8000_0000` и `src = 0177777` это переполнение знакового деления: на x86 — аппаратное исключение `#DE` (процесс падает с `0xC0000094`), в wasm — trap «integer overflow», на ARM — тихо INT32_MIN. То же для `dividend % divisor` на строке 1117.
- Сценарий: УК-НЦ (ВМ2, `has_eis`), любая программа, делящая `-2^31` на `-1` (в т.ч. случайные данные в тестах памяти/CPU). Реальный ВМ2 ставит V.
- Правка: перед делением `if (dividend == INT32_MIN && divisor == -1) { set_flag(F_V, true); set_flag(F_C, false); break; }`.

**2. [Критично] `src/emulator/devices/cpu/i8080.cpp:130` — `save_state` не сохраняет F** — CONFIRMED
- `w.array("regs", c->registers.reg_array_8, 8)` — в `i8080_context.h:15-17` порядок `C,B,E,D,L,H,m,A,F`, массив `reg_array_8[8]` покрывает индексы 0-7, F — индекс 8. Отдельной строки `F` нет ни в `save_state`, ни в `load_state`. Фикстура подтверждает: `tests/files/state-rk86.ecats.zip` → `regs[0-7] = …`, `PC = …`, строки с флагами нет.
- Сценарий: снимок делается между командами; если следующая — `JNZ`/`RC`/`ADC`, после восстановления она видит флаги от `reset()` (а `i8080core::reset()` F вообще не трогает, см. п. 9). Цикл `DCR B / JNZ` продолжает не туда. Z80 (`reg_array_8[12]` включает F) и 6502 (`P`) сохраняют полностью.
- Правка: `w.u("F", c->registers.regs.F, 8)` / `r.u("F", …)`; старые снимки без ключа сохраняют текущее поведение (ключ отсутствует → значение не трогается), тесты `state-rk86`, `state-unior` перезаписывать не нужно, но новый эталон стоит снять.

**3. [Критично, если подтвердится] `i8080.cpp:144-147`, `z80.cpp:176-189`, `6502.cpp:94`, `k1801vm1.cpp:198,202-203` — неконстантные ссылки на поля `#pragma pack(1)`** — PLAUSIBLE

> **Опровергнуто проверкой:** MinGW 13.1 компилирует такой код без ошибок и предупреждений. Ошибку даёт только `__attribute__((packed))`. Текст находки оставлен для полноты.

- `StateReader::u(const char*, uint16_t&)` / `(…, uint32_t&)` (`src/emulator/state.h:177-180`) принимает ссылку, а ей передают `c->registers.regs.SP` (смещение 9 в упакованном union), `c->int_enable`, `c->IFF1…index8_inc`, `c->r16.PC` (смещение 5), `c->PSW`, `c->console_pc`. GCC на такое отвечает ошибкой компиляции `cannot bind packed field '…' to 'uint16_t&'`; Clang — предупреждением `-Waddress-of-packed-member`; MSVC молчит.
- Факты из дерева: текущая сборка — `src/cmake-build-qt-6.11.2-msvc` (30.09); последняя сборка GCC 13 — `src/cmake-build-headless-build.log` от 19.09, а `load_state` в ядрах появился коммитом `c0bcb2c` от 21.09. В каталогах MinGW объектов нет. То есть этот код компилятором GCC на этой машине ещё не проходил. Linux-AppImage и macOS собираются GCC/Clang.
- Проверка: `g++ -std=c++11 -fsyntax-only` на `z80.cpp`. Правка: читать во временную переменную нужного типа и присваивать полю, либо снять `#pragma pack(1)` с контекстов — упаковка там ничего не даёт, кроме невыровненных 16/32-битных полей.

**4. [Важно] `src/emulator/devices/cpu/i8080.cpp:188-189`, `z80.cpp:267-268` — остановленный процессор возвращает 10 тактов** — CONFIRMED
- `if (m_debug == DEBUG_STOPPED) return 10;` (с пометкой `TODO: use HALT imitation`). В `Emulator::timer_proc()` (`src/emulator/emulator.cpp:868-894`) ненулевой результат идёт по ветке `counter > 0`: `clock_counter += 10`, `dm->clock(k, 10)` — эмулируемое время и все устройства продолжают идти на полной скорости. 6502 (`6502.cpp:172`) и ВМ1 (`k1801vm1.cpp:442`) возвращают 0 и попадают в специальную «нулевую» ветку, которую описывает CLAUDE.md («clock never advances», «A stopped CPU produces no cycles»).
- Сценарий: `COMMAND cpu.stop()` затем `WAIT 1000` на РК86/Орионе — WAIT завершается, магнитофон, ВИ53, ВГ75 работают дальше при стоящем процессоре; на БК/Агате тот же скрипт зависает навсегда. Отладчик на i8080 при остановке даёт устройствам «набегать» прерывания.
- Правка: `return 0;` в обоих файлах (см. также п. 12 — это симптом дублирования).

**5. [Важно] `src/emulator/devices/cpu/6502core.cpp` — недокументированные коды NMOS 6502 в основном неверны** — CONFIRMED (по чтению)
- `__NOP` (1698-1701) декодирует режим адресации из самого байта: для однобайтовых `NOP` `1A/3A/5A/7A/DA/FA` получается `(cmd>>2)&7 = 6` → abs,Y: команда съедает два следующих байта и делает чтение памяти. Программа, содержащая `$1A`, теряет две команды.
- `__DCP` (1489): `S.w = REG_A - T.w`, где `T.w` — **адрес**, а не декрементированное значение.
- `__ISB` (1504-1506): SBC с пред-инкрементным `D.b.L`, вычитается `FLAG_C` вместо `1-C`, V по формуле полупереноса.
- `__SLO`/`__SRE`/`__RLA`/`__RRA` (1678, 1694, 1567, 1585): вторая операция берёт значение **до** сдвига (`D.b.L`), а не `S.b.L`; у RRA ещё и перенос уже новый, а слагаемое старое.
- `__ARR` (1460 + 1463): `get_operand()` читает `#imm`, затем `__ANE(0x8B)` читает ещё один байт — команда длиной 3.
- `__ANC` (1447-1448): после AND делает `ASL A` — аккумулятор сдвинут, должно быть только `C = N`.
- `__SBX` (1621-1623): `calc_flags((A&X) - v)` — C = 0 при отсутствии заёма (сравни с `doCMP`, где `+0x100`). `__SBC` 0xEB (1610-1611): флаги от `REG_A`, заём инвертирован, десятичный режим игнорируется; должен быть просто `doSBC(next_byte())`.
- Klaus functional test это не покрывает (запускается без illegal opcodes), а в регрессии 6502-тестов вообще нет (в `tests/scripts` только zexall для Z80 и cputest2 для БК). На Агате/Apple II быстрые загрузчики и защиты `LAX/SAX/DCP/ISB` встречаются.

**6. [Важно] `src/emulator/devices/cpu/z80core.cpp:392-406`, вызовы `:475`, `:1434` — флаги 3/5 у `BIT n,(IX+d)` и `BIT n,(HL)` берутся из младшего байта адреса** — CONFIRMED
- `do_bit_ind(bit, v, address)` передаёт `address` как `value35`, а `calc_z80_flags` маскирует `value35 & FLAGS_35` — биты 3 и 5 байта **L** / младшего байта `IX+d`. По спецификации (Undocumented Z80 Documented, 4.1) для `(IX+d)` они копируются из **старшего** байта `IX+d`, для `(HL)` — из старшего байта MEMPTR.
- Почему zexall слеп: `msbt = 0x0103` (`src/tests/zexall.asm:29-33`), IX = msbt-1, d = 1 → адрес 0x0103: и 0x03, и 0x01 дают нули в битах 3 и 5. Пример расхождения: `LD IX,$2800; BIT 0,(IX+0)` — реальный F имеет биты 5 и 3 установленными, эмулятор — нет.
- Правка: для DD/FD CB передавать `address >> 8`; для `(HL)` в отсутствие MEMPTR `REG_H` ближе к правде, чем `L`.

**7. [Важно] `src/emulator/devices/cpu/pdp11core.cpp:1124-1135` — ASH: V и UB на границах сдвига** — CONFIRMED
- `res = value << shift` считается в 32 битах, а `set_flag(F_V, ((value ^ res) & 0x8000))` сравнивает только начальный и конечный знак: `ASH #2` от `040000` → 0, реальный процессор ставит V (знак менялся по дороге), эмулятор — нет. У ASHC (1149) то же для бита 31.
- `shift` после `shift -= 64` лежит в `[-32, 31]`: `value >> 32` для `int32_t` — UB; на x86/wasm счётчик маскируется → `ASH #-40(8), R0` оставляет R0 как был вместо заполнения знаком. `value << 31` при ненулевом `value` — переполнение знакового в C++11.
- Только ВМ2 (УК-НЦ). Правка: считать через `int64_t`/`uint32_t`, V — как `res_16 != (value << shift)` по знаковому расширению, для отрицательного сдвига ≤ -16 (ASH) результат — заполнение знаком.

**8. [Важно, латентно] `src/emulator/devices/cpu/6502core.cpp:53-70, 1977-1998` — ядро 65C02 не доведено** — CONFIRMED
- `WDC65c02_TIMES` объявлена (с `TODO: fill values` и содержимым, идентичным NMOS) и **нигде не используется**: `execute()` берёт `MOS6502_TIMES[command]` для обоих семейств. Итог: `PHX/PHY/PLX/PLY` по 2 такта, `(zp)`-режимы по 2, `TSB/TRB zp` 3/4, `JMP (abs,X)` 5, `BRA` без +1, десятичные ADC/SBC без +1.
- `WAI` при `I=1` (1988-1992): IRQ не будит процессор (реальный 65C02 продолжает со следующей команды) — `SEI; WAI` виснет навсегда.
- `_BRA` (1840-1843) не добавляет тактов взятого перехода и пересечения страницы.
- Ни один `.cfg` в `deploy/computers` не объявляет `65c02`, поэтому это долг, а не регрессия.

**9. [Важно] `src/emulator/devices/cpu/i8080core.cpp:125-131, 188-195` — контекст i8080 не инициализируется** — CONFIRMED (неинициализировано), эффект PLAUSIBLE
- Конструктор и `reset()` задают только `PC`, `halted`, `int_enable`, `ei_delay`; `A,B,C,D,E,H,L,F,SP` — indeterminate из кучи (объект создаётся `new` после того, как разбор конфига уже поработал с той же кучей). `z80core` (`memset`, :156) и `mos6502core` (:101) обнуляют.
- Сценарий: `PUSH PSW` до первой арифметической команды кладёт мусор с битами 3/5; `LOG cpu.registers` сразу после `MACHINE` недетерминирован между запусками; в сочетании с п. 2 после восстановления снимка F = мусор.
- Правка: `memset(&context, 0, sizeof(context)); context.registers.regs.F = F_BASE_8080;` в конструкторе (не в `reset()` — реальный ВМ80 регистры при сбросе не чистит).

**10. [Важно] Гонки между потоком GUI/скрипта и ядром** — PLAUSIBLE
- `src/dialogs/debugwindow.cpp:112-113, 177-190`: `track()` раз в 200 мс на потоке GUI читает `get_registers()`/`get_flags()` у работающего ядра — несинхронизированное чтение упакованных 16-битных полей; `:204` пишет PC (`set_context_value`) с GUI-потока; `:143, 173, 194` пишут `m_debug` (обычный `unsigned int`) — формально data race, практически рваные значения на экране.
- Существеннее: `get_command()` у всех ядер идёт **через шину**: у ВМ1 `pdp11core::get_command()` → `K1801VM1Core::read_word()` (`k1801vm1.cpp:27-33`) — ставит `m_abort` при тайм-ауте и делает `Vm1BusTiming::record()`. Следствие: `LOG cpu.command` в скрипте (выполняется между командами) вписывает фантомное обращение в цепочку `timing = vm1`, и `finish()` следующей команды сопоставляет шаги с лишней записью — одна команда получает чужое время. Из GUI ещё и `mm->no_device` затирается. Правка: для `get_command()` читать через `mm->get_direct()`/`peek`-путь без `record()` и без `m_abort`.

**11. [Мелочь] Языковой UB без практического эффекта** — CONFIRMED
- `z80core.cpp:1229`: `(~T.b.L) << 8` — сдвиг влево отрицательного `int` (UB до C++20).
- `z80core.cpp:655, 673, 691, 710, 771`: `D.dw` читается, когда записан только `D.w` (верхняя половина union indeterminate; результат маскируется приведением к `uint16_t`, но `-Wmaybe-uninitialized` это и есть).
- Union-punning регистров (`i8080_context.h`, `z80_context.h`, `mos6502context::r8/r16`, `PartsRecLE`) — LE-only по построению; все текущие цели (x86, ARM64, wasm) LE, так что это ограничение, а не дефект.

**12. [Мелочь → архитектура] Четыре одинаковых пролога `execute()`** — CONFIRMED
- `i8080.cpp:179-211`, `z80.cpp:254-293`, `6502.cpp:163-193`, `k1801vm1.cpp:420-466` — одна и та же последовательность `reset_mode → DEBUG_STOPPED → take_hold() → core->execute() → switch(m_debug)`, и она уже разошлась (п. 4: 10 против 0). Естественная правка — шаблонный метод в `CPU`: невиртуальный `execute()` с этим каркасом и `virtual unsigned run_instruction()` + `virtual void core_reset()`; ВМ1 добавляет свои два хука (`m_held_in_reset`, `m_timing`) переопределением.
- Там же: `CPU::i_address`/`i_data` объявлены в базе, но `i_address` ведёт только `mos6502::read_mem/write_mem` (`6502.cpp:56, 62`); i8080/Z80 его не трогают, поэтому `~address = cpu.address` в `deploy/computers/orion-128/Orion-128-*.cfg:157, 154, 74, 125, 82, 53` — мёртвая проводка (значение остаётся `_FFFF`). `i_m1` у i8080 и Z80, `i_data` у всех — не ведутся никогда.

**13. [Мелочь → скорость] Стоимость на обращение к памяти** — CONFIRMED (класс стоимости)
- Цепочка на каждый байт: виртуальный `i8080core::read_mem` → `I8080Core::read_mem` → виртуальный `CPU::read_mem` (`i8080::read_mem`) → `mm->read()`. Два виртуальных вызова на обращение, 2-4 обращения на команду (Z80 2.5 МГц ≈ 5-8 млн/с). Ядро не знает о `MemoryMapper` намеренно; дешёвый вариант без нарушения границы — хранить в ядре указатель на функцию/объект доступа и звать его невиртуально, или сделать ядро шаблоном по типу доступа.
- 6502 дополнительно делает `i_address.change(address)` на **каждое** обращение (`6502.cpp:56, 62`); на Агат-7 это каскадом уходит в `Memory::interface_callback` → `i_data.change()` (`core.cpp:1009-1013`) — на каждый байт при 1 МГц. Это плата за комбинаторный `rom-card-mapper`, а не ошибка, но её стоит знать.
- Опрос прерываний: i8080 зовёт виртуальный `int_request()` на каждую команду (`i8080core.cpp:237`), Z80 держит `INT` в контексте через callback — второй вариант дешевле и его стоило бы применить в i8080.
- Остальное на команду дёшево: `reset_mode`/`m_debug`/`take_hold()` — три сравнения; `check_breakpoint` — только в `DEBUG_BRAKES`; у ВМ1 `m_history` — одна запись; `timed_cycles()` — `peek_read_device()` через кэш страниц O(1), `form_of()` табличный, в `finish()` одно `%` на медленный цикл. Декодеры: вложенный `switch` (i8080/Z80), таблица указателей на члены (6502), маски (PDP-11) — нормально; флаги — таблицы `PARITY`/`ZERO_SIGN`, без ленивых схем, но и без лишней работы.

**14. [Мелочь] Z80: вектор IM2 и код IM0 зашиты** — CONFIRMED
- `z80core.cpp:2121-2141`: IM0 всегда `RST 38h`, IM2 всегда младший байт `FF`. У i8080 то же самое вынесено в `int_opcode` (`i8080.cpp:67`, используется `Unior.cfg:20`). Машина с ВН59 на Z80 (или тест IM2 с реальным вектором с шины) не конфигурируется. Симметричный параметр стоил бы десяти строк.

**15. [Мелочь] Z80 `reset()` не выставляет AF и SP в FFFF** — CONFIRMED
- `z80core.cpp:498-512` — реальный Z80 после RESET имеет `AF = SP = FFFF`. Программы, полагающиеся на SP после сброса, редки; отмечаю для полноты.

**16. [Мелочь] 6502: задержка CLI/PLP и защёлка NMI** — CONFIRMED по коду, латентно
- `6502core.cpp:1983-1990`: IRQ берётся сразу после `CLI` (реальный NMOS выполняет ещё одну команду); NMI — уровень через `set_nmi(bool)` (`6502.cpp:155`), импульс, поднятый и снятый внутри одного `clock()` устройства, теряется — Z80 для этого имеет `nmi_pending` (`z80core.cpp:901-907`). Ни один конфиг не подключает `cpu.irq`/`cpu.nmi` у 6502, поэтому сегодня не проявляется.

**17. [Мелочь] `src/emulator/disasm.cpp:51-82` — нет границ у таблиц** — CONFIRMED
- `count` растёт без проверки против `ins[2048]`, `ins[count].length` — против `bytes[16]`, а `CommandBytes` — это `uint8_t (*)[15]`, так что 16-байтовая строка `.dis` читала бы за буфером. Сегодня безопасно (`z80.dis` — 1271 строка, максимум 4 кода в строке), но это данные из файла.

## Ответы на вопросы

**Архитектура.** Разделение «ядро без зависимостей + обёртка-устройство» выдержано во всех четырёх случаях и оправдано: ядро видит только виртуальные `read/write` и хуки событий, обёртка привязывает интерфейсы, `mm`, состояние и скрипты. Доступ к памяти: байтовые ядра → `mm->read/write`, ВМ1 → `mm->read_word/write_word` с проверкой `no_device` после каждого цикла и `m_abort`-разворачиванием команды (правильно — ловушка через 4 после завершения текущей команды, стек не портится при повторном тайм-ауте). `reset_mode` откладывает сброс до первого `execute()` — это единственный корректный момент, так как `reset()` ядра читает векторы (6502 `FFFC`, ВМ1 `start_vector`), а в `load_state` он снимается. `hold()` учитывают все четыре ядра; ВМ1 ещё и двигает цепочку `Vm1BusTiming::idle()`. Точки останова — линейный поиск по 100 адресам только в `DEBUG_BRAKES`. Дублирование — п. 12. Машинно-специфичного в ядрах не нашёл: константы `0177716/0177674/0177676/0160002` в `pdp11core` — микрокод самого ВМ1, поведение включается семейством (`vm1_console`, `has_console`) и параметрами (`start_address`, `start_vector`, `halt_vector`, `halt_sel`, `reply_delay`, `timing`); у i8080 — `int_opcode`. Единственная асимметрия — п. 14.

**Полнота состояния.** i8080 — п. 2 (F). Z80 — полно, включая `process_ints`/`nmi_pending`. 6502 — полно (`commands[]` восстанавливается из типа). ВМ1 — полно, включая защёлки запросов, `m_no_trace`, `held_in_reset`, фазу `timing_now/start`; транзиентные `m_stream`, `m_last_*`, `m_acc` восстанавливать не нужно.

**Что не поймают стандартные тесты.** zexall: п. 6 (адрес `msbt` даёт нули в битах 3/5), флаги INI/IND/OUTI/OUTD (только S,Z,3,5 от B, без N/H/C/P — zexall I/O не тестирует), IM2-вектор. Klaus: п. 5 целиком, п. 16, п. 8. Для ВМ1 `timing-*`/`cputest2` не касаются EIS — п. 1 и 7 только на УК-НЦ. Проверено и расхождений не найдено: таблицы `TIMING` i8080 и Z80 (включая условные переходы, DD/FD/CB/ED, DD CB 20/23), i8080 DAA/AC при вычитании/ANA, Z80 DAA/CPI/LDI/ADC-SBC HL/RLD/RRD, задержка EI и пробуждение из HALT у обоих, NMI Z80, 6502 десятичные ADC/SBC для NMOS и 65C02 (совпадают с алгоритмом Брюса Кларка), пересечение страницы у чтений и ветвлений, JSR/RTS, B-флаг на стеке; PDP-11 CMP/SUB/ADD/ADC/SBC/NEG/ROR/ROL/ASR/ASL/MUL, MOVB со знаком, шаг SP/PC в байтовых режимах, порядок выборки индексных слов, `WAIT`/ловушки/T-бит, консольный вход ВМ1.

**Итог.** Ядра в рабочем состоянии для всех выпущенных машин; критичных дефектов на горячих путях нет. Обязательного внимания требуют: 1 (падение), 2 (порча состояния i8080), 4 (поведение «стоп» и документация расходятся). Остальное — точность вне документированного подмножества и долг 65C02.

---

# Проход 4. Устройства

Просмотрено: tape.cpp + tape_*.h, fdd, wd1793, gmd70, bk_fdc, agat_fdc140/840, uknc_hdd, i8275 + i8275display, raster_display, agat_7/9_display, bk/uknc/irisha/o128 display, i8253/8255/8251/8257/8259, dl11 + host_serial, sound/speaker/ay8910/generator/covox, keyboard/mapkeyboard/scankeyboard/uknc_keyboard, mouse/joystick/connector, indirect_memory, plane_pair, smk512, argo/unior_memory, bk_timer, uknc_channels/timer/sound/graphics, agat_9_mapper, плюс вызывающий код в `core.cpp`, `mainwindow.cpp`, `dialogs/taperecorder.cpp` и конфиги там, где вывод зависит от них.

Общий вердикт: код устройств аккуратный, контракт `get_direct`/`force`/`state_restored` соблюдается почти везде. Реальные дефекты сосредоточены в трёх местах: (а) разыменование по индексу, который задаёт гость или файл, без проверки диапазона — в двух контроллерах Агата это гарантированное падение на поставляемых конфигах; (б) смена носителя из окна без какой-либо синхронизации с потоком эмуляции; (в) один недоведённый до конца «hoist» в дисплеях Агата, оставивший виртуальный вызов на пиксель.

## Критично

**1. `src/emulator/devices/specific/agat_fdc140.cpp:80-98, 150, 221` — null-разыменование при выборе второго дисковода на конфиге с одним. CONFIRMED**
- `load_config` делает `memset(&drives, 0)` и заполняет только `drives_count` элементов (58-60). `select_drive(A & 1)` (141, 215) ставит `selected_drive = 1` по любому обращению к `$C0xB`. Далее `phase_on()` (87, 94) и запись `case 0xC` (221) зовут `drives[selected_drive]->...` без проверки; проверка `!= nullptr` есть только в чтении `0xC` и в `0xE`.
- Конфиги с одним приводом: `Agat-7.cfg:64`, `Agat-9.cfg:82`, `Agat-9-pp.cfg:82` (`drives = fdd0`). Сценарий: на Агат-7/9 команда ДОС к диску 2 (`CATALOG,D2`, любой доступ к `$C0EB` + шаг фазы) → чтение по нулевому указателю, 0xC0000005.
- Правка: в `select_drive()` не принимать `n >= drives_count` (оставлять прежний привод, как контроллер без второго кабеля), либо в `phase_on()` и `case 0xC` добавить `if (selected_drive >= drives_count || drives[selected_drive] == nullptr) break;`.

**2. `src/emulator/devices/specific/agat_fdc840.cpp:211` — то же для 840К. CONFIRMED**
- `update_state()` берёт `selected_drive` из разряда 3 порта (141) и остальные места защищены `selected_drive < drives_count`, но `get_value` case `0x4` — `drives[selected_drive]->get_loaded()` — нет. `Agat-7-840.cfg:64`, `Agat-7-840-pp.cfg:64` — один привод. Гость выбирает диск 1 и читает регистр данных → падение.
- Правка: `if (selected_drive < drives_count && drives[selected_drive]->get_loaded() && motor_on)`.

**3. `src/emulator/devices/common/i8253.cpp:206-220` — управляющее слово с SC=3 пишет за пределы массивов. CONFIRMED**
- `C = (value >> 6) & 3` может быть 3; все массивы в `i8253.h:21-31` размером 3 (`IsBCD`, `Orders`, `Modes`, `Loaded`, `Indexes`, `Counting`) и 6 (`ReadData[C*2]` = `ReadData[6]`). Запись `0xC0..0xFF` в порт управления (команда read-back ВИ54, которую шлют программы под 8254, или любой промах) молча портит соседние поля объекта: по раскладке заголовка `Modes[3]` → `IsBCD[0]`, `IsBCD[3]` → `Orders[0]`, `ReadData[6]` → `Gates[0]` (канал 0 перестаёт считать, звук/тайминги «уплывают» без сообщений).
- Правка: в начале ветки `if (C == 3) return;` (ВИ53 такое слово игнорирует; для ВИ54 это read-back — либо смоделировать, либо игнорировать явно).

**4. Смена носителя из окна GUI без синхронизации с потоком эмуляции. CONFIRMED (гонка), падение — PLAUSIBLE**
- `mainwindow.cpp:1597` → `FDD::load_image()` и `:1613` → `FDD::unload()` выполняются в потоке GUI. `fdd.cpp:150-151, 237-238, 460-461` делают `delete [] buffer; buffer = new ...` и переписывают `sides/tracks/sector_size/track_mode`, пока поток эмуляции стоит в `ReadNextByte/WriteNextByte` (`fdd.cpp:375, 381, 403`) или в `BKFDC::encode_track` (`bk_fdc.cpp:248-249` — 5120 чтений подряд на смене generation, т.е. ровно в момент смены диска). Аналогично `dialogs/taperecorder.cpp:287` → `TapeRecorder::load_file` → `set_data()` (`tape.cpp:585`) переприсваивает `data`, пока `clock()` читает `data[data_position]` (`tape.cpp:739`); `taperecorder.cpp:400` → `set_recording()` сбрасывает декодеры, в которые `interface_callback` пишет из потока эмуляции; `taperecorder.cpp:443` присваивает `d->on_mode_changed = nullptr`, пока `notify_state()` (`tape.cpp:512`) может вызывать этот `std::function`.
- В `Emulator` нет замка на устройства (`emulator.h`: мьютексы только для host keys, скриншота и состояния); модальный `QFileDialog` не останавливает поток эмуляции. Единственное устройство, которое защищает себя само, — `UKNCHDD` (`m_image_mutex`, `uknc_hdd.cpp:192, 324, 341`).
- Сценарий: на БК ANDOS крутит мотор и повторяет чтение сектора; пользователь вставляет другой образ → `encode_track` читает освобождённый/ещё не заполненный `buffer` с геометрией «наполовину новой» → порча кучи, симптом как у уже пойманного падения в `closeEvent`.
- Правка (минимальная): в `FDD` — `compat_mutex` вокруг замены буфера и вокруг `ReadNextByte/WriteNextByte/WriteByte/SeekSector` (один захват на байт дёшев, как в UKNCHDD); в `TapeRecorder` — новый образ класть в «pending» и переключать в `clock()`; либо общий путь: GUI ставит флаг-запрос, а замену делает `timer_proc()` между инструкциями (паттерн `m_screenshot_requested` уже есть). Того же класса, но признанного в CLAUDE.md: `MapKeyboard::send_key` (`mapkeyboard.cpp:246-248`) и `ScanKeyboard::calculate_out` (`scankeyboard.cpp:355`) дёргают интерфейсы/порт из потока GUI — для БК это линия запроса прерывания клавиатуры; очередь, как у `UKNCKeyboard`, сняла бы и это.

## Важно

**5. `load_state()` доверяет индексам и геометрии из файла, а `.ecats` приходит по ссылке (`index.html?load=`). CONFIRMED (механизм), нужен подготовленный файл**
- `fdd.cpp:747-761`: `sides/tracks/sectors/sector_size` читаются независимо от `disk_size`; `translate_address()` (346) затем уходит за `buffer`. Таблица `track_indexes` (775-785) не сверяется с `disk_size`.
- `i8275.cpp:496`: `m_fill_buf` ≥ 2 → `m_row_buf[m_fill_buf]` (306) за массивом из двух.
- `uknc_hdd.cpp:807`: `m_bufferoffset` нечётный или ≥ 512 → `memcpy` за `m_buffer` (577, 604).
- `gmd70.cpp:283`: `m_counter` ≥ 128 → `m_buffer[m_counter]` (103, 153).
- `agat_fdc140.cpp:279` / `agat_fdc840.cpp:319`: `selected_drive` → `drives[]` (см. п. 1-2).
- `tape.cpp:899`: `bit_shift` > 31 → сдвиг в 739 — UB.
- Правка: клэмп при чтении (`& 1`, `% 512`, `< drives_count`, `< 8`), для FDD — проверить `sides*tracks*sectors*sector_size <= disk_size` и каждое `offset+size <= disk_size`, иначе вернуть `FileError`. Всё по одной строке. (`bk_fdc.cpp` в этом смысле образцовый: `current()` и `sector_in_range()` защищают всё, что пришло из снимка.)

**6. `src/emulator/devices/common/fdd.cpp:126-173` — загрузчик `.mfm/.hfe` верит таблице дорожек из файла. PLAUSIBLE**
- `disk_size = track_indexes[0].mfmtracksize * tracks` (149) предполагает равные дорожки; `mfmtrackoffset/mfmtracksize` каждой дорожки не сверяются с `disk_size`; `sides` из заголовка не проверяется, а `track*sides + side` (355, 381, 409) индексирует таблицу, прочитанную только на `tracks` элементов; результаты `file.read()` не проверяются (короткий файл → в «диск» уходит мусор из `new[]`); `mfmtracklistoffset` не проверяется. `sector_in_range()` (289-295) для этого режима проверяет лишь `< 200`.
- Сценарий: двусторонний или разнодорожечный HxC-образ на Агате → `ReadNextByte` читает за буфером.
- Правка: после загрузки пройти таблицу и отвергнуть образ, если `offset + size > disk_size` или `sides*tracks > 200`; проверять `gcount()`.

**7. `agat_7_display.cpp:181-200`, `agat_9_display.cpp:211-230` — виртуальный вызов на каждый пиксель при включённой палитровой карте. CONFIRMED**
- `convert_rgba()` на каждый пиксель делает `m_pal_switch->get_direct(0)` (виртуальный) плюс ветвления; вызывается в 248/269/306/340/357 (А7) и 284/300/340/372/391/412/466/493 (А9). Комментарий там утверждает, что чтение вынесено, но вынесены только три чтения памяти палитры — переключатель остался. Стоимость: 512×256 пикселей × 50 кадров ≈ 6,5 млн виртуальных вызовов/с в потоке эмуляции на `Agat-7-max` и палитровых конфигах.
- Правка: в начале `render_line()` один раз прочитать `pal`, выбрать `const uint32_t * table` (стандартная, `Agat_RGBA16_palcard_std[pal]` или перестроенная `Agat_RGBA16_palcard`), и индексировать её напрямую.

**8. `src/emulator/devices/specific/irisha_display.cpp` — производное состояние дисплея не восстанавливается из снимка и частью не инициализируется. CONFIRMED по коду (теста `state-irisha` нет)**
- `load_state` (256-266) возвращает `m_mode/m_color/m_page/m_base_address/m_mode_index`, но не `m_fore_color`, `m_back_color`, `m_page_size` — те выводятся из портов в `clock()` (116-174), а `clock()` пересчитывает их только при *изменении* порта; после восстановления порты уже равны сохранённым → пересчёта не будет. `m_page_size` остаётся `0` из конструктора → `render_all` (202-206) не рисует ни байта: **после открытия снимка Ириши экран пуст, пока программа не тронет видеопорт.**
- Там же: `m_fore_color`/`m_back_color` не инициализируются (`irisha_display.h:17-18`) и в режиме 3 (139-145, 163-169) `m_fore_color` не присваивается вовсе — `render_mono` (220) рисует неинициализированным `uint32_t` (UB, видно как случайный цвет), если машина входит в «моно высокое» первым. И в ветке «Blanking» (123) третий случай ставит `m_mode_index = 3` (141), тогда как два других — 0: гашение для этого режима не работает.
- Правка: инициализировать оба цвета; в `state_restored()` сбросить `m_mode = _FFFF` (или вынести разбор портов в функцию и позвать её); присвоить `m_fore_color` в ветках режима 3; решить, должно ли 141 быть `0`.

**9. `src/emulator/devices/specific/agat_9_display.cpp:445, 450` — приоритет операций: `v & 0x3F + 0x80` есть `v & 0xBF`. PLAUSIBLE**
- Инверсные и мигающие символы текстового режима Apple II рисуются глифами `0x00-0x3F` вместо `0x80-0xBF`. Проявление зависит от раскладки шрифта Агат-9; проверяется одним скриншотом инверсного текста в Apple-режиме. `-Wparentheses` это ловит.
- Правка: `(v & 0x3F) + 0x80`.

**10. Гость может остановить машину обычным обращением к шине через `im->dm->error()`. CONFIRMED (поведение), решение за владельцем**
- `i8257.cpp:113` — на *чтение* регистра 9-15 (`$E009-$E00F`); `wd1793.cpp:171/184` — многосекторное чтение/запись (бит m); `wd1793.cpp:217` — Force Interrupt с параметрами; `i8255.cpp:85/176`. `DeviceManager::error()` (`core.cpp:342-354`) → `timer_proc` останавливает машину. Тест портов или копировщик, дающий WD1793 `$D8`, замораживает эмулятор с ошибкой там, где железо просто вернуло бы что-то.
- Предложение: чтения делать no-op (`_FFFF`), `error()` оставить только для действительно немоделируемых путей записи.

**11. Дисплеи опрашивают порты на каждой инструкции вместо реакции на запись. CONFIRMED (стоимость)**
- `irisha_display.cpp:118-122` — три виртуальных `get_direct()` + три сравнения на инструкцию (и ещё три чтения при изменении); `o128display.cpp:84-85` — два; `agat_7_display.cpp:144`, `agat_9_display.cpp:201` — по одному сверх `RasterDisplay::clock`. Класс: на инструкцию (Ириша ≈ 7,5 млн виртуальных вызовов/с при 2,5 МГц). У этих `Port` есть `i_data`, а у `RAM` — `set_memory_callback`; `I8275Display` берёт бордюр через `interface_callback` — тот же приём снял бы опрос.

**12. `src/emulator/devices/common/tape.cpp:1372, 1411` — O(N) по записям дважды на битовый интервал. CONFIRMED (стоимость)**
- `advance_unit()` и `current_bit()` каждый раз зовут `record_start(m_rec)` (1258-1265), которая суммирует `m_records` с нуля. На ленте-накопителе (Юниор/Арго) это 2×`baud_rate` проходов в секунду × число записей: том TCP/M из сотен секторов даёт порядка 10^6 сложений/с на потоке эмуляции.
- Правка: держать `m_rec_start`, обновляемый в `locate()` и при `m_rec++` (1388) — обе точки уже знают границу.

## Мелочь

**13. `tape.cpp:665-666, 647/581` — разбор `[TapeFiles]` из ini.** `bytes[1]` без проверки `bytes.size() >= 2` (строка формата `...;E6;data` без `:count` → выход за `std::vector`); `baud = 0` → деление на ноль в `set_baud_rate()`. Файл редактируется пользователем. PLAUSIBLE. Правка: валидировать и вернуть `Result::error`.

**14. Разбор списка `drives` в четырёх контроллерах не проверяет ни количество, ни тип.** `wd1793.cpp:50-52` (`drives[4]`, `drives_count = parts.size()`), `gmd70.cpp:70-72` (`m_drives[2]`), `agat_fdc140.cpp:58-60`, `agat_fdc840.cpp:54-62` (`drives[2]`): пять приводов переполняют массив, опечатка в имени даёт `nullptr` и падение при первой команде. Проверяет только `bk_fdc.cpp:75-90`. CONFIRMED. См. дублирование ниже.

**15. `uknc_hdd.cpp:753-763` — `save_state` читает весь образ в память.** Комментарий сам говорит про образы в сотни мегабайт; снимок УК-НЦ/СМК с большим `.hdi` — временный вектор такого размера плюс столько же в zip; на 32-битной сборке (кит Win7 i386) это `bad_alloc`. Осознанное решение; альтернатива — писать оверлей (`m_overlay`) и ссылку на неизменённый файл. PLAUSIBLE.

**16. `sound.cpp:310` — `refresh_sources()` на каждый звуковой отсчёт.** Очистка/сборка `m_active`, виртуальные `sound_active()`/`sound_volatile()` по каждому источнику и `m_level_dirty = true` — 22050 раз в секунду, после чего следующий `clock()` пересчитывает всю сумму даже для молчащих источников. Комментарий говорит, что активность меняется только при записи/сбросе — для этого уже есть `source_mode_changed()`. CONFIRMED (стоимость на отсчёт, не на инструкцию).

**17. `tape.cpp:58-61` + `sound.cpp:85` — у каждого магнитофона свой `Speaker`, т.е. второй полный тракт `GenericSound` и второе аудиоустройство хоста на машину** (два потока miniaudio, два кольцевых буфера, два фильтра, два `clock()` на инструкцию). Работает, но дороже, чем `SoundSource`, подмешанный в динамик машины через уже существующий `mix =`. PLAUSIBLE.

**18. `uknc_sound.cpp:155-196`, `uknc_timer.cpp:105-119` — единственный путь с плавающей точкой на инструкцию.** Пока звучит тон (`m_self_volatile`), на каждую инструкцию ПП считаются double и 1-2 вызова float-Баттерворта (~0,5 млн/с). Модель обоснована в комментарии; фиксирую как факт, а не как дефект.

**19. `dl11.cpp:337-358` — `m_plug` (заглушка на разъёме) не сохраняется в состоянии.** Снимок, снятый с `COMMAND dl11.plug(1)`, открывается без петли. Только для сценариев. CONFIRMED.

## Полнота состояния

Проверены `save_state/load_state/state_restored` у FDD, WD1793, GMD70, BKFDC, обоих FDC Агата, UKNCHDD, I8275/I8275Display, RasterDisplay + BK/UKNC/Agat7/Agat9/O128, GenericSound/AY/Speaker/Covox, ВИ53/ВВ55/ВВ51/ВТ57/ВН59, DL11, ленты, всех клавиатур, мыши/джойстика, IndirectMemory, SMK512, Argo/UniorMemory, BKTimer, UKNC timer/channels/sound. Запретное «`load_state` ведёт линию» нигде не нашёл: линии везде выводятся из восстановленных интерфейсов (`i8275.cpp:509`, `tape.cpp:1030`) или пересчитываются в `state_restored()` (`ArgoMemory`, `UKNCSound`, `GenericSound`, `IndirectMemory`). Пропуски производного состояния: **IrishaDisplay (п. 8)** и `DL11::m_plug` (п. 19). У `BKFDC::save_state` вызов `flush_all()` (629) ведёт `i_side` — это на потоке эмуляции при сохранении, допустимо.

## Дублирование

- **Загрузка списка `drives`** — пять копий (wd1793, gmd70, agat_fdc140, agat_fdc840, bk_fdc), с проверками только в одной. Просится `FDC::load_drives(max, &count, FDD**)` в `core.h`.
- **Agat7Display ↔ Agat9Display** — `convert_rgba`, `convert_font`, разбор `pal_*` из конфига, мигание, `get_device_options` палитровой карты, `HSYNC/VSYNC` — ~150 строк почти построчно одинаковы; исправление п. 7 придётся делать дважды.
- **Байтовая половина слова поверх `get_value_word`** — одни и те же три строки в `bk_timer`, `uknc_timer`, `uknc_channels`, `uknc_graphics`, `dl11`, `uknc_hdd`, `indirect_memory`, `bk_fdc` (8 копий). Это естественная реализация по умолчанию для `AddressableDevice` с флагом «словное устройство».
- **Отложенная смена режима на потоке отрисовки** — `BKDisplay::get_screen_constraints` и `UKNCDisplay::get_screen_constraints` реализуют один и тот же `m_mode_pending` шаблон; у UKNC ещё и таблица цветов строится дважды (`uknc_display.cpp:109-121` и `136-146`).
- **Оценка «единицы» медианой отсортированной головы** — по копии в `tape_bk.h:295-298`, `tape_rk86.h:82-87`, `tape_uknc.h:169-171`.
- **`run_transfer()` и обратная запись регистров ВТ57** — `argo_memory.cpp:193-247` и `unior_memory.cpp:111-154` (при этом конец счёта они записывают по-разному: `$3FFF` у Арго, `0` у Юниора, комментарий Арго объясняет, почему должно быть как у `dma_next()`).
- `GMD70::get_value`/`get_direct` дважды собирают регистр состояния (94-98, 120-124).

## Общая оценка

Устройства написаны с явным вниманием к контракту (`get_direct` без побочных эффектов, `force`, `state_restored`, кэши палитр, снимок кадра у ВГ75) и с хорошими пояснениями; большинство «горячих» путей уже оптимизированы (прыжок по событиям в `I8275::clock`, прямые буферы у BK/UKNC дисплеев, кэш `Speaker`). Обязательно исправить: п. 1-3 (детерминированные падения/порча памяти по действию гостя на поставляемых конфигах) и п. 4 (гонка на смене носителя — та же природа, что уже пойманное падение при закрытии окна). Затем п. 5-6 (валидация входных файлов — снимки приходят по ссылке), п. 7-8 (пиксельный виртуальный вызов и пустой экран Ириши после снимка). Остальное — по мере касания кода; список дублирования стоит иметь в виду при правке п. 7 и п. 14, чтобы не чинить одно и то же в двух-пяти местах.

---

# Проход 5. Разбор файлов, снимки, скрипты, MCP

Paths are relative to `src/`.

## Answers to the direct questions

- **Zip path traversal**: sound. `ZipReader::is_safe_name` rejects absolute names, `:`, and `..`; backslashes are normalised first; all names are checked before anything is written.
- **Per-entry inflate bound**: sound. lodepng honours `max_output_size` (64 MB), and offsets are bounds-checked by subtraction.
- **`.ecats` cannot carry `@script`**: enforced on every path. Both `machine_base_file()` and `load_machine_description()` go through `MachineStateFile::parse()`; a `@script` inside `@config` fails as a config error.
- **Can a snapshot read or write an arbitrary host file?** Yes to both, see findings 1 and 3.
- **What can an `.ext` `@script` do to the host?** Write arbitrary files with arbitrary content, see finding 2.
- **`tick()` fast path**: correct as far as I traced it (atomic state and two wake-up moments, everything else under the mutex). It costs two or three atomic loads per slice; no performance problem there.

## High severity

**1. Snapshot disk geometry is not checked against the buffer: guest-driven heap read/write.** CONFIRMED
- `emulator/devices/common/fdd.cpp:742-756, 771-786` (load), used at `:344-347, 375, 381, 403, 409, 424, 429`.
- `FDD::load_state()` takes `disk_size`, `sides`, `tracks`, `sectors`, `sector_size` and, in MFM mode, `track_offset[]`/`track_size[]` straight from the file. The buffer is `disk_size` bytes; `sector_in_range()` (`:288-296`) checks only the geometry.
- Scenario: a `.ecats` with `disk_size = _512`, a 512-byte blob, `tracks = _80`, `sectors = _9`, `sector_size = _512`, and guest RAM holding a program that writes through the FDC. `buffer[translate_address()]` then reaches ~700 KB past a 512-byte allocation. In MFM mode `track_offset[i]` is an arbitrary 32-bit index.
- On desktop this is memory corruption from a file opened from someone else; in the browser it stays inside the wasm heap.
- I only traced FDD. `StateReader` hands raw values to every device, so other `load_state()` implementations deserve the same audit.

**2. Opening an `.ext` / `.ext.zip` runs its `@script`, which can write any file with any content.** CONFIRMED
- Auto-start: `mainwindow.cpp:1374-1380` (`run_embedded` defaults to true, `mainwindow.h:65`), `headless/headless_main.cpp:883-891`, `wasm/wasm_main.cpp:106`.
- Write primitives: `COMMAND ram.save("path", from, to)` at `emulator/core.cpp:1120-1146`; `SAVESTATE` at `script_engine.cpp:612-622`; `SCREEN` at `:962-974`; `fdd.save`, `tape.save`, `sound.record`.
- `resolve_output_path()` (`utils.cpp:308-314`) takes absolute names as they are and does not filter `..` in relative ones.
- Scenario: an `.ext` in Downloads with `COMMAND ram.set(0, …)` then `COMMAND ram.save("../AppData/Roaming/Microsoft/Windows/Start Menu/Programs/Startup/x.bat", 0, _200)` plants a startup file.
- `LOGFILE` (`script_engine.cpp:656`, `script_log.cpp:43`) also accepts `..`, but is limited to a `-<timestamp>.log` suffix.
- `LOAD` and `dev.load` read any host file into the guest.
- The rule "a fetched file must not run anything" is applied to `.ecats` only; `index.html?load=<x.ext>` does run the script, though inside MEMFS.
- Suggested fix: confine embedded-script output to the script's directory (or a sandbox directory), or ask before running a script from a file outside `computers/`.

**3. A snapshot's `@config` (or an `.ext`) can point a device at any host path; the hard disk opens it read-write.** CONFIRMED
- `utils.cpp:328-329`: `find_file_location()` accepts absolute names, and relative ones may contain `..`.
- `emulator/devices/specific/uknc_hdd.cpp:282-288` loads `image` from the config; `:105` opens it `r+b`; `:342-354` writes sectors through unless `volatile`.
- `load_state()` forces `m_volatile = true` only when the state says `attached` (`:773-790`); a crafted state simply omits it.
- Scenario: a `.ecats` whose `@config` has `hdd : uknc-hdd { image = C:/Users/x/…/places.sqlite }` and whose restored CPU is mid-write. Any host file whose size is a multiple of 512 (disk images, VM disks, SQLite databases) is overwritten sector by sector.
- The same mechanism reads any file into the guest: `fdd:image`, `rom:image`, `StateReader::blob()` at `state.cpp:637`.
- `looks_like_a_file()` (`state_save.cpp:63-81`) will bundle any such file into the next snapshot the user saves and shares.
- Suggested fix: for a machine loaded from a state, resolve names only under `ext_path`, reject absolute names and `..`, and force media volatile.

**4. Archive unpacking has no total limit and the cache is never pruned.** CONFIRMED
- `config_ext.cpp:153-178`, `libs/zip_reader.h:49`.
- The limit is 64 MB per entry, but up to 65,534 entries are accepted and several central entries may point at the same local data.
- Scenario: a ~3 MB `.ecats.zip` whose entries all reference one 62 KB deflate stream expands to terabytes in `%TEMP%/ecat3-cache/ext/<name>-<crc>/` (or exhausts MEMFS in the browser).
- I found no code that deletes anything under `cache_path`, so every distinct snapshot ever opened stays on disk.

**5. A throwing `WAITFOR` kills the emulation thread without a word.** CONFIRMED
- `script_engine.cpp:1007-1019`: `waitfor_step()` runs from `tick()` (`:537-541`), outside `execute()`'s try/catch. It parses `c.args[1]` and, via `read_field()` → `parse_range()` (`:873-893`), the bracket parameters. `do_waitfor()` validates neither.
- Scenario: `WAITFOR cpu.pc == 8` on a БК (8 is not an octal digit), or `WAITFOR ram.value(zz) == 1`. `std::invalid_argument` leaves `tick()`, and the catch-all at `emulator.cpp:755-760` sets `m_running = false` and stores no message.
- Outcome: GUI shows a frozen picture with no error; headless spins forever in `while (!script_finished())`; MCP reports "[stalled] … CPU is halted", which is wrong.
- Fix: parse both in `do_waitfor()`; make the catch-all record `ex.what()` in `dm->error_message`.

**6. Exceptions on the load path escape to `std::terminate`.** CONFIRMED
- `emulator.cpp:204-205`: `system->get_parameter("type")` and `("name")` are `required = true` and throw. `mainwindow.cpp:1316` and `headless_main.cpp:724` have no try; only wasm catches (`wasm_main.cpp:114`).
  - Scenario: a `.ecats` whose `@config` has `system { radix = 8 }` only, or an `.ext` with `-system:name`, terminates the desktop process.
- `emulator.cpp:699`: `apply_state()` runs on the emulation thread before the try block at `:755`.
  - Scenario: a snapshot with FDD `disk_size = _3000000000` makes `fdd.cpp:757-758` call `new uint8_t[negative int]`, which throws and terminates.
- `files.cpp:204-217` (`load_hex`): a short line makes `substr` throw `out_of_range`, or `"$"` throw `invalid_argument`. File > Open (`mainwindow.cpp:1455`) has no try, so a truncated `.hex` kills the GUI.
- `files.cpp:85, 103` (`load_rk`): `len = end - start` underflows when end < start; `std::vector(len)` of ~4 GB throws on the same unguarded path.

## Medium severity

**7. `files.cpp` loaders trust the file.** CONFIRMED
- `load_hex` (`:209-215`) writes `buffer[addr + j]` with no check against the RAM size. A record at `FFF0` with length `FF` writes 239 bytes past a 64 KB RAM; on a smaller `ram` it is worse.
- `load_rko` (`:322-323, 328, 334`) dereferences `ram1`/`ram2`/`ram3` without a null check. `LOAD "x.rko"` on any non-Орион machine is a null dereference.
- `load_rk` (`:154`): `MIN(len, page_size - delta)` underflows when the load address exceeds the RAM size.
- `ReadHeader` (`:19-38`) returns ok with the header uninitialised when the file cannot be opened.

**8. A snapshot with a medium over 64 MB saves but cannot be opened; the save stalls the machine.** CONFIRMED mechanism, timing PLAUSIBLE
- `uknc_hdd.cpp:754-763` puts the whole image into the bundle. `ZipWriter` has no limit below 4 GB. `ZipReader::read` (`libs/zip_reader.cpp:98`) refuses entries over `max_entry_size`.
- The project's own comment says hard disk images are "hundreds of megabytes", so such a snapshot is written successfully and then fails with "entry is too large".
- The save runs on the emulation thread and holds about four copies of the image (vector, bundle member, deflated entry, archive string) while `lodepng_deflate` runs. Expect seconds of silence.

**9. An empty string value cannot be written or read back.** CONFIRMED tokenizer behaviour, reachability PLAUSIBLE
- `state.cpp:212-215`, `config.cpp:136, 353-358`.
- `config_quote_value("")` returns an empty string, so the writer emits `key = `. Even a written `""` is dropped by `next()`'s `if (s.empty()) continue`.
- The next line's key becomes the value, and the load fails with "incorrect parameters".
- `FDD::save_state` writes `file_name` unconditionally (`fdd.cpp:688`). Tape already guards with `!loaded_name.empty()`, which suggests this has been hit before.
- Fix in `StateWriter::s` and the tokenizer, not per device.

**10. `parse_numeric_value()` hazards.** CONFIRMED (`utils.cpp:207-208, 243-247`)
- `toupper(s[i])` on plain `char` is undefined for bytes ≥ 0x80 (`size = 64к` with a Cyrillic к). The comment at `:42-46` describes this exact bug for `str_tolower`, but this call site was missed.
- `strtoul` accepts leading whitespace and a sign: `-1` becomes `0xFFFFFFFF`.
- Overflow is not checked: `$100000000` becomes 0 on LP64, and `value * mult` wraps for `5000000k`.

**11. Non-ASCII paths on Windows.**
- CONFIRMED: `libs/lodepng/lodepng.cpp:396` uses `fopen` with a UTF-8 name. `SCREEN` (`emulator.cpp:1055`) and the GUI screenshot (`mainwindow.cpp:1813`) therefore fail or create a mojibake name when the script or target directory is Cyrillic. The script case also prints to stderr, which fails the test.
- PLAUSIBLE: `headless_main.cpp:772` is `main(int, char**)`, so arguments arrive in the ANSI code page. They go to `fs::exists` / `fs::absolute` (`:629, 689-690, 836`) and then to the dsk_tools UTF-8 helpers. `eCat3-headless --script C:\Тест\a.ecat` either throws a conversion error (uncaught at `:836`) or reports the file missing.

**12. `read_confg_value(..., std::string)` lower-cases its result, and it is used for file names.** CONFIRMED
- `utils.cpp:375-383`; `uknc_hdd.cpp:282` reads `image` through it.
- `image = Disk/HDD.IMG` is looked up as `disk/hdd.img` and not found on Linux or macOS. Under a single-byte C locale `tolower` would also corrupt UTF-8 bytes.

**13. The ambient radix is an unsynchronised global.** CONFIRMED design hazard
- `utils.cpp:185`. It is written on the GUI thread (`emulator.cpp:213`) and on the emulation thread (`RADIX`, `apply_state` at `emulator.cpp:405`), and read everywhere.
- `collect_config_fields()` (`config_fields.cpp:55-89`) builds a second machine on the GUI thread without setting it. Constructors that parse numbers (for example the `clock` multiplier at `core.cpp:414-424`) use the running machine's radix and swallow the resulting exception. Today this only yields wrong defaults inside the editor's throwaway machine.
- `EmulatorConfigDevice::radix` is populated (`config.cpp:323`) but nothing parses with it. Passing `cd->radix` into `read_confg_value` would remove the global from config parsing.

**14. Write errors are never checked.** CONFIRMED
- `state_save.cpp:244-247, 257-260, 274-279`; `config_ext.cpp:65-72`; `script_parser.cpp:276-282`.
- Only the open is tested. A full disk gives a truncated snapshot, recording, or cached ROM reported as success.
- A snapshot is written in place, so a failed save also destroys the previous file.

## Low severity

**15. Config errors carry no line number.** CONFIRMED. `ConfigReader::line()` (`config.cpp:86`) is never called. A missing `}` in a 300-line config is reported by device name only. `.ext` and script errors do have lines.

**16. A UTF-8 BOM breaks `.cfg` and `.ecat`.** CONFIRMED. Only `ConfigExtension::parse` strips it (`config_ext.cpp:204`). A `.cfg` saved by Notepad fails with "no type found" (`config.cpp:266-274`); a script's first `MACHINE` line becomes "Unknown command" (`script_parser.cpp:195-208`).

**17. `#` is both the script comment marker and the binary prefix.** CONFIRMED. `script_parser.cpp:72`. A value printed by `LOGDEFS _8,_2` (`#1010…`) cannot be typed back into a script unquoted. It produces an error rather than a wrong value.

**18. Unguarded `std::stod` / `stoi` / `stoul` on ini values.** CONFIRMED. `emulator.cpp:444, 923-925`. `scale=` left empty or a damaged `[DeviceOptions]` entry throws on the GUI thread at every start. `std::to_string(double)` at `:1191` writes a locale decimal comma where Qt has set the locale (Linux).

**19. The machine chooser reads every snapshot in full.** CONFIRMED for the function; per-file cost PLAUSIBLE. `dialogs/openconfigwindow.cpp:137, 317` call `load_machine_description(system_only = true)`. For a `.ecats.zip` that reads the whole archive, copies it again in `ZipReader::open` (`zip_reader.cpp:33`), inflates the text, and splits the entire `@state`. The header alone would do.

**20. MCP.**
- `mcp/mcp_server.cpp:48-55`: `static_cast<unsigned int>(double)` is undefined above `UINT_MAX` (`timeout_ms: 1e20`).
- `wait_done()` (`mcp_session.cpp:160`) calls 250 ms without clock movement a stall; a starved emulation thread on a loaded host gives a false "[stalled]".
- `clock_now()` reads a plain `uint64_t` across threads (`emulator.h:148, 274`); it can tear on the 32-bit kits.
- The interactive buffer only grows (no trimming after `interrupt()`).

**21. `dsk_tools::base64_decode`.** CONFIRMED. `libs/dsk_tools/src/utils.cpp:156-167`. `val = (val << 6) + …` on an `int` overflows (undefined behaviour) after five characters. `=` is ignored anywhere, and the lookup map is rebuilt on every call. Output is bounded by input.

## Architecture

- **One tokenizer for cfg, ext and state** is a good choice, and the selftest round-trips protect it. Its weak points are all in `ConfigReader::next()`: the dropped last character, `//` cut inside quoted values, no representation for an empty string or a `"`, and no line tracking. Fix them there once rather than around it (the `body += "\n"` in the extension parser, the tape-name guard).
- **Script threading**: the documented rule (buffer edited only while inactive, or under the mutex in interactive mode) holds in the code I traced. It depends on the GUI transport being disabled under MCP (`mainwindow.cpp:1961`). One oddity: `seek()` while paused keeps the interrupted wait and resumes into it.
- **Script launch logic is written three times** (GUI `mainwindow.cpp:408-423, 1374`; headless `:830-892`; wasm `:106`), each with its own copy of the "who wins" rule. A single `Emulator::start_default_script(explicit)` would keep them aligned.
- **State restore trusts devices to validate.** A few checked helpers on `StateReader` (`u_in_range`, a size-checked geometry read) would address finding 1 and its likely siblings systematically.

## Overall assessment

The container-level code is careful: the zip reader, hex dump, extension parser and `.ecats` header parsing are bounded and report errors properly. The exposure is one layer up. Once a file is accepted, its configuration, state values and script are treated as if the user had written them. That gives three real attack paths from a shared file on desktop (findings 1-3) and several crash paths (5-7). The web build is shielded from the file-system ones by MEMFS but not from finding 1 or the zip bomb. The most valuable single change is to treat a machine loaded from `.ecats` or a foreign `.ext` as untrusted: confined paths, volatile media, validated geometry, and no unprompted script.

---

# Проход 6. Фронтенды и сборка

Scope: `src/main.cpp`, `src/mainwindow.*`, `src/dialogs/*`, `src/renderers/*`, `src/qt_utils.*`, `src/headless/*`, `src/wasm/*`, `src/mcp_bridge.*`, `thread_compat.h`, `renderer.h`, `src/CMakeLists.txt`, `globals.h.in`, `.build/*`, plus the parts of `emulator.cpp`/`core.cpp` the frontends call into.

## A. Crashes and memory corruption

### 1. A failed machine load leaves the toolbar and menus bound to destroyed devices — CONFIRMED (high)
`src/mainwindow.cpp:1316-1325`, `src/emulator/emulator.cpp:148-166`

`MainWindow::load_config()` calls `e->load_config()`, which deletes `dm`/`im` of the old machine first. On failure the GUI shows the message and returns before `CreateDevicesMenu()` / `UpdateToolbar()`. What stays alive:
- `fdds[]` holds raw `FDD*` of the deleted machine. `fdd_open(n)` reads `fdds[n]->files` (`mainwindow.cpp:1591`); `fdd_eject`/`fdd_wp`/`fdd_write` do the same.
- `hdds[]` is the same, through `get_field`/`send_command`.
- The option combo lambdas captured `dev` by raw pointer (`mainwindow.cpp:907-909`).
- The Devices menu indexes the new, shorter `dm` with old indices (`mainwindow.cpp:941`).
- Cold/Soft restart calls `e->reset()` with no `loaded` check (`mainwindow.cpp:1203`, `1210`), so `dm->reset_devices()` runs on devices whose `load_config` never finished.

Scenario: a machine is running, the user opens a `.cfg` with a missing ROM or a stale `.ecats`, dismisses the error, clicks a floppy button. That is a use-after-free.

Fix: on failure rebuild the toolbar and menus for "no machine" (clear `fdds`/`hdds`/combos, disable the machine actions), and guard `reset` with `loaded`.

Related leak: when the previous load failed, `loaded` is false, so the next `load_config` skips `delete dm; delete im` and overwrites both (`emulator.cpp:148-169`). A half-built machine leaks, including any audio device it already opened.

### 2. `DisAsmArea::lines[1000]` has no bound — CONFIRMED (high)
`src/dialogs/disasmarea.h:14,53`, `src/dialogs/disasmarea.cpp:155-183` (`disassemble_lines`), `:286-296` (`move_cursor`)

`move_cursor()` extends the buffer with `disassemble_lines(lines_count, lines_to_add)`, and `lines_count += count` is never checked against `DISASM_SIZE`. Each PageDown adds a screenful. After about 30 PageDowns in the debugger, `lines[i].command = QString...` writes `QString` objects past the array into the rest of the widget.

### 3. `DumpArea::buffer[2][64*16]` overflows in a tall window — CONFIRMED mechanism, PLAUSIBLE reachability (high)
`src/dialogs/dumparea.h:52`, `src/dialogs/dumparea.cpp:88`, `:226-238`

`lines_count = height / font_height - 1` has no cap, and neither `.ui` sets a maximum height for the dump area. `fill_buffer()` writes `buffer[x][i*16+j]` for every line, and `paintEvent` reads it. A dump or debug dialog stretched past about 64 text rows (roughly 1100-1300 px, a maximised dialog on a 1440p/4K monitor) overruns the member array.

### 4. The Tape menu item terminates the process on a machine without a device named `tape` — CONFIRMED (high, trivial to hit)
`src/mainwindow.cpp:1886`, `src/emulator/core.cpp:292-293`, `src/mainwindow.ui:193`

`on_actionTape_triggered()` calls `e->dm->get_device_by_name("tape")` with `required = true`, which throws `std::runtime_error`. The menu action is never hidden or disabled; only the toolbar button is conditional. There is no `try` around `a.exec()` and no `QApplication::notify` override, so the exception leaves a slot and the process terminates. Every Agat config has no `taperecorder` (e.g. `deploy/computers/agat/Agat-7.cfg`).

The toolbar button has the same defect in a different form: it is shown for any device of class `tape`, then looked up by the name `tape`.

Other exceptions that escape a slot the same way:
- `Emulator::init_video()` does `get_device_by_name("display")` with `required = true` and unguarded `std::stod`/`std::stoi` on `[Video] scale/ratio/filtering` (`emulator.cpp:916`, `923-925`). An empty `scale=` in the ini would kill every start (PLAUSIBLE).
- `apply_saved_device_options()` does an unguarded `std::stoul` (`emulator.cpp:444`).
- `MemoryMapperWindow`, see 5.

One top-level guard (a `notify()` override, or try/catch in `load_config`) turns all of these into message boxes.

### 5. Memory-mapper debug window: bad input or an unmapped address crashes — CONFIRMED (medium)
`src/dialogs/mmwindow.cpp:39-40`, `:44-52`

`parse_numeric_value()` is called without a try (the dump window, by contrast, has one). Empty or non-hex text throws out of the slot. `map_memory()` returning `nullptr` for an unmapped address is dereferenced straight away (`d->get_value(...)`, `d->name`). That `get_value` is also a side-effecting read whose result is discarded.

Same family: `DumpWindow::on_saveButton_clicked()` dereferences `dynamic_cast<Memory*>(d)` unchecked (`dumpwindow.cpp`, save handler).

### 6. Debug windows outlive their devices during the error box of a failed load — CONFIRMED mechanism (medium)
`src/mainwindow.cpp:1306-1313`, `:1323`, `src/dialogs/debugwindow.cpp:93-95`, `:288-290`

The windows are closed with `WA_DeleteOnClose`, which is `deleteLater()`. Qt does not run deferred deletes inside a nested event loop, and `QMessageBox::critical` at line 1323 is one. While the box is up, the hidden `DebugWindow` still has its 200 ms `state_timer` (never stopped; there is no `closeEvent`) and any pending `track()` single-shot. They read `cpu->m_debug` and `cpu->get_registers()` on a CPU that `e->load_config()` already deleted. `I8255Window`'s 100 ms timer does the same with `d->get_value()`.

Fix: `delete w` (or stop the timers in `closeEvent`) instead of relying on deferred deletion.

## B. Threading: GUI thread against emulation and render threads

### 7. The disassembler and step-over read memory with the CPU's own side-effecting path — CONFIRMED (high)
`src/dialogs/disasmarea.cpp:74`, `:167`, `src/dialogs/debugwindow.cpp:230`, `src/emulator/core.cpp:2323-2339`, `src/emulator/devices/cpu/k1801vm1.cpp:229-233`, `:277-283`

`cpu->read_mem()` is `mm->read()`, not `get_direct()`. Called from the GUI thread it:
- flips `cancelinit` (`first_range = 1; invalidate_pages()`) when the viewed address matches the mask, i.e. the debugger unmaps the boot ROM;
- performs real I/O reads: port `access` strobes, read-to-clear registers, Agat `C0xx` soft switches; scrolling the listing through the I/O page changes machine state;
- writes `last_device` and `no_device`, and fills the page cache (`find_page`/`fill_page`) concurrently with the emulation thread. A half-written cache entry (new tag, old device pointer) sends a guest access to the wrong device. `k1801vm1::read_mem` then runs `note_timeout()` off `mm->no_device`, which the GUI read may just have set.

This happens while the machine runs: `DebugWindow::track()` repaints the code view every 200 ms in `DEBUG_BRAKES`, and `area_crc16()` re-reads the whole disassembled range.

CLAUDE.md already states the rule for the dump window ("a register whose read does something must override `get_direct()`"); the disassembler was never moved to it. Fix: read through `cpu->mm->get_direct()`.

`I8255Window::update()` polls `d->get_value(0..3)` every 100 ms instead of `get_direct` (`i8255window.cpp`, `update()`); same rule.

### 8. Disk, tape, option and reset actions mutate devices on the GUI thread with no synchronisation — CONFIRMED mechanism (high)
- `FDD::load_image()` / `unload()` do `delete[] buffer; buffer = new ...` (`src/emulator/devices/common/fdd.cpp:150-151`, `:459-462`), called from `mainwindow.cpp:1597`, `:1613` while the controller on the emulation thread indexes `buffer`. Swapping a disk while the drive LED is on (a "insert disk 2" prompt polling the drive) is a read, or a write, of freed heap.
- `TapeRecorderWindow` calls `d->load_file()`, `play()`, `stop()`, `rewind()`, `get_save_data()` directly (`taperecorder.cpp:287`, `353-355`, `387`, `425`). There is no mutex anywhere in `tape.cpp`.
- The option combos call `dev->set_device_option()` from the lambda (`mainwindow.cpp:909`). For a `connector` that plugs or unplugs devices and cascades interface lines.
- `e->reset()` runs from the menu, Pause/Break and `wasm_reset` (`mainwindow.cpp:1203`, `emulator.cpp:1084`, `wasm_main.cpp:322-327`): every device's `reset()` plus `invalidate_pages()` mid-instruction.
- `cpus[i]->m_debug = ...` (`mainwindow.cpp:1229`, `debugwindow.cpp:143`, `173`, `194`, ...): `m_debug` is a plain `unsigned int` (`core.h:722`).
- `add_breakpoint` is `breakpoints[break_count++] = a` against `check_breakpoint` on the other thread (`core.cpp:1710-1723`).
- `DumpArea::editor_return_pressed` calls `d->set_value(..., true)` (`dumparea.cpp:190`).

The HDD already does this properly (`m_image_mutex`), and scripts and MCP do every one of these on the emulation thread. The mechanism to reuse exists: the `m_state_requested` / `ScriptEngine::tick()` pattern. One "run this on the emulation thread at the next instruction boundary" queue in `Emulator` (`post(std::function)`), used by all three frontends, removes the whole class. It would also let `record_command()` be derived from the same call.

### 9. `TapeRecorder::on_mode_changed` is a `std::function` written on one thread and called on another — CONFIRMED (medium)
`src/dialogs/taperecorder.cpp:111-115`, `:443`, `src/emulator/devices/common/tape.cpp:512`, `src/wasm/wasm_main.cpp:90`

`closeEvent` sets it to `nullptr` while the emulation thread may be inside `if (on_mode_changed) on_mode_changed(...)`. That is a torn `std::function` or a call into a destroyed lambda capturing a window that is about to be deleted. The window is widest when the machine toggles the motor line often. Needs a mutex, or the posted-call queue from 8.

### 10. Save state freezes the GUI for up to 5 s, then reports the wrong thing — CONFIRMED (medium)
`src/mainwindow.cpp:1503-1515`

After `request_state()` the GUI thread does `for (i < 500 && pending) QThread::msleep(10)`. A snapshot bundles media raw, and an `uknc-hdd` image is hundreds of MB. Once the write exceeds 5 s the user gets "The machine is not running, so its state cannot be taken" while the file is still being written, and its real result is never reported. The request is not cancelled either, so after the emulation thread has died the flag stays up.

Fix: poll from the existing `rec_timer` and report when `state_pending()` drops.

### 11. The GUI-thread screenshot races the render thread's resize — CONFIRMED mechanism (low-medium)
`src/mainwindow.cpp:1797`, `src/renderers/renderer_opengl.h:66-74`, `:89-98`, `renderer_qt.h:124-133`, `:182-193`

`renderer->get_screenshot()` runs on the GUI thread without `lock_surface()`. The render thread's `resize()` deletes `surface` under that lock. A screenshot during a video-mode switch reads a freed `QImage`. `store_screenshot()` on the render thread is the safe path; the menu action could call `request_screenshot("")` and then `take_screenshot_png()`.

Separately: `sx, sy` come from `display` and the pixels from the renderer, so a size mismatch makes `lodepng::encode` fail silently, and `lodepng::save_file`'s return value is ignored (`mainwindow.cpp:1812-1813`).

### 12. Failures on the emulation thread are silent or fatal — CONFIRMED
- `src/emulator/emulator.cpp:662`, `:669`: `get_device_by_name("display"/"keyboard")` with `required = true` runs outside the `try` at `:755`. A config lacking either terminates the process. In the GUI `init_video` throws first, see 4.
- `:757-760`: any `std::exception` from `timer_proc` sets `m_running = false` and nothing else. The window freezes with no message. Headless then sits forever in `while (!emulator.script_finished())` (`headless_main.cpp:899`).

Store the text in `dm->error_message` and surface it from `rec_tick`.

### 13. `MainWindow::e` after `closeEvent()` — mostly handled
The null checks in `keyPress`/`keyRelease`/`eventFilter`/`changeEvent`/`paintEvent`/`mouse_flush` cover the events that arrive. One hole remains under MCP (PLAUSIBLE, narrow): a `BlockingQueuedConnection` `gui_load_machine` already queued when the window closes runs `mcp_load_config()` → `load_config()` → `e->loaded` with `e == nullptr` (`mcp_bridge.cpp:98-102`, `mainwindow.cpp:2308-2315`, `:1285`). `McpBridge::m_window` is never cleared, so its null checks are dead code. A `if (e == nullptr) return tr(...)` at the top of `mcp_load_config` closes it.

### 14. `thread_compat.h` — correct, but not the abstraction CLAUDE.md describes
- `EmuThread` exists only in the QThread variant (`thread_compat.h:33-59`). The std variant is a raw `std::thread` behind `#if USE_QT_THREADING` in `emulator.h:90-96`, `emulator.cpp:616-620`, `713-742`, `764-797`, `809-828` and `mcp_bridge.*`. CLAUDE.md's "use `EmuThread` instead of raw `std::thread`" is not what the code does. A 15-line std `EmuThread` with `create()`/`join()` would delete about ten `#if` blocks.
- Join and lifetime are right: `stop_emulation()` joins both threads before `renderer->stop()`, and the frontends always stop before `load_config`. `compat_lock_guard` is fine for both mutex types.
- `compat_now_ms()` in the Qt variant has an unsynchronised `if (!timer.isValid()) timer.start()` (`:73-74`). It is racy on a first call from two threads; XP kit only.
- `setThreadPriority` ignores the result of `pthread_setschedparam(SCHED_FIFO)` (`emulator.cpp:1260`). On Linux without `CAP_SYS_NICE` it fails silently and the thread stays at normal priority.
- The render thread busy-spins on `yield()` until `m_ready` (`emulator.cpp:770-776`). Harmless.
- `QtRenderer::render()` reads `widget->width()/height()` from the render thread (`renderer_qt.h:141-142`), a formally racy read of widget geometry. The GL widget posts everything correctly.

Queued connections across threads are otherwise right: `GLWidget` and `QtRenderWidget` post with `Qt::QueuedConnection`, and `McpBridge` picks Direct or BlockingQueued by thread.

## C. Presentation path

### 15. 50 fps pacing by `sleep_for(20 - elapsed)` — PLAUSIBLE (medium, needs a frame count)
`src/emulator/emulator.cpp:784-796`; no `timeBeginPeriod` anywhere in the project's own code.

At the ~15 ms default granularity CLAUDE.md itself cites for the emulation loop, a 20 ms sleep wakes on the second tick (~31 ms), i.e. about 32 frames a second on the Windows builds, not 50. The emulation loop avoids this by spinning; the render thread does not. A 1 ms timer period for the render thread only, or a waitable high-resolution timer, fixes it without touching the emulation loop.

The SDL2 renderer is created without `SDL_RENDERER_PRESENTVSYNC` (`renderer_sdl2.h:44`), so it can tear. The GL and Qt paths are composited and do not tear, but 50 Hz on a 60 Hz display judders. That is inherent.

### 16. OpenGL: a new texture object and two full-frame copies per frame — CONFIRMED cost (medium-low)
`src/renderers/GLWidget.cpp:82-96`

Every frame: `pendingImage.mirrored()` (copy 1), `new QOpenGLTexture(QImage)` (it converts to RGBA8888, copy 2, and allocates new GL storage), `delete` of the old texture. All under the mutex the render thread needs for the next `updateTexture`.

Cheaper: one persistent texture, `glTexSubImage2D` with `GL_BGRA`, and flip the V coordinate in the quad instead of mirroring.

Robustness: `program->link()` and shader compile results are never checked (`:47-49`), and the fragment shader has no `precision` qualifier (`:23-29`). On a GLES context (ANGLE, EGL) it fails to compile and the screen stays black with no message (PLAUSIBLE). There is no fallback when GL is unavailable (RDP, a VM without GL 2).

### 17. `MainWindow::paintEvent` re-renders the whole machine screen on the GUI thread — CONFIRMED (medium-low)
`src/mainwindow.cpp:1195-1199` → `Emulator::resize_screen()` → `GenericDisplay::validate(true)` → `render_all(true)` under the surface lock (`core.cpp:2676-2691`).

Every main-window paint (a resize drag, an expose) does a full software render on the GUI thread and contends the mutex with the render thread. Setting `was_updated = true` would do; the render thread already redraws.

### 18. `QImage` sharing between renderer and widget is safe today but rests on an unwritten rule — CONFIRMED
`renderer_opengl.h:51-54`, `:61-64`, `:79-82`

`render()` hands `*surface` to the widget by implicit sharing while the emulation thread keeps writing through the raw `render_pixels` pointer. Any non-const call on `*surface` while the widget holds the copy detaches and strands `render_pixels`. `fill()` is exactly that (it also ignores its colour argument in both Qt back ends), and CLAUDE.md records the resulting heap corruption.

Structural fix: the renderer owns a plain pixel buffer and gives the widget a deep copy (or uploads directly). The hazard disappears, and so do the `constBits()` comments.

### 19. SDL2 back end — CONFIRMED (legacy kits only)
- `SDL_CreateTextureFromSurface` + `SDL_DestroyTexture` every frame (`renderer_sdl2.h:180-193`).
- `recreate_renderer()` runs on the render thread while the first renderer was created on the GUI thread (`:149-153`, `:80`). SDL requires one thread for both.
- `get_screenshot()` leaks the `SDL_RWops` on every call (`:217-219`, `freedst = 0`, never closed) and writes `screen_x`/`screen_y` through a cast reference (`:220`).
- `resize()` uses `SDLRendererRef` without a null check (`:135`).

### 20. GUI polling — CONFIRMED, low
`update_fdds()` builds a `QIcon` from a resource path and calls `setIcon` for every drive every 100 ms whether or not anything changed (`mainwindow.cpp:1740-1765`). Add `rec_timer` 100 ms (always on), the keyboard window at 40 ms, the debugger at 200 ms, `I8255Window` at 100 ms. None is expensive; all would be "set on change" with cached icons.

## D. Qt, library and platform hazards

### 21. Non-ASCII paths on Windows — CONFIRMED for lodepng, PLAUSIBLE for the rest
- `lodepng::save_file` uses `fopen` (`src/libs/lodepng/lodepng.cpp:396`) on a UTF-8 path (`mainwindow.cpp:1813`, `emulator.cpp:1055`). A screenshot, or a script `SCREEN`, into a directory with Cyrillic in its name fails or creates a mojibake-named file, and the GUI ignores the return code. The rest of the core goes through `dsk_tools::UTF8_ifstream` or `_wfopen`, so this is the odd one out. `QFile` plus `lodepng::encode` in the GUI is a two-line fix.
- The mINI backend builds `std::filesystem::path` from a UTF-8 `std::string` (`ini_wrapper.cpp:69`, `75`, `140`; `ini.h:384-386`). As far as I know libstdc++ reads that as UTF-8 on Windows and MSVC reads it as the ANSI code page. If so, the MSVC kit (`build-win-msvc-latest.bat`) installed under a non-ASCII path fails to read the ini, `open()` retries 40 × 5 ms and sets `loaded = false`, and settings are never saved. Needs one run on MSVC.
- `headless_main.cpp:629`, `:836`: `argv` is ANSI and is fed to `fs::path`. Non-ASCII script or config arguments are mis-decoded (PLAUSIBLE).

### 22. QSettings and mINI backends diverge — CONFIRMED by reading
The XP/Win7 kits use QSettings and everything else uses mINI (`CMakeLists.txt:99-103`). The mINI side has the atomic replace, the retry, the `loaded` guard and quote stripping. The QSettings side is a bare `sync()` (`ini_wrapper.cpp:193-220`), and QSettings' INI format escapes backslashes, commas and non-Latin1 differently. A `last_path` or `[DeviceOptions]` written by one kit can read back differently in the other.

### 23. Small leaks and fixed arrays — CONFIRMED, low
- `DebugWindow::disasm` is `new`ed and never deleted (`debugwindow.cpp:48-50`, destructor `:104-108`).
- `ToolButtonProxy` has no parent and `setStyle` does not take ownership (`taperecorder.cpp:65`).
- `OpenConfigWindow::list_machines` creates a parentless `QStandardItemModel`; the last one leaks (`openconfigwindow.cpp:121`).
- `CreateScreenMenu()` adds new `QActionGroup`s to the same menus on every language switch (`mainwindow.cpp:574`, `598`, `613`, `635`).
- `DWM` and `renderer` are never freed (`mainwindow.cpp:307`, `326-330`).
- `fdd_button[8]` / `fdd_menu[8]` are not clamped the way `hdd_button[4]` is (`mainwindow.cpp:783-790` vs `:812-813`).

### 24. Emscripten — CONFIRMED by reading
- **Blocking on the main thread.** `wasm_load_machine()` runs on the browser main thread and calls `stop_emulation()`, which joins two pthreads (`wasm_main.cpp:72-73`). This works only because Emscripten spins the main thread and services proxied calls meanwhile.
- **Per-frame cost.** `render()` does a synchronous `MAIN_THREAD_EM_ASM` while holding `render_mutex`, allocating a fresh `ImageData` each time (`renderer_wasm.cpp:115-131`). That is 50 synchronous proxies a second, and `emulator.cpp:979-983` renders every frame in the wasm build even when nothing changed. A persistent `ImageData` plus the `was_updated` test (or an OffscreenCanvas owned by the render worker) removes most of it.
- **Memory.** `-sALLOW_MEMORY_GROWTH=1` with pthreads and no `INITIAL_MEMORY` (`wasm/CMakeLists.txt:27-40`) puts growable-heap checks on every JS heap access. Sizing the memory up front would let growth go.
- **Exported API.** Returning `static std::string::c_str()` is fine under `ccall`. `wasm_reset` does not check `loaded`, so before the first machine (`dm == nullptr`) it traps the module (`wasm_main.cpp:322-327`). `wasm_tape_save` reads a vector the emulation thread appends to (`:551-557`).
- The page checks `crossOriginIsolated` and reports it properly (`ecat_wasm.js:2676`), and no page code touches `HEAPU8` directly. Both good.

### 25. Minor
- `main.cpp:24` forces `QT_QPA_PLATFORM=xcb` unconditionally, overriding the user's environment. A Wayland-only session without XWayland cannot start.
- `QT_VERSION` guards are consistently applied. `DumpArea` re-measures `char_width` only for Qt ≥ 6.3 (`dumparea.cpp:32-34`) and otherwise relies on `DOSFrame`; harmless.

## E. Build system

### 26. `if(USE_MINI_INI)` tests a variable that is never set; the "C++11 baseline" is not what Qt6 builds compile with — CONFIRMED
`src/CMakeLists.txt:95-103`, `:177-179`

`add_compile_definitions(USE_MINI_INI)` does not create a CMake variable, so the `-std=c++17` property on `ini_wrapper.cpp` is dead code. It is also GCC-only syntax. The builds work only because Qt6's targets raise the whole target to C++17 (and `mainwindow.cpp:107`, `117` use `std::filesystem` directly). Consequences:
- `CMAKE_CXX_STANDARD 11` constrains nothing on any Qt6 kit. C++14/17 slipping into the core is caught only when someone builds the XP/Win7 kit.
- Qt 5.15 with GCC ≥ 9 (the "Qt 5.15 + MinGW 13" combination) defines `USE_MINI_INI` but compiles at gnu++11, so `<filesystem>` fails.

Fix: a real `set(USE_MINI_INI ON)` plus `target_compile_features(... cxx_std_17)`, and a small OBJECT library of `EMULATOR_SOURCES` pinned to C++11 so the baseline is enforced by a compiler. The object library would also stop the GUI and headless targets compiling the core twice.

### 27. Renderer selection is unvalidated — CONFIRMED
`src/CMakeLists.txt:48-77`, `:118-124`
- A non-Debug configure without `-DRENDERER_x` defines nothing and fails deep in `mainwindow.cpp` (`nativeView` undeclared at `:1353`) instead of at configure time.
- Two renderers ON silently picks SDL2, by `#elif` order.
- In Debug the `option()`s are never declared, and the Debug auto-pick never runs on multi-config generators (MSVC, Xcode), where `CMAKE_BUILD_TYPE` is empty.
- `OPENGL_FILES` is set only for Qt6, so Qt5 with `RENDERER_OPENGL` ends in a link error.

One `set(ECAT_RENDERER OPENGL CACHE STRING ...)` with `STRINGS` and a `FATAL_ERROR` on anything else replaces all of it.

### 28. No warning flags; sanitizer and LTO builds are not provided — CONFIRMED
No `-Wall -Wextra`, `/W4`, `-fsanitize` or `INTERPROCEDURAL_OPTIMIZATION` appears in `src/CMakeLists.txt`, `src/wasm/CMakeLists.txt` or `.build/*`.
- Several findings above are warning-class: unused results (`error` in `renderer_sdl2.h:220`, `value` in `mmwindow.cpp`), signed/unsigned loops over `fdds_found`.
- Sanitizers are possible by hand through `CMAKE_CXX_FLAGS`, but line 6 forces the static MSVC runtime for every non-Debug configuration, which is awkward for MSVC ASan.
- A TSan run of the headless build on Linux would flag items 7-9 straight away. It is the cheapest verification available for section B.

### 29. Smaller build items — CONFIRMED
- `configure_file(globals.h.in ${CMAKE_CURRENT_SOURCE_DIR}/globals.h)` (`:553`) writes into the source tree from every build directory. The wasm list works around it ("binary dir first so generated globals.h takes precedence", `wasm/CMakeLists.txt:164`). Generate into `CMAKE_CURRENT_BINARY_DIR` everywhere.
- Qt5 path: `find_program(LRELEASE_EXECUTABLE lrelease)` is unchecked and `lrelease` runs via `execute_process` at configure time (`:420-429`). Editing a `.ts` does not rebuild the `.qm`, and a missing tool surfaces as an rcc error.
- `CMAKE_OSX_ARCHITECTURES` and `CMAKE_OSX_DEPLOYMENT_TARGET` are `FORCE`d after `project()` and after `add_subdirectory(dsk_tools)` (`:523-524`). This works only because `build-macos.sh` also passes the architectures on the command line; it blocks a single-arch developer build.
- `${CMAKE_SOURCE_DIR}` is used for the dsk_tools paths (`:163`, `:467`), so the project cannot be consumed as a subdirectory.
- The release scripts have been reworked and are reasonable: they always reconfigure, pass `Release`, recreate the release directories and read one `VERSION` file. What is left is hard-coded tool paths (`build-linux.sh` `QT_PATH`, linuxdeployqt in `~/Downloads`; `build-macos.sh` `QT_PATH`) and no codesign or notarisation step for the `.dmg`.
- The two source lists are in sync today. I diffed them; the only difference is `audio_driver_sdl.cpp`, correctly absent from wasm. A shared `emulator_sources.cmake` `include()`d by both removes the hazard.

## F. Architecture

### 30. Core/frontend boundary — mixed
**Reaching into internals.**
- `Emulator::dm` is public and the GUI walks it (`mainwindow.cpp:729-735`, `783`, `860-862`).
- The GUI writes `cpu->m_debug` directly (`:1229`), reads `FDD::file_name/files/files_save` fields, and `dynamic_cast`s to `FDD`, `TapeRecorder`, `Keyboard`, `Memory`, `I8255`, `MemoryMapper`.
- `src/emulator/debug.h` sits in the "Qt-free" directory, is listed in `EMULATOR_SOURCES`, and includes `dialogs/genericdbgwnd.h` (`QDialog`). It belongs in `src/dialogs/`.

**Naming types instead of discovering capabilities.**
- The debug-window registry is 15 type strings (`mainwindow.cpp:309-323`).
- `DebugWindow` picks the `.dis` file and the disassembler class by `d->type` (`debugwindow.cpp:33-50`) and finds memories by `"ram"`/`"rom"` (`:78`).
- A new CPU needs edits in three GUI places, as CLAUDE.md's own "Add a new CPU" list admits.

**The good pattern already exists.** Hard disks are driven purely through `get_field`/`send_command` (`mainwindow.cpp:1630-1691`), options through `get_device_options()`, the keyboard window through `Keyboard`'s public query API. Moving FDD and tape to the same command/field surface, and adding something like `ComputerDevice::debug_view()` and `CPU::disasm_table()`, would finish the job. It dovetails with the posted-call queue of item 8, since commands are already what scripts execute on the emulation thread.

### 31. Logic repeated across GUI, headless and wasm — CONFIRMED
| What | GUI | Headless | Wasm |
|---|---|---|---|
| Root/ini path resolution and ini seeding | `mainwindow.cpp:88-148` | `headless_main.cpp:625-681` | ini text assembled in `ecat_wasm.js:2652-2668` |
| `resolve_startup_path` | `:363-374` | `:685-692` | — |
| Machine bring-up (stop script → stop → load → volume/mute → `init_video` → `run` → embedded script) | `:1283-1385` | `:710-740`, `:880-896` | `wasm_main.cpp:62-113` |
| Script-before-machine (`MACHINE` line) | `:391-413` | `:833-844` | — |
| Tape format lookup in `[TapeFiles]` (machine-specific, then generic) | `taperecorder.cpp:274-276` | — | `wasm_main.cpp:512-514` |
| Tape follows the motor line | window | — | `wasm_main.cpp:87-96` |
| `[DeviceOptions]` key format | `mainwindow.cpp:910` | core `emulator.cpp:441` | — |
| `md2html` | `qt_utils.cpp:80` | — | `wasm_main.cpp:374-386` |

Natural homes: `Emulator::start_machine(file, surface, flags)`, returning a `Result` and covering the whole bring-up; a Qt-free `HostPaths resolve_host_paths(argv0, cwd)`; `TapeRecorder::load_file_auto(path)`; and `set_device_option` persisting its own ini key. `McpHost::load_machine` would then be the same call in all three.

### 32. `mainwindow.cpp` — yes, a god object (2328 lines, about 60 members)
Natural split along seams that already exist in the file:
1. Startup paths and ini (→ core, see 31).
2. `MachineController`: `load_config`, title, volume/mute restore.
3. `MediaToolBar`: FDD/HDD/tape/keyboard buttons and option combos (`:480-549`, `:739-937`, `:1587-1765`, about 550 lines).
4. `MouseCapture`: `:998-1188`, self-contained.
5. `RecordingController`: `:1917-2276`, about 360 lines with its own state machine.
6. The screen and settings menus (`:556-723`).
7. The MCP glue.

Items 3-5 depend only on `Emulator*` and a few actions, so they can move out mechanically. Item 1 (fixing the failed-load path) is much easier once 3 owns its own "machine gone" reset.

### 33. Three compile-time renderers — the abstraction costs more than it returns
- `VideoRenderer` is a sound interface, and `NullRenderer`/`WasmRenderer` prove it.
- The cost is on the desktop side: eight `#ifdef RENDERER_*` blocks in `mainwindow.cpp`, `void *p` plus `reinterpret_cast` for the widget, three release artifacts per platform, and no fallback when GL fails.
- `renderer_qt.h` and `renderer_opengl.h` duplicate the surface code almost line for line (`resize`, `fill`, `get_buffer`, `get_screenshot`). They differ only in how the frame is presented.

Suggestion: one `QImageSurfaceRenderer` with a runtime-chosen presenter widget (GL, falling back to the painter when context creation or shader link fails). Keep SDL2 as the compile-time option for the XP/Win7 kits only. That also lifts the "windowed MCP requires OpenGL" restriction (`CMakeLists.txt:143-145`).

## Overall assessment

The newer layers are careful. The script engine has a lock-free fast path; the save-state and screenshot request flags, the mouse atomics, the UKNC keyboard queue, the surface lock and the GL widget's queued setters are all done properly. The release scripts and the wasm page's failure reporting are in good shape, and the two source lists are currently in sync.

The weak area is the older GUI layer.

1. **Debug widgets.** Three unchecked fixed arrays or unguarded inputs (items 2, 3, 5), and a disassembler that reads through the CPU's side-effecting path (7). These are ordinary bugs with short fixes.
2. **Exceptions and failed loads.** There is no top-level exception guard, so a missing device name kills the process; the Tape menu on an Agat is the easy reproduction (4). The failed-load path leaves the GUI pointing at freed devices (1, 6).
3. **GUI-thread device mutation.** Disk, tape, option and reset actions call devices directly while the machine runs (8, 9). One posted-call queue on the emulation thread, which scripts and MCP effectively already have, removes the class and also pulls the GUI towards the command/field surface the HDD code uses.

On the build side, the C++11 baseline is declared but not enforced on any Qt6 kit (26), renderer selection fails late (27), and no warnings are enabled (28). A `-Wall -Wextra` pass and one TSan run of the headless build would independently surface a good part of sections A and B.

Presentation is functionally fine. The one item worth measuring is the render thread's `sleep_for(20 ms)` on Windows (15), which I expect to give about 32 fps rather than 50.
