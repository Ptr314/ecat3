// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: WASM JavaScript bridge - keyboard, machine loading, file I/O

"use strict";

// ============================================================================
// EmuKey constants (matching Qt::Key values used by the emulator core)
// ============================================================================

const EmuKey = {
    Space:        0x20,
    Key_0:        0x30,  Key_1: 0x31, Key_2: 0x32, Key_3: 0x33, Key_4: 0x34,
    Key_5:        0x35,  Key_6: 0x36, Key_7: 0x37, Key_8: 0x38, Key_9: 0x39,
    Key_A:        0x41,  Key_B: 0x42, Key_C: 0x43, Key_D: 0x44, Key_E: 0x45,
    Key_F:        0x46,  Key_G: 0x47, Key_H: 0x48, Key_I: 0x49, Key_J: 0x4a,
    Key_K:        0x4b,  Key_L: 0x4c, Key_M: 0x4d, Key_N: 0x4e, Key_O: 0x4f,
    Key_P:        0x50,  Key_Q: 0x51, Key_R: 0x52, Key_S: 0x53, Key_T: 0x54,
    Key_U:        0x55,  Key_V: 0x56, Key_W: 0x57, Key_X: 0x58, Key_Y: 0x59,
    Key_Z:        0x5a,
    Exclam:       0x21, QuoteDbl: 0x22, NumberSign: 0x23, Dollar: 0x24,
    Percent:      0x25, Ampersand: 0x26, Apostrophe: 0x27,
    ParenLeft:    0x28, ParenRight: 0x29, Asterisk: 0x2a, Plus: 0x2b,
    Comma:        0x2c, Minus: 0x2d, Period: 0x2e, Slash: 0x2f,
    Colon:        0x3a, Semicolon: 0x3b, Less: 0x3c, Equal: 0x3d,
    Greater:      0x3e, Question: 0x3f, At: 0x40,
    BracketLeft:  0x5b, Backslash: 0x5c, BracketRight: 0x5d,
    AsciiCircum:  0x5e, Underscore: 0x5f, QuoteLeft: 0x60,
    BraceLeft:    0x7b, Bar: 0x7c, BraceRight: 0x7d, AsciiTilde: 0x7e,
    // Non-ASCII keys
    Escape:       0x01000000,
    Tab:          0x01000001,
    Backspace:    0x01000003,
    Return:       0x01000004,
    Enter:        0x01000005,
    Insert:       0x01000006,
    Delete:       0x01000007,
    Home:         0x01000010,
    End:          0x01000011,
    Left:         0x01000012,
    Up:           0x01000013,
    Right:        0x01000014,
    Down:         0x01000015,
    PageUp:       0x01000016,
    PageDown:     0x01000017,
    Shift:        0x01000020,
    Control:      0x01000021,
    Alt:          0x01000023,
    CapsLock:     0x01000024,
    NumLock:      0x01000025,
    ScrollLock:   0x01000026,
    F1:           0x01000030, F2: 0x01000031, F3: 0x01000032, F4: 0x01000033,
    F5:           0x01000034, F6: 0x01000035, F7: 0x01000036, F8: 0x01000037,
    F9:           0x01000038, F10: 0x01000039, F11: 0x0100003a, F12: 0x0100003b,
    Cancel:       0x01020001,
};

const EmuModifier = {
    NoModifier:      0x00000000,
    ShiftModifier:   0x02000000,
    ControlModifier: 0x04000000,
    AltModifier:     0x08000000,
};

// Map KeyboardEvent.code to EmuKey values
const CODE_TO_EMUKEY = {
    "Escape":       EmuKey.Escape,
    "Tab":          EmuKey.Tab,
    "Backspace":    EmuKey.Backspace,
    "Enter":        EmuKey.Return,
    "NumpadEnter":  EmuKey.Enter,
    "Insert":       EmuKey.Insert,
    "Delete":       EmuKey.Delete,
    "Home":         EmuKey.Home,
    "End":          EmuKey.End,
    "ArrowLeft":    EmuKey.Left,
    "ArrowUp":      EmuKey.Up,
    "ArrowRight":   EmuKey.Right,
    "ArrowDown":    EmuKey.Down,
    "PageUp":       EmuKey.PageUp,
    "PageDown":     EmuKey.PageDown,
    "ShiftLeft":    EmuKey.Shift,
    "ShiftRight":   EmuKey.Shift,
    "ControlLeft":  EmuKey.Control,
    "ControlRight": EmuKey.Control,
    "AltLeft":      EmuKey.Alt,
    "AltRight":     EmuKey.Alt,
    "CapsLock":     EmuKey.CapsLock,
    "NumLock":      EmuKey.NumLock,
    "ScrollLock":   EmuKey.ScrollLock,
    "F1":           EmuKey.F1,   "F2":  EmuKey.F2,  "F3":  EmuKey.F3,  "F4":  EmuKey.F4,
    "F5":           EmuKey.F5,   "F6":  EmuKey.F6,  "F7":  EmuKey.F7,  "F8":  EmuKey.F8,
    "F9":           EmuKey.F9,   "F10": EmuKey.F10,  "F11": EmuKey.F11,  "F12": EmuKey.F12,
    "Space":        EmuKey.Space,
    "Minus":        EmuKey.Minus,
    "Equal":        EmuKey.Equal,
    "BracketLeft":  EmuKey.BracketLeft,
    "BracketRight": EmuKey.BracketRight,
    "Backslash":    EmuKey.Backslash,
    "Semicolon":    EmuKey.Semicolon,
    "Quote":        EmuKey.Apostrophe,
    "Backquote":    EmuKey.QuoteLeft,
    "Comma":        EmuKey.Comma,
    "Period":       EmuKey.Period,
    "Slash":        EmuKey.Slash,
    // Letter keys
    "KeyA": EmuKey.Key_A, "KeyB": EmuKey.Key_B, "KeyC": EmuKey.Key_C,
    "KeyD": EmuKey.Key_D, "KeyE": EmuKey.Key_E, "KeyF": EmuKey.Key_F,
    "KeyG": EmuKey.Key_G, "KeyH": EmuKey.Key_H, "KeyI": EmuKey.Key_I,
    "KeyJ": EmuKey.Key_J, "KeyK": EmuKey.Key_K, "KeyL": EmuKey.Key_L,
    "KeyM": EmuKey.Key_M, "KeyN": EmuKey.Key_N, "KeyO": EmuKey.Key_O,
    "KeyP": EmuKey.Key_P, "KeyQ": EmuKey.Key_Q, "KeyR": EmuKey.Key_R,
    "KeyS": EmuKey.Key_S, "KeyT": EmuKey.Key_T, "KeyU": EmuKey.Key_U,
    "KeyV": EmuKey.Key_V, "KeyW": EmuKey.Key_W, "KeyX": EmuKey.Key_X,
    "KeyY": EmuKey.Key_Y, "KeyZ": EmuKey.Key_Z,
    // Digit keys
    "Digit0": EmuKey.Key_0, "Digit1": EmuKey.Key_1, "Digit2": EmuKey.Key_2,
    "Digit3": EmuKey.Key_3, "Digit4": EmuKey.Key_4, "Digit5": EmuKey.Key_5,
    "Digit6": EmuKey.Key_6, "Digit7": EmuKey.Key_7, "Digit8": EmuKey.Key_8,
    "Digit9": EmuKey.Key_9,
    // Numpad
    "Numpad0": EmuKey.Key_0, "Numpad1": EmuKey.Key_1, "Numpad2": EmuKey.Key_2,
    "Numpad3": EmuKey.Key_3, "Numpad4": EmuKey.Key_4, "Numpad5": EmuKey.Key_5,
    "Numpad6": EmuKey.Key_6, "Numpad7": EmuKey.Key_7, "Numpad8": EmuKey.Key_8,
    "Numpad9": EmuKey.Key_9,
    "NumpadMultiply":  EmuKey.Asterisk,
    "NumpadAdd":       EmuKey.Plus,
    "NumpadSubtract":  EmuKey.Minus,
    "NumpadDecimal":   EmuKey.Period,
    "NumpadDivide":    EmuKey.Slash,
    // Pause/Break -> Cancel (used for reset)
    "Pause":           EmuKey.Cancel,
};

// Keys that should be intercepted (prevent browser default action)
const INTERCEPT_KEYS = new Set([
    "Tab", "Escape", "Backspace", "Enter", "Space", "Pause",
    "ArrowLeft", "ArrowUp", "ArrowRight", "ArrowDown",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "Backquote", "Minus", "Equal",
    // Firefox opens its quick find on ' and / and takes the typing that follows
    "Quote", "Slash",
]);

// Keys whose character depends on the layout and on Shift. The desktop hands
// the core the character Qt reports for them - Shift+' is Key_QuoteDbl, Shift+1
// is Key_Exclam - and the keyboard maps of the machines are written that way:
// bk.map has "/S for the double quote and ' only unshifted, rk.kbd puts 2 and "
// on one contact. So these come from event.key. A character outside ASCII (a
// Cyrillic layout has э on the Quote key) falls back to the physical key, which
// is what the ЙЦУКЕН -> ЯВЕРТЬ remap of the core expects; letters stay physical
// for the same reason.
const CHARACTER_CODES = new Set([
    "Digit0", "Digit1", "Digit2", "Digit3", "Digit4",
    "Digit5", "Digit6", "Digit7", "Digit8", "Digit9",
    "Minus", "Equal", "BracketLeft", "BracketRight", "Backslash", "IntlBackslash",
    "Semicolon", "Quote", "Backquote", "Comma", "Period", "Slash",
]);

// ============================================================================
// Settings
// ============================================================================
//
// Kept in the browser, per site. Storage may be unavailable altogether (a
// private window, blocked site data), and then the page simply starts with its
// defaults every time.

const settings = {
    get(key, def) {
        try {
            const value = localStorage.getItem("ecat3." + key);
            return value === null ? def : value;
        } catch (e) {
            return def;
        }
    },
    set(key, value) {
        try {
            localStorage.setItem("ecat3." + key, String(value));
        } catch (e) {
            // Not remembered, nothing else is lost
        }
    },
    // A setting taken away goes back to the default of the page: that is how
    // the keyboard is put back to the size that follows the screen
    remove(key) {
        try {
            localStorage.removeItem("ecat3." + key);
        } catch (e) {
            // Nothing was kept there in the first place
        }
    },
};

function clamp(value, lo, hi) {
    return Math.min(hi, Math.max(lo, value));
}

// ============================================================================
// Page address
// ============================================================================
//
// index.html?machine=Agat-7&kbd=1&scale=250&kbdscale=120 opens the page in that
// state, and blocks=machine,-screen says which of the folding blocks are open.
// It holds for this visit only: nothing read from the address goes into
// the saved settings, so following somebody else's link leaves one's own
// choices as they were. A machine picked or a slider moved by hand is saved as
// usual. The link button next to the title builds such an address from what is
// on the screen.

const urlParams = readUrlParams();

function readUrlParams() {
    const q = new URLSearchParams(location.search);
    const number = (name) => {
        const value = parseInt(q.get(name), 10);
        return isNaN(value) ? null : value;
    };
    const kbd = q.get("kbd");
    return {
        machine:  q.get("machine") || null,
        keyboard: kbd !== null && !/^(0|false|no|off)$/i.test(kbd),
        scale:    number("scale"),
        // "auto" follows the width of the screen, like a size never picked
        kbdScale: /^auto$/i.test(q.get("kbdscale") || "") ? "auto" : number("kbdscale"),
        aspect:   aspectId(q.get("aspect") || ""),
        filter:   filterId(q.get("filter") || ""),
        blocks:   blockStates(q.get("blocks") || ""),
    };
}

// blocks=machine,-screen,fdd0: a name on its own is an open block, a name with
// a minus in front a folded one. The names are those of data-block in
// shell.html plus the device name of a drive or a tape block - the seven of
// the page (machine, screen, volume, language, file, keyboard, options) are
// therefore reserved. A block the address says nothing about is left to the
// choice saved in this browser
function blockStates(text) {
    const map = new Map();
    for (const item of text.split(",")) {
        const entry = item.trim();
        const name = entry.replace(/^-/, "");
        if (name) map.set(name, entry.startsWith("-"));
    }
    return map;
}

// The filtering modes of the screen: none, linear, sharp
function filterId(text) {
    const id = text.trim().toLowerCase();
    return ["none", "linear", "sharp"].includes(id) ? id : null;
}

// The aspect modes of the screen, as the list offers them: 4:3, a square
// screen, square pixels. The address takes aspect=4x3 or 4:3, square, 1x1 or
// 1:1; anything else - the 4:3* of an earlier version too - is ignored. The
// list lives inside: readUrlParams() calls this before a constant further
// down the file would exist
function aspectId(text) {
    const id = text.trim().toLowerCase().replace(":", "x");
    return ["4x3", "square", "1x1"].includes(id) ? id : null;
}

// A machine by its id in machines.json or by the name of its configuration
// file, with or without .cfg, in any case
function findMachine(machines, name) {
    const want = name.toLowerCase().replace(/\.cfg$/, "");
    const base = (m) => (m.cfg_path || "").split("/").pop().toLowerCase().replace(/\.cfg$/, "");
    return machines.find((m) => m.id === name)
        || machines.find((m) => m.id.toLowerCase() === want)
        || machines.find((m) => base(m) === want)
        || null;
}

// The address that opens the page the way it looks now
function currentPageUrl() {
    const q = new URLSearchParams();
    if (currentMachine) q.set("machine", currentMachine.id);
    q.set("scale", screenScale);
    q.set("aspect", aspectMode());
    q.set("filter", filterMode());
    // Only a machine with a drawing has a keyboard to speak of
    if (kbdBaseWidth > 0) {
        if (!document.getElementById("kbd-panel").hidden) q.set("kbd", "1");
        const chosen = kbdScaleChosen();
        q.set("kbdscale", chosen === null ? "auto" : chosen);
    }
    // Folded or not, for every block on the screen - those of the page and
    // those built for this machine alike
    const blocks = visibleBlocks().map((b) => (b.body.hidden ? "-" : "") + b.urlName);
    if (blocks.length) q.set("blocks", blocks.join(","));
    // A comma is allowed in a query string and keeps the address readable;
    // URLSearchParams escapes it all the same
    return location.origin + location.pathname + "?" + q.toString().replace(/%2C/g, ",");
}

async function copyText(text) {
    try {
        await navigator.clipboard.writeText(text);
        return true;
    } catch (e) {
        // No clipboard API or no permission for it: the old way, a selection
        const area = element("textarea", "", document.body);
        area.value = text;
        area.style.position = "fixed";
        area.style.opacity = "0";
        area.select();
        let ok = false;
        try { ok = document.execCommand("copy"); } catch (e2) { ok = false; }
        area.remove();
        return ok;
    }
}

function setupLinkButton() {
    const button = document.getElementById("btn-link");
    let timer = null;
    button.addEventListener("click", async () => {
        // A focused button would take Space and Enter away from the machine
        button.blur();
        const url = currentPageUrl();
        if (await copyText(url)) {
            setStatus("stLinkCopied", "success");
            // A check mark for a moment says that it worked
            button.classList.add("copied");
            clearTimeout(timer);
            timer = setTimeout(() => button.classList.remove("copied"), 1500);
        } else {
            setStatus("stLinkFailed", "error", url);
        }
    });
}

// A control that kept the focus would keep the typing away from the machine:
// the key handler leaves inputs and selects alone
function releaseFocus(el) {
    el.addEventListener("change", () => el.blur());
}

// A slider paints its filled part from --fill: WebKit draws none of its own,
// so the position of the thumb is handed to the stylesheet. Every change by
// hand comes through the listener below, a change from the script calls this
function paintRange(el) {
    const min = parseFloat(el.min) || 0;
    const max = el.max === "" ? 100 : parseFloat(el.max);
    const pct = max > min ? (parseFloat(el.value) - min) / (max - min) * 100 : 0;
    el.style.setProperty("--fill", pct + "%");
}

document.addEventListener("input", (e) => {
    if (e.target.type === "range") paintRange(e.target);
});

function element(tag, className, parent) {
    const el = document.createElement(tag);
    if (className) el.className = className;
    if (parent) parent.appendChild(el);
    return el;
}

// Hands bytes to the browser as a download
function downloadFile(fileName, data) {
    const url = URL.createObjectURL(new Blob([data], { type: "application/octet-stream" }));
    const a = element("a", "", document.body);
    a.href = url;
    a.download = fileName;
    a.click();
    a.remove();
    // Not revoked at once: a browser may start the download after click() returns
    setTimeout(() => URL.revokeObjectURL(url), 10000);
}

// ============================================================================
// Interface language
// ============================================================================

const I18N = {
    en: {
        title:              "eCat3 - Retro Computer Emulator",
        machinesLoading:    "Loading machines...",
        selectMachine:      "-- Select a machine --",
        power:              "Power",
        powerTitle:         "Cold reset (power cycle)",
        reset:              "Reset",
        resetTitle:         "Warm reset",
        scale:              "Screen scale",
        volume:             "Volume",
        language:           "Language",
        keyboard:           "Keyboard",
        keyboardTitle:      "On-screen keyboard",
        kbdSize:            "Keyboard size",
        percent:            "{0}%",
        kbdPercentAuto:     "{0}% (auto)",
        kbdAutoTitle:       "Size by the width of the screen",
        drive:              "Drive {0}",
        noDisk:             "No disk",
        load:               "Load",
        save:               "Save",
        eject:              "Eject",
        protect:            "Write protect",
        openFile:           "Load a file",
        openFileTitle:      "Load a program straight into the memory of the machine",
        stFileLoaded:       "File loaded: {0}",
        stFileFailed:       "Failed to load {0}: {1}",
        stInit:             "Initializing...",
        stListLoading:      "Loading machine list...",
        stListFailed:       "Failed to load machines.json: {0}",
        stWasmLoading:      "Loading WASM module...",
        stWasmFailed:       "Failed to load WASM: {0}",
        stWasmWaiting:      "Loading WASM module... still waiting after {0} s: {1}",
        stNotSecure:       "The emulator cannot start at {0}: the page is not opened over HTTPS (or from localhost), and without that the browser gives threads no shared memory",
        stNotIsolated:      "The emulator cannot start: the server does not send the Cross-Origin-Opener-Policy and Cross-Origin-Embedder-Policy headers, and without them the browser gives threads no shared memory",
        stReady:            "Ready. Select a machine to start.",
        stAssets:           "Loading machine assets...",
        stStarting:         "Starting emulation...",
        stMachineFailed:    "Failed to load machine (error {0})",
        stRunning:          "Running",
        stError:            "Error: {0}",
        stDiskLoading:      "Loading disk image: {0}",
        stDiskLoaded:       "Disk image loaded: {0}",
        stDiskFailed:       "Failed to load disk image (error {0})",
        stDiskSaved:        "Disk image saved: {0}",
        stSaveFailed:       "Failed to save disk image (error {0})",
        stSaveUnsupported:  "This drive cannot save its image as {0}",
        tape:               "Tape recorder",
        tapeEmpty:          "No tape",
        tapeRecording:      "Recording",
        bytes:              "{0} B",
        tapeLoad:           "Load a tape file",
        tapeRewind:         "Rewind to the beginning",
        tapePlay:           "Play",
        tapePause:          "Pause",
        tapeRecord:         "Record what the machine writes",
        tapeRecordStop:     "Stop recording and save the file",
        tapeMute:           "Mute the tape",
        tapeUnmute:         "Unmute the tape",
        stTapeLoaded:       "Tape loaded: {0}",
        stTapeUnknown:      "Unknown tape file format: {0}",
        stTapeFailed:       "Failed to load the tape (error {0})",
        stTapeSaved:        "Recording saved: {0}",
        stTapeNothing:      "Nothing has been recorded",
        stTapeSaveFailed:   "Failed to save the recording (error {0})",
        collapse:           "Collapse",
        expand:             "Expand",
        repoTitle:          "Source code on GitHub",
        emuverseTitle:      "Emuverse.ru, the encyclopedia of emulation (in Russian)",
        infoTitle:          "About this configuration",
        close:              "Close",
        infoMissing:        "There is no description for this configuration.",
        linkTitle:          "Copy a link to this machine with the current settings",
        stLinkCopied:       "Link copied to the clipboard",
        stLinkFailed:       "Could not copy the link: {0}",
        stUrlMachine:       "Unknown machine in the address: {0}",
        clickToStart:       "Click or press a key to start",
        aspectTitle:        "Shape of the screen. 4:3 - as on a TV set; Square screen - width equal to height; Square pixels - every pixel of the raster a square of whole screen pixels",
        aspectSquareScreen: "Square screen",
        aspectSquarePixels: "Square pixels",
        filterTitle:        "Filtering when scaling. None - sharp, columns may come out uneven at a fractional scale; Linear - even but soft; Sharp - the whole part of the scale without filtering, only the fraction smoothed",
        filterNone:         "None",
        filterLinear:       "Linear",
        filterSharp:        "Sharp",
        mouseSpeed:         "Mouse speed",
        blockMachine:       "Machine",
        blockScreen:        "Screen",
        blockFile:          "File",
        blockDevices:       "Devices",
        stMouseCaptured:    "The mouse is captured by the machine. Press Esc, Ctrl-Alt or the middle button to release it",
    },
    ru: {
        title:              "eCat3 — эмулятор ретрокомпьютеров",
        machinesLoading:    "Загрузка списка машин...",
        selectMachine:      "-- Выберите машину --",
        power:              "Питание",
        powerTitle:         "Холодный сброс (выключить и включить)",
        reset:              "Сброс",
        resetTitle:         "Тёплый сброс",
        scale:              "Масштаб экрана",
        volume:             "Громкость",
        language:           "Язык",
        keyboard:           "Клавиатура",
        keyboardTitle:      "Экранная клавиатура",
        kbdSize:            "Размер клавиатуры",
        percent:            "{0}%",
        kbdPercentAuto:     "{0}% (авто)",
        kbdAutoTitle:       "Размер по ширине экрана",
        drive:              "Дисковод {0}",
        noDisk:             "Нет диска",
        load:               "Загрузить",
        save:               "Сохранить",
        eject:              "Извлечь",
        protect:            "Защита от записи",
        openFile:           "Загрузить файл",
        openFileTitle:      "Загрузить программу прямо в память машины",
        stFileLoaded:       "Файл загружен: {0}",
        stFileFailed:       "Не удалось загрузить {0}: {1}",
        stInit:             "Инициализация...",
        stListLoading:      "Загрузка списка машин...",
        stListFailed:       "Не удалось загрузить machines.json: {0}",
        stWasmLoading:      "Загрузка модуля WASM...",
        stWasmFailed:       "Не удалось загрузить WASM: {0}",
        stWasmWaiting:      "Загрузка модуля WASM... ждём уже {0} с: {1}",
        stNotSecure:       "Эмулятор не запустится по адресу {0}: страница открыта не по HTTPS (и не с localhost), а без этого браузер не даёт потокам общей памяти",
        stNotIsolated:      "Эмулятор не запустится: сервер не отдаёт заголовки Cross-Origin-Opener-Policy и Cross-Origin-Embedder-Policy, а без них браузер не даёт потокам общей памяти",
        stReady:            "Готово. Выберите машину.",
        stAssets:           "Загрузка файлов машины...",
        stStarting:         "Запуск эмуляции...",
        stMachineFailed:    "Не удалось загрузить машину (ошибка {0})",
        stRunning:          "Работает",
        stError:            "Ошибка: {0}",
        stDiskLoading:      "Загрузка образа диска: {0}",
        stDiskLoaded:       "Образ диска загружен: {0}",
        stDiskFailed:       "Не удалось загрузить образ диска (ошибка {0})",
        stDiskSaved:        "Образ диска сохранён: {0}",
        stSaveFailed:       "Не удалось сохранить образ диска (ошибка {0})",
        stSaveUnsupported:  "Этот дисковод не умеет сохранять образ в формате {0}",
        tape:               "Магнитофон",
        tapeEmpty:          "Нет записи",
        tapeRecording:      "Идёт запись",
        bytes:              "{0} Б",
        tapeLoad:           "Загрузить запись",
        tapeRewind:         "Перемотать в начало",
        tapePlay:           "Воспроизвести",
        tapePause:          "Пауза",
        tapeRecord:         "Записывать то, что выводит машина",
        tapeRecordStop:     "Остановить запись и сохранить файл",
        tapeMute:           "Выключить звук магнитофона",
        tapeUnmute:         "Включить звук магнитофона",
        stTapeLoaded:       "Запись загружена: {0}",
        stTapeUnknown:      "Неизвестный формат записи: {0}",
        stTapeFailed:       "Не удалось загрузить запись (ошибка {0})",
        stTapeSaved:        "Запись сохранена: {0}",
        stTapeNothing:      "Ничего не записано",
        stTapeSaveFailed:   "Не удалось сохранить запись (ошибка {0})",
        collapse:           "Свернуть",
        expand:             "Развернуть",
        repoTitle:          "Исходный код на GitHub",
        emuverseTitle:      "Emuverse.ru — энциклопедия эмуляции",
        infoTitle:          "Информация о конфигурации",
        close:              "Закрыть",
        infoMissing:        "Для этой конфигурации нет описания.",
        linkTitle:          "Скопировать ссылку на эту машину с текущими настройками",
        stLinkCopied:       "Ссылка скопирована в буфер обмена",
        stLinkFailed:       "Не удалось скопировать ссылку: {0}",
        stUrlMachine:       "В адресе указана неизвестная машина: {0}",
        clickToStart:       "Щёлкните или нажмите клавишу, чтобы запустить",
        aspectTitle:        "Форма экрана. 4:3 — как на телевизоре; Квадратный экран — ширина равна высоте; Квадратные пиксели — каждая точка растра квадратом из целого числа пикселей экрана",
        aspectSquareScreen: "Квадратный экран",
        aspectSquarePixels: "Квадратные пиксели",
        filterTitle:        "Фильтрация при масштабировании. Нет — резко, при дробном масштабе столбцы бывают разной ширины; Линейная — ровно, но мягко; Резкая — целая часть масштаба без фильтрации, сглаживается только дробная",
        filterNone:         "Нет",
        filterLinear:       "Линейная",
        filterSharp:        "Резкая",
        mouseSpeed:         "Скорость мыши",
        blockMachine:       "Машина",
        blockScreen:        "Экран",
        blockFile:          "Файл",
        blockDevices:       "Устройства",
        stMouseCaptured:    "Мышь захвачена машиной. Esc, Ctrl-Alt или средняя кнопка отпускает ее",
    },
};

let lang = "en";

function t(key, ...args) {
    const text = (I18N[lang] && I18N[lang][key]) || I18N.en[key] || key;
    return text.replace(/\{(\d+)\}/g, (m, i) => (args[i] !== undefined ? String(args[i]) : m));
}

// The saved choice, otherwise the main language of the browser
function detectLanguage() {
    const saved = settings.get("lang", null);
    if (saved && I18N[saved]) return saved;
    const list = (navigator.languages && navigator.languages.length) ? navigator.languages : [navigator.language];
    return String(list[0] || "").toLowerCase().startsWith("ru") ? "ru" : "en";
}

// The status is kept as a key, not as text, so a change of language translates
// the message already on the screen
let statusState = { key: "stInit", cls: "", args: [] };

function setStatus(key, cls, ...args) {
    statusState = { key: key, cls: cls || "", args: args };
    renderStatus();
}

function renderStatus() {
    const el = document.getElementById("status");
    el.textContent = t(statusState.key, ...statusState.args);
    el.className = statusState.cls;
}

function applyLanguage(value) {
    lang = I18N[value] ? value : "en";
    document.documentElement.lang = lang;
    document.title = t("title");
    for (const el of document.querySelectorAll("[data-i18n]")) el.textContent = t(el.dataset.i18n);
    for (const el of document.querySelectorAll("[data-i18n-title]")) {
        el.title = t(el.dataset.i18nTitle);
        // A button with an icon and no caption is named by its hint
        if (!el.textContent.trim()) el.setAttribute("aria-label", el.title);
    }
    document.getElementById("lang").value = lang;
    renderStatus();
    renderDeviceOptions();
    renderDrives();
    renderTapes();
    updateKbdScaleUi();
    for (const block of pageBlocks) block.renderHint();
}

function setupLanguageSelect() {
    const select = document.getElementById("lang");
    select.addEventListener("change", () => {
        settings.set("lang", select.value);
        applyLanguage(select.value);
    });
    releaseFocus(select);
}

// ============================================================================
// Keyboard handling
// ============================================================================

function getModifiers(event) {
    let mods = 0;
    if (event.shiftKey)   mods |= EmuModifier.ShiftModifier;
    if (event.ctrlKey)    mods |= EmuModifier.ControlModifier;
    if (event.altKey)     mods |= EmuModifier.AltModifier;
    return mods;
}

function translateKeyEvent(event) {
    // A digit or a punctuation key: the character it types, as on the desktop
    if (CHARACTER_CODES.has(event.code) && event.key.length === 1) {
        const code = event.key.toUpperCase().charCodeAt(0);
        if (code > 0x20 && code <= 0x7e) return code;
    }

    // Try code-based mapping first (layout-independent)
    let emuKey = CODE_TO_EMUKEY[event.code];
    if (emuKey !== undefined) return emuKey;

    // Fallback: use event.key for printable characters
    if (event.key.length === 1) {
        let code = event.key.toUpperCase().charCodeAt(0);
        // ASCII printable range maps directly
        if (code >= 0x20 && code <= 0x7e) return code;
    }

    return null;
}

function setupKeyboard(module) {
    // What went down on each physical key. The release lifts that same key: the
    // character is taken from the layout, and with Shift let go first the key
    // would come up as ' while " is still held in the machine
    const held = new Map();

    const send = (emuKey, mods, press) =>
        module.ccall("wasm_key_event", null, ["number", "number", "number"], [emuKey, mods, press ? 1 : 0]);

    function onKey(event, press) {
        const id = event.code || event.key;

        let emuKey;
        if (press) {
            // Don't intercept when focused on UI controls
            if (event.target.tagName === "SELECT" || event.target.tagName === "INPUT") return;
            // Nor while the info window is open: Esc closes it, it is not a key of the machine
            if (document.getElementById("info-dialog").open) return;
            emuKey = held.has(id) ? held.get(id) : translateKeyEvent(event);
            if (emuKey === null) return;
            held.set(id, emuKey);
        } else {
            // A key that went down in the machine comes up there, wherever the
            // focus has moved since
            if (held.has(id)) {
                emuKey = held.get(id);
                held.delete(id);
            } else {
                if (event.target.tagName === "SELECT" || event.target.tagName === "INPUT") return;
                emuKey = translateKeyEvent(event);
                if (emuKey === null) return;
            }
        }

        send(emuKey, getModifiers(event), press);

        if (INTERCEPT_KEYS.has(event.code)) {
            event.preventDefault();
        }
    }

    document.addEventListener("keydown", (e) => onKey(e, true));
    document.addEventListener("keyup", (e) => onKey(e, false));

    // A key held while the page loses the focus never sends its keyup here
    window.addEventListener("blur", () => {
        for (const emuKey of held.values()) send(emuKey, 0, false);
        held.clear();
    });
}

// ============================================================================
// On-screen keyboard
// ============================================================================
//
// The drawing is the same SVG the desktop uses. Keys are elements whose id is
// the machine's own name for them ("key_1"), so no table has to be mirrored
// here: the id goes straight back to the emulator.

let kbdPollTimer = null;
let kbdShown = new Set();
let kbdHeldId = null;
let kbdLatched = [];
let kbdDark = new Set();
let kbdLampKeys = new Set();
let kbdResetSeen = 0;

function kbdRelease(module) {
    if (kbdHeldId !== null) {
        module.ccall("wasm_key_event_id", null, ["string", "number"], [kbdHeldId, 0]);
        kbdHeldId = null;
    }
}

function kbdReleaseAll(module) {
    kbdRelease(module);
    for (const id of kbdLatched)
        module.ccall("wasm_key_event_id", null, ["string", "number"], [id, 0]);
    kbdLatched = [];
}

function kbdPoll(module, panel) {
    // The machine has been reset since the last look: it holds nothing any
    // more, so the latches shown here are stale. They are dropped rather than
    // released - there is nothing left to release, and a release would press
    // the modifier back on. The key under the pointer is kept: its release
    // still has to reach the machine, or the key that caused the reset (СБР)
    // stays lit for good
    const seq = module.ccall("wasm_kbd_reset_count", "number", [], []);
    if (seq !== kbdResetSeen) {
        kbdResetSeen = seq;
        kbdLatched = [];
    }

    const text = module.ccall("wasm_keys_pressed", "string", [], []);
    const now = new Set(text ? text.split(",") : []);
    // A latched modifier the machine has let go of by itself - one marked
    // "once" in the key table, released after the next key - is not latched
    // here any more either: the next click has to press it on, not off
    kbdLatched = kbdLatched.filter((id) => now.has(id));
    // A key whose lamp is drawn is left alone: the alphabet belongs on the
    // picture once, where the machine itself shows it
    for (const id of kbdLampKeys) now.delete(id);

    for (const id of kbdShown)
        if (!now.has(id)) {
            const el = panel.querySelector("#" + CSS.escape(id));
            if (el) el.classList.remove("pressed");
        }
    for (const id of now)
        if (!kbdShown.has(id)) {
            const el = panel.querySelector("#" + CSS.escape(id));
            if (el) el.classList.add("pressed");
        }
    kbdShown = now;

    // A lamp shows what the machine is in, not what the user presses, so it is
    // driven by state: the unlit one is blacked out and the burning one is left
    // exactly as the drawing has it
    const dark = module.ccall("wasm_leds_dark", "string", [], []);
    const off = new Set(dark ? dark.split(",") : []);
    for (const id of kbdDark)
        if (!off.has(id)) {
            const el = panel.querySelector("#" + CSS.escape(id));
            if (el) el.classList.remove("led-off");
        }
    for (const id of off)
        if (!kbdDark.has(id)) {
            const el = panel.querySelector("#" + CSS.escape(id));
            if (el) el.classList.add("led-off");
        }
    kbdDark = off;
}

function setupOnScreenKeyboard(module) {
    const panel = document.getElementById("kbd-panel");
    const button = document.getElementById("btn-keyboard");

    if (kbdPollTimer !== null) { clearInterval(kbdPollTimer); kbdPollTimer = null; }
    kbdReleaseAll(module);
    kbdShown = new Set();
    kbdDark = new Set();
    kbdLampKeys = new Set();
    kbdResetSeen = module.ccall("wasm_kbd_reset_count", "number", [], []);
    panel.innerHTML = "";
    panel.hidden = true;
    button.classList.remove("active");
    kbdBaseWidth = 0;
    layoutKeyboard();

    const path = module.ccall("wasm_keyboard_picture", "string", [], []);
    if (!path) { button.hidden = true; return; }

    let svg;
    try {
        svg = module.FS.readFile(path, { encoding: "utf8" });
    } catch (e) {
        console.warn("Keyboard picture not readable:", path, e);
        button.hidden = true;
        return;
    }

    // The picture is machine data, not code: a <script> in it would run with
    // the page's privileges, so it goes before the drawing reaches the DOM
    panel.innerHTML = svg.replace(/<script[\s\S]*?<\/script>/gi, "");

    // Only the keys the machine really has react, the way the desktop window
    // does: a drawing shared between machines then lights up just what fits
    const ids = module.ccall("wasm_key_ids", "string", [], []);
    const known = new Set(ids ? ids.split(",") : []);
    let found = 0;
    for (const el of panel.querySelectorAll('[id^="key_"]')) {
        if (known.has(el.id)) found++;
        else el.classList.add("unknown");
    }
    if (!found) {
        panel.innerHTML = "";
        button.hidden = true;
        return;
    }
    // Only a machine with a drawing gets the button at all
    button.disabled = false;
    button.hidden = false;

    const svgEl = panel.querySelector("svg");
    kbdBaseWidth = svgEl ? svgBaseWidth(svgEl) : 0;
    layoutKeyboard();

    for (const pair of (module.ccall("wasm_led_keys", "string", [], []) || "").split(",")) {
        const [led, key] = pair.split("=");
        if (key && panel.querySelector("#" + CSS.escape(led))) kbdLampKeys.add(key);
    }

    //The panel is polled only while it is open, so the lamps are set once here
    kbdPoll(module, panel);

    const keyOf = (target) => {
        const el = target.closest ? target.closest('[id^="key_"]') : null;
        return (el && known.has(el.id)) ? el.id : null;
    };

    const send = (id, press) =>
        module.ccall("wasm_key_event_id", null, ["string", "number"], [id, press ? 1 : 0]);

    panel.addEventListener("pointerdown", (e) => {
        const id = keyOf(e.target);
        if (id === null) return;
        e.preventDefault();

        // A finger or a mouse has one contact point, so a modifier the hardware
        // expects to be held is clicked on and off instead. The core decides
        // which is which, so the page and the desktop window agree.
        const mode = module.ccall("wasm_key_click_mode", "number", ["string"], [id]);
        if (mode === 2) {
            send(id, true);
            send(id, false);
        } else if (mode === 1) {
            const at = kbdLatched.indexOf(id);
            if (at >= 0) { kbdLatched.splice(at, 1); send(id, false); }
            else { kbdLatched.push(id); send(id, true); }
        } else {
            send(id, true);
            kbdHeldId = id;
            if (e.target.setPointerCapture) e.target.setPointerCapture(e.pointerId);
        }
        kbdPoll(module, panel);
    });

    const up = (e) => { if (kbdHeldId !== null) { kbdRelease(module); kbdPoll(module, panel); } };
    panel.addEventListener("pointerup", up);
    panel.addEventListener("pointercancel", up);
    panel.addEventListener("lostpointercapture", up);
    window.addEventListener("blur", up);
    document.addEventListener("visibilitychange", () => { if (document.hidden) up(); });

    button.onclick = () => {
        panel.hidden = !panel.hidden;
        // A switch that is on is drawn filled
        button.classList.toggle("active", !panel.hidden);
        updateKbdScaleUi();
        if (panel.hidden) {
            clearInterval(kbdPollTimer);
            kbdPollTimer = null;
            kbdReleaseAll(module);
        } else if (kbdPollTimer === null) {
            kbdPollTimer = setInterval(() => kbdPoll(module, panel), 40);
        }
    };
}

// ============================================================================
// Keyboard size
// ============================================================================
//
// 100% is 2 px per millimetre of the drawing, so the keyboards of different
// machines keep their real proportions. Until a size is picked the panel is as
// wide as the screen and follows it; once picked, it stays that size whatever
// the screen does.

const KBD_PX_PER_MM = 2;
const KBD_MIN = 50;
const KBD_MAX = 200;
const KBD_STEP = 10;

let kbdBaseWidth = 0;   // px at 100%; 0 when the machine has no drawing

function svgBaseWidth(svg) {
    const m = /^\s*([\d.]+)\s*(mm|px)?\s*$/i.exec(svg.getAttribute("width") || "");
    if (m && parseFloat(m[1]) > 0) {
        const value = parseFloat(m[1]);
        return (m[2] && m[2].toLowerCase() === "mm") ? value * KBD_PX_PER_MM : value;
    }
    const vb = svg.viewBox && svg.viewBox.baseVal;
    return (vb && vb.width > 0) ? vb.width : 0;
}

// A size given in the address: stands in for the saved one until the user picks
// a size. undefined when the address has none, null for auto
let kbdScaleFromUrl = urlParams.kbdScale === null ? undefined
    : urlParams.kbdScale === "auto" ? null
    : clamp(Math.round(urlParams.kbdScale / KBD_STEP) * KBD_STEP, KBD_MIN, KBD_MAX);

// The size picked by the user, or null while the panel follows the screen
function kbdScaleChosen() {
    if (kbdScaleFromUrl !== undefined) return kbdScaleFromUrl;
    const value = parseInt(settings.get("kbdScale", ""), 10);
    return isNaN(value) ? null : clamp(value, KBD_MIN, KBD_MAX);
}

function screenCssWidth() {
    return parseFloat(document.getElementById("canvas").style.width) || 0;
}

function layoutKeyboard() {
    const panel = document.getElementById("kbd-panel");
    if (kbdBaseWidth > 0) {
        const chosen = kbdScaleChosen();
        const width = (chosen !== null) ? kbdBaseWidth * chosen / 100 : (screenCssWidth() || kbdBaseWidth);
        panel.style.width = Math.round(width) + "px";
    } else {
        panel.style.width = "";
    }
    updateKbdScaleUi();
}

function updateKbdScaleUi() {
    const range = document.getElementById("kbd-scale");
    const label = document.getElementById("kbd-scale-value");
    const minus = document.getElementById("kbd-minus");
    const plus = document.getElementById("kbd-plus");
    const auto = document.getElementById("kbd-auto");

    // Sizing a keyboard that is not on the page means nothing, so the controls
    // are shown only together with it
    document.getElementById("kbd-size").hidden =
        kbdBaseWidth <= 0 || document.getElementById("kbd-panel").hidden;

    if (kbdBaseWidth <= 0) {
        range.disabled = minus.disabled = plus.disabled = auto.disabled = true;
        label.textContent = "";
        return;
    }

    const chosen = kbdScaleChosen();
    // Following the screen the slider is only an indication: the panel itself
    // is exactly as wide as the screen, not a multiple of the step
    const value = (chosen !== null) ? chosen
        : clamp(Math.round(screenCssWidth() / kbdBaseWidth * 100 / KBD_STEP) * KBD_STEP, KBD_MIN, KBD_MAX);

    range.disabled = false;
    range.value = value;
    paintRange(range);
    minus.disabled = value <= KBD_MIN;
    plus.disabled = value >= KBD_MAX;
    // Nothing to go back to while the size already follows the screen
    auto.disabled = chosen === null;
    label.textContent = t(chosen !== null ? "percent" : "kbdPercentAuto", value);
}

function setKbdScale(value) {
    kbdScaleFromUrl = undefined;
    settings.set("kbdScale", clamp(Math.round(value / KBD_STEP) * KBD_STEP, KBD_MIN, KBD_MAX));
    layoutKeyboard();
}

// Back to the width of the screen: a size is not so much set as unpicked, and
// a size never picked is exactly what following the screen means
function setKbdAuto() {
    kbdScaleFromUrl = undefined;
    settings.remove("kbdScale");
    layoutKeyboard();
}

function setupKeyboardSize() {
    const range = document.getElementById("kbd-scale");
    range.addEventListener("input", () => setKbdScale(parseInt(range.value, 10)));
    releaseFocus(range);
    document.getElementById("kbd-minus").addEventListener("click",
        () => setKbdScale(parseInt(range.value, 10) - KBD_STEP));
    document.getElementById("kbd-plus").addEventListener("click",
        () => setKbdScale(parseInt(range.value, 10) + KBD_STEP));
    document.getElementById("kbd-auto").addEventListener("click", setKbdAuto);
    layoutKeyboard();
    watchKeyboardPlace();
}

// Side by side the keyboard has a row of its own under the columns, so its
// size never pushes them apart. Stacked one above another (the query is the
// one that stacks #app in shell.html: a phone, a portrait tablet) it would end
// up below the right column, out of sight of the screen, so there it goes right
// under the screen. The node is moved, not copied: its listeners and pressed
// keys stay with it
function watchKeyboardPlace() {
    const panel = document.getElementById("kbd-panel");
    const query = window.matchMedia("(max-width: 720px), (orientation: portrait) and (pointer: coarse)");
    const place = () => {
        const parent = document.getElementById(query.matches ? "col-center" : "kbd-row");
        if (panel.parentElement !== parent) parent.appendChild(panel);
    };
    query.addEventListener("change", place);
    place();
}

// ============================================================================
// Machine loading
// ============================================================================

let loadedBundles = new Set();

async function fetchAndMount(module, url, mountPath) {
    if (loadedBundles.has(url)) return;

    let response = await fetch(url);
    if (!response.ok) throw new Error(`Failed to fetch ${url}: ${response.status}`);

    let buffer = await response.arrayBuffer();
    let data = new Uint8Array(buffer);

    // The bundle is a simple archive: for each file, a null-terminated name + 4-byte LE size + raw content
    let offset = 0;
    while (offset < data.length) {
        // Read null-terminated filename
        let nameEnd = offset;
        while (nameEnd < data.length && data[nameEnd] !== 0) nameEnd++;
        if (nameEnd === offset) break; // Empty name = end of archive
        let name = new TextDecoder().decode(data.subarray(offset, nameEnd));
        offset = nameEnd + 1;

        // Read 4-byte LE file size
        if (offset + 4 > data.length) break;
        let size = (data[offset] | (data[offset+1] << 8) | (data[offset+2] << 16) | (data[offset+3] << 24)) >>> 0;
        offset += 4;

        // Read file content
        if (offset + size > data.length) break;
        let content = data.subarray(offset, offset + size);
        offset += size;

        // Create directory structure and write file
        let fullPath = mountPath + "/" + name;
        let dir = fullPath.substring(0, fullPath.lastIndexOf("/"));
        mkdirRecursive(module, dir);
        module.FS.writeFile(fullPath, content);
    }

    loadedBundles.add(url);
}

function mkdirRecursive(module, path) {
    let parts = path.split("/").filter(p => p.length > 0);
    let current = "";
    for (let part of parts) {
        current += "/" + part;
        try {
            module.FS.mkdir(current);
        } catch (e) {
            // Directory may already exist
            if (e.errno !== 20) { /* EEXIST */ }
        }
    }
}

// beforeStart, when given, is awaited once the files are in place and before
// the machine is started: a machine that starts by itself waits there for the
// first click, see waitForActivation()
async function loadMachine(module, machinePath, bundleUrl, dataBundleUrl, beforeStart = null) {
    // The pointer captured for the machine being replaced is given back first
    if (document.pointerLockElement) document.exitPointerLock();

    try {
        setStatus("stAssets", "loading");

        // Load shared data bundle (charmaps, keyboard maps) if provided
        if (dataBundleUrl) {
            console.log("Loading data bundle:", dataBundleUrl);
            await fetchAndMount(module, dataBundleUrl, "/data");
        }

        // Load machine-specific bundle
        console.log("Loading machine bundle:", bundleUrl);
        await fetchAndMount(module, bundleUrl, "");

        if (beforeStart) {
            setStatus("clickToStart", "");
            await beforeStart();
        }

        console.log("Bundles loaded. Calling wasm_load_machine:", machinePath);
        setStatus("stStarting", "loading");

        let result = module.ccall("wasm_load_machine", "number", ["string"], [machinePath]);

        // The drives and the options belong to the machine loaded now, or to
        // none at all. Options are named after the configuration file, the way
        // the desktop names them in its ini
        const configKey = machinePath.replace(/^.*\//, "").replace(/\.cfg$/i, "");
        setupDrives(module, configKey);
        setupDeviceOptions(module, configKey);
        setupMouseSpeed(module);
        setupTapes(module, configKey);
        setupOpenFile(module);

        if (result !== 0) {
            setStatus("stMachineFailed", "error", result);
            return false;
        }

        setStatus("stRunning", "success");

        // Resume audio context (miniaudio creates it but browsers suspend it without user gesture)
        if (window.miniaudio && window.miniaudio.unlock) {
            window.miniaudio.unlock();
        }

        // Enable controls
        document.getElementById("btn-reset").disabled = false;
        document.getElementById("btn-power").disabled = false;

        // The drawing belongs to the machine, so it is rebuilt on every load
        setupOnScreenKeyboard(module);
        return true;

    } catch (err) {
        console.error("loadMachine error:", err);
        let msg = (err instanceof Error) ? err.message : String(err);
        setStatus("stError", "error", msg);
        return false;
    }
}

// ============================================================================
// Configuration info
// ============================================================================
//
// The description the desktop shows beside a machine in its chooser: the .md
// file next to the configuration, turned into HTML by the same md4c call.

let currentMachine = null;

function setupInfoDialog(module) {
    const dialog = document.getElementById("info-dialog");
    const button = document.getElementById("btn-info");

    document.getElementById("info-close").addEventListener("click", () => dialog.close());
    // A click on the dimmed page lands on the dialog element itself
    dialog.addEventListener("click", (e) => { if (e.target === dialog) dialog.close(); });
    // The typing goes back to the machine, not to the button that opened it
    dialog.addEventListener("close", () => button.blur());

    button.addEventListener("click", () => {
        if (currentMachine) showMachineInfo(module, currentMachine);
    });
}

function showMachineInfo(module, machine) {
    const dialog = document.getElementById("info-dialog");
    const body = document.getElementById("info-body");

    document.getElementById("info-title").textContent = machine.name;

    const path = machine.cfg_path.replace(/\.cfg$/i, ".md");
    const html = module.ccall("wasm_md2html", "string", ["string"], [path]);
    body.innerHTML = "";
    if (html) {
        // The descriptions are files of the project, still nothing in them runs
        body.innerHTML = html.replace(/<script[\s\S]*?<\/script>/gi, "");
        for (const a of body.querySelectorAll("a[href]")) {
            a.target = "_blank";
            a.rel = "noopener";
        }
    } else {
        element("p", "info-missing", body).textContent = t("infoMissing");
    }

    dialog.showModal();
    body.scrollTop = 0;
}

// ============================================================================
// Device options
// ============================================================================
//
// What the desktop puts on its toolbar as dropdowns - so far the output type
// of a display. A choice is kept per configuration, device and option, under
// the name the desktop gives it in its ini, and put back on every load.

// The core hands over the untranslated strings of the desktop; the Russian
// ones are those of src/translations/ru_ru.ts
const DEVICE_OPTION_TEXT = {
    ru: {
        "Video output": "Видеовыход",
        "Palette card": "Плата палитр",
        "Standard":     "RGB-выход",
        "16 colors":    "16 цветов",
        "16 inverted":  "16 инверсный",
        "8 colors":     "8 цветов",
        "Grayscale":    "Оттенки серого",
        "Experimental": "Прототип",
        "RGB":          "RGB",
        "Mono":         "Моно",
        "Input device": "Устройство ввода",
        "Sound":        "Звук",
        "None":         "Нет",
        "Joystick":     "Джойстик",
        "Mouse":        "Мышь",
    },
};

function optionText(text) {
    const table = DEVICE_OPTION_TEXT[lang];
    return (table && table[text]) || text;
}

let deviceOptions = [];

function readDeviceOptions(module) {
    const text = module.ccall("wasm_device_options", "string", [], []);
    const list = [];
    for (const line of (text || "").split("\n")) {
        if (!line) continue;
        const f = line.split("\t");
        const values = [];
        for (let i = 4; i + 1 < f.length; i += 2) values.push({ id: parseInt(f[i], 10), title: f[i + 1] });
        list.push({ device: f[0], id: parseInt(f[1], 10), title: f[2], current: parseInt(f[3], 10), values: values });
    }
    return list;
}

function setupDeviceOptions(module, configKey) {
    const box = document.getElementById("options");
    box.innerHTML = "";
    deviceOptions = readDeviceOptions(module);

    const apply = (opt, value) =>
        module.ccall("wasm_set_device_option", "number", ["string", "number", "number"], [opt.device, opt.id, value]);

    for (const opt of deviceOptions) {
        const key = "option." + configKey + "_" + opt.device + "_" + opt.id;
        const group = element("div", "group", box);
        opt.caption = element("label", "caption", group);
        opt.select = element("select", "", group);
        opt.select.id = "option-" + opt.device + "-" + opt.id;
        opt.caption.htmlFor = opt.select.id;
        for (const v of opt.values) element("option", "", opt.select).value = v.id;

        // A saved choice goes to the device at once; without one the list shows
        // what the device starts with, which need not be the first value (a
        // socket with "default = ay")
        const saved = parseInt(settings.get(key, ""), 10);
        if (opt.values.some((v) => v.id === saved)) {
            opt.select.value = saved;
            apply(opt, saved);
        } else if (opt.values.some((v) => v.id === opt.current)) {
            opt.select.value = opt.current;
        }

        opt.select.addEventListener("change", () => {
            const value = parseInt(opt.select.value, 10);
            apply(opt, value);
            settings.set(key, value);
        });
        releaseFocus(opt.select);
    }
    renderDeviceOptions();
}

function renderDeviceOptions() {
    for (const opt of deviceOptions) {
        opt.caption.textContent = optionText(opt.title);
        opt.values.forEach((v, i) => { opt.select.options[i].textContent = optionText(v.title); });
    }
    if (mouse.speedCaption) mouse.speedCaption.textContent = t("mouseSpeed");
}

// ============================================================================
// Machine mouse
// ============================================================================
//
// The desktop grabs the host pointer for the mouse of the machine (see
// MainWindow::eventFilter), the page locks it with the Pointer Lock API. A
// click on the screen captures it, and the click itself is not given to the
// machine. The movement goes over in portions, as on the desktop; Ctrl-Alt or
// the middle button give the pointer back, and so does Esc, which the browser
// keeps to itself, and leaving the page.

// Portions of movement are sent this often, as MOUSE_FLUSH_MS of the desktop
const MOUSE_FLUSH_MS = 40;
// The choices of the desktop menu "Mouse speed"; a setting of the host, not of a machine
const MOUSE_SPEEDS = [100, 50, 25, 12];

const mouse = {
    supported: false,
    captured: false,
    accX: 0,
    accY: 0,
    buttons: 0,
    timer: 0,
    speed: 25,
    speedCaption: null,
};

function mouseState(module) {
    return module.ccall("wasm_mouse_state", "number", [], []);
}

function setupMouse(module) {
    const saved = parseInt(settings.get("mouseSpeed", ""), 10);
    mouse.speed = MOUSE_SPEEDS.includes(saved) ? saved : 25;

    // Both canvases sit in the wrap, and "Sharp" filtering swaps them; the
    // lock is taken by the wrap, so a change of filtering does not drop it
    const wrap = document.getElementById("canvas-wrap");
    mouse.supported = typeof wrap.requestPointerLock === "function";
    if (!mouse.supported) return;

    // Buttons: bit 0 left, bit 1 right, -1 unchanged
    const send = (dx, dy, buttons) =>
        module.ccall("wasm_mouse_event", null, ["number", "number", "number"], [dx, dy, buttons]);

    const flush = (buttonsChanged) => {
        // The socket may have been switched to something else meanwhile
        if (mouse.captured && !buttonsChanged && mouseState(module) !== 2) {
            document.exitPointerLock();
            return;
        }
        const dx = Math.trunc(mouse.accX);
        const dy = Math.trunc(mouse.accY);
        mouse.accX -= dx;
        mouse.accY -= dy;
        if (dx === 0 && dy === 0 && !buttonsChanged) return;
        send(dx, dy, buttonsChanged ? mouse.buttons : -1);
    };

    const buttonBit = (e) => (e.button === 0) ? 1 : ((e.button === 2) ? 2 : 0);

    // Only a real mouse captures. A finger also produces a mousedown, and Chrome
    // on Android grants the lock to it: every tap on the page then went to the
    // capture, and a phone has neither Esc nor Ctrl-Alt to give it back
    wrap.addEventListener("pointerdown", (e) => {
        if (e.pointerType !== "mouse") return;
        if (mouse.captured || e.button !== 0 || mouseState(module) !== 2) return;
        e.preventDefault();
        // Refused for a moment after Esc; newer browsers reject the promise,
        // older ones only fire pointerlockerror, and either way nothing is lost
        const request = wrap.requestPointerLock();
        if (request && typeof request.catch === "function") request.catch(() => {});
    });

    // A touch while captured (a tablet with a mouse) gives the pointer back:
    // the fingers are then the only way out
    document.addEventListener("pointerdown", (e) => {
        if (mouse.captured && e.pointerType !== "mouse") document.exitPointerLock();
    }, true);

    // While locked every event goes to the wrap; the capturing click has
    // already been handled above when its own release arrives, and a release
    // of a button the machine never saw pressed is not passed on
    document.addEventListener("mousedown", (e) => {
        if (!mouse.captured) return;
        e.preventDefault();
        if (e.button === 1) {
            document.exitPointerLock();
            return;
        }
        const bit = buttonBit(e);
        if (bit !== 0 && (mouse.buttons & bit) === 0) {
            mouse.buttons |= bit;
            flush(true);
        }
    });

    document.addEventListener("mouseup", (e) => {
        if (!mouse.captured) return;
        e.preventDefault();
        const bit = buttonBit(e);
        if (bit !== 0 && (mouse.buttons & bit) !== 0) {
            mouse.buttons &= ~bit;
            flush(true);
        }
    });

    document.addEventListener("mousemove", (e) => {
        if (!mouse.captured) return;
        // At 100% a line of the machine's screen crossed by the pointer is a
        // step, the line measured on the page as the desktop measures it on
        // the widget: the scale, the aspect and the zoom are all in it
        const canvas = document.getElementById("canvas");
        const shown = parseFloat(canvas.style.height) || canvas.height;
        const line = Math.max(1, (canvas.height > 0) ? shown / canvas.height : 1);
        const k = mouse.speed / 100 / line;
        mouse.accX += e.movementX * k;
        mouse.accY += e.movementY * k;
    });

    document.addEventListener("contextmenu", (e) => {
        if (mouse.captured) e.preventDefault();
    });

    // Ctrl-Alt gives the pointer back. The keys still reach the machine:
    // swallowing one would leave the other held there
    document.addEventListener("keydown", (e) => {
        if (mouse.captured && ((e.key === "Control" && e.altKey) || (e.key === "Alt" && e.ctrlKey)))
            document.exitPointerLock();
    });

    // The browser ends the lock on its own too - Esc, another tab or window -
    // so the capture follows this event, not the calls above
    document.addEventListener("pointerlockchange", () => {
        const on = document.pointerLockElement === wrap;
        if (on === mouse.captured) return;

        if (on) {
            mouse.captured = true;
            mouse.accX = 0;
            mouse.accY = 0;
            mouse.buttons = 0;
            mouse.timer = setInterval(() => flush(false), MOUSE_FLUSH_MS);
            setStatus("stMouseCaptured", "success");
            return;
        }

        clearInterval(mouse.timer);
        // What is still on its way, and the buttons let go: the machine must
        // not be left with a button held by a pointer that has gone
        mouse.accX = 0;
        mouse.accY = 0;
        if (mouse.buttons !== 0) {
            mouse.buttons = 0;
            flush(true);
        }
        mouse.captured = false;
        if (statusState.key === "stMouseCaptured") setStatus("stRunning", "success");
    });
}

// The speed is offered with the device options of a machine that has a mouse,
// whether plugged in or not: the socket is chosen right there
function setupMouseSpeed(module) {
    mouse.speedCaption = null;
    if (!mouse.supported || mouseState(module) === 0) return;

    const group = element("div", "group", document.getElementById("options"));
    mouse.speedCaption = element("label", "caption", group);
    const select = element("select", "", group);
    select.id = "mouse-speed";
    mouse.speedCaption.htmlFor = select.id;
    for (const speed of MOUSE_SPEEDS) {
        const option = element("option", "", select);
        option.value = speed;
        option.textContent = speed + "%";
    }
    select.value = mouse.speed;
    select.addEventListener("change", () => {
        mouse.speed = parseInt(select.value, 10);
        settings.set("mouseSpeed", mouse.speed);
    });
    releaseFocus(select);
    renderDeviceOptions();
}

// ============================================================================
// Direct file loading
// ============================================================================
//
// The "Load a file" of the desktop: a program goes straight into the memory of
// the machine, bypassing tape and disk. Only a configuration whose system
// section names the files it takes ("files") gets the button.

// The messages of the core come as "{Context|text}"; the Russian ones are
// those of src/translations/ru_ru.ts
const CORE_MESSAGE_TEXT = {
    ru: {
        "Unknown file type":                         "Неизвестный тип файла",
        "Error reading":                             "Ошибка чтения",
        "Error reading HEX file":                    "Ошибка чтения HEX-файла",
        "Error reading header data!":                "Ошибка чтения заголовка файла!",
        "Error reading preamble data!":              "Ошибка чтения преамбулы файла!",
        "Error reading CRC bytes!":                  "Не удалось прочитать CRC!",
        "File is smaller than expected!":            "Размер файла меньше ожидаемого!",
        "Unable to find a RAM page to store data":   "Не получилось найти страницу памяти для записи данных",
        "Unable to find an expected preamble byte 0xE6!":     "Не удалось найти байт преамбулы 0xE6!",
        "Unable to find an expected finalization byte 0xE6!": "Не удалось найти байт завершения 0xE6!",
        "Can't load the file: all ramdisks are full!":        "Не удалось загрузить файл: все ram-диски уже заполнены!",
    },
};

function coreMessage(message) {
    const table = CORE_MESSAGE_TEXT[lang];
    return message.replace(/\{[^|{}]*\|([^{}]*)\}/g, (m, text) => (table && table[text]) || text);
}

// The accept list of a file input. A filter that also offers all files
// restricts nothing, as its dialog on the desktop does not
function acceptFor(filter) {
    return /\*\.\*/.test(filter) ? "" : filterExtensions(filter).join(",");
}

function setupOpenFile(module) {
    const button = document.getElementById("btn-open");
    const input = document.getElementById("open-file");

    const files = module.ccall("wasm_system_files", "string", [], []);
    button.hidden = !files;
    input.accept = acceptFor(files || "");

    // Assigned, not added: the machine changes, the button stays
    button.onclick = () => {
        button.blur();
        input.click();
    };
    input.onchange = () => {
        const file = input.files[0];
        // Cleared so that the same file can be loaded again
        const done = file ? openFile(module, file) : Promise.resolve();
        done.then(() => { input.value = ""; });
    };
}

async function openFile(module, file) {
    try {
        const data = new Uint8Array(await file.arrayBuffer());
        // The extension decides the format, so the name stays as it is
        mkdirRecursive(module, "/tmp/open");
        const path = "/tmp/open/" + file.name;
        module.FS.writeFile(path, data);

        const error = module.ccall("wasm_open_file", "string", ["string"], [path]);
        try { module.FS.unlink(path); } catch (e) { /* already gone */ }

        if (!error) setStatus("stFileLoaded", "success", file.name);
        else setStatus("stFileFailed", "error", file.name, coreMessage(error.split(path).join(file.name)));
    } catch (err) {
        console.error("File load error:", err);
        setStatus("stError", "error", err.message);
    }
}

// ============================================================================
// Disk drives
// ============================================================================
//
// A block per drive of the configuration. The core is asked for the list on
// every load and polled for the lamp, the disk and the protection, so whatever
// changes them elsewhere shows up here as well.

let drives = [];
let drivesTimer = null;

function readDrives(module) {
    const text = module.ccall("wasm_fdd_info", "string", [], []);
    const list = [];
    for (const line of (text || "").split("\n")) {
        if (!line) continue;
        const f = line.split("\t");
        list.push({
            name: f[0],
            loaded: f[1] === "1",
            protected: f[2] === "1",
            led: f[3] === "1",
            file: f[4] || "",
            files: f[5] || "",
            filesSave: f[6] || "",
        });
    }
    return list;
}

// "Образы дисков Орион-128 (*.odi)|*.odi" -> [".odi"]
function filterExtensions(filter) {
    const list = [];
    const re = /\*\.([A-Za-z0-9_]+)/g;
    let m;
    while ((m = re.exec(filter)) !== null) {
        const ext = "." + m[1].toLowerCase();
        if (!list.includes(ext)) list.push(ext);
    }
    return list;
}

// Every block that folds, in the order the page holds them. The ones built per
// machine are made anew for every machine, so those taken off the page leave
// the list as the next one is registered
const collapsibleBlocks = [];

// The blocks on the screen now: what the address describes and what it can put
// back. A block whose content this machine lacks is hidden and left out
function visibleBlocks() {
    return collapsibleBlocks.filter((b) => b.root.isConnected && !b.root.hidden);
}

// A block that folds down to its head line: the head is a button, the body the
// rest. Shared by the blocks written into the page and those built per machine.
// urlName is how the address calls it, see blockStates()
function makeCollapsible(root, head, body, settingKey, urlName) {
    const block = { root, head, body, urlName };
    block.apply = (collapsed) => {
        root.classList.toggle("collapsed", collapsed);
        body.hidden = collapsed;
        head.setAttribute("aria-expanded", String(!collapsed));
        block.renderHint();
    };
    block.renderHint = () => { head.title = t(body.hidden ? "expand" : "collapse"); };

    head.addEventListener("click", () => {
        const collapsed = !body.hidden;
        block.apply(collapsed);
        settings.set(settingKey, collapsed ? "1" : "0");
        // A block folded or opened by hand stops following the address
        urlParams.blocks.delete(urlName);
        // The typing belongs to the machine, not to this button
        head.blur();
    });

    for (let i = collapsibleBlocks.length - 1; i >= 0; i--)
        if (!collapsibleBlocks[i].root.isConnected) collapsibleBlocks.splice(i, 1);
    collapsibleBlocks.push(block);

    // What the address says stands in for the saved choice
    const fromUrl = urlParams.blocks.get(urlName);
    block.apply(fromUrl !== undefined ? fromUrl : settings.get(settingKey, "0") === "1");
    return block;
}

// A drive or a tape block. The state is kept per configuration and device, so
// every machine remembers its own
function collapsibleBlock(className, settingKey, urlName) {
    const root = element("div", className);
    const head = element("button", "block-head drive-head", root);
    head.type = "button";
    element("span", "block-chevron", head).textContent = "▼";
    const body = element("div", "block-body", root);
    return makeCollapsible(root, head, body, settingKey, urlName);
}

// The blocks of shell.html, the side columns. Folded or not is a choice about
// the page, not about a machine, so there is one setting per block. A block
// whose content this machine lacks is hidden: the content is watched rather
// than every place that shows or hides it being taught about the block
const BLOCK_CONTENT = { file: "btn-open", keyboard: "btn-keyboard", options: "options" };
const pageBlocks = [];

function setupPageBlocks() {
    for (const root of document.querySelectorAll(".block[data-block]")) {
        const name = root.dataset.block;
        pageBlocks.push(makeCollapsible(root, root.querySelector(".block-head"),
                                        root.querySelector(".block-body"), "collapsed.block_" + name, name));

        const content = BLOCK_CONTENT[name] ? document.getElementById(BLOCK_CONTENT[name]) : null;
        if (!content) continue;
        // A button is there when it is not hidden, a box when it has something in it
        const sync = () => {
            root.hidden = content.hidden || (content.tagName === "DIV" && content.children.length === 0);
        };
        new MutationObserver(sync).observe(content, { attributes: true, attributeFilter: ["hidden"], childList: true });
        sync();
    }
}

function setupDrives(module, configKey) {
    if (drivesTimer !== null) { clearInterval(drivesTimer); drivesTimer = null; }
    const box = document.getElementById("drives");
    box.innerHTML = "";
    drives = readDrives(module);
    for (const drive of drives) {
        drive.ui = buildDrive(module, drive, configKey);
        box.appendChild(drive.ui.root);
    }
    renderDrives();
    if (drives.length) drivesTimer = setInterval(() => pollDrives(module), 200);
}

function pollDrives(module) {
    const now = readDrives(module);
    for (const drive of drives) {
        const state = now.find((d) => d.name === drive.name);
        if (!state) continue;
        drive.loaded = state.loaded;
        drive.protected = state.protected;
        drive.led = state.led;
        drive.file = state.file;
    }
    renderDrives();
}

function buildDrive(module, drive, configKey) {
    const ui = {};
    ui.block = collapsibleBlock("drive", "collapsed." + configKey + "_" + drive.name, drive.name);
    ui.root = ui.block.root;
    const body = ui.block.body;

    // The lamp stays in the head line: a folded drive still shows it is busy
    const head = ui.block.head;
    ui.led = element("span", "drive-led", head);
    ui.title = element("span", "drive-title", head);
    element("span", "drive-device", head).textContent = drive.name;

    ui.file = element("div", "drive-file", body);

    const buttons = element("div", "row", body);
    ui.load = element("button", "", buttons);
    ui.save = element("button", "", buttons);
    ui.eject = element("button", "", buttons);

    const input = element("input", "", body);
    input.type = "file";
    input.hidden = true;
    const exts = filterExtensions(drive.files);
    if (exts.length) input.accept = exts.join(",");

    const protect = element("label", "drive-protect", body);
    ui.protect = element("input", "", protect);
    ui.protect.type = "checkbox";
    ui.protectText = element("span", "", protect);

    ui.load.addEventListener("click", () => input.click());
    input.addEventListener("change", async () => {
        const file = input.files[0];
        if (file) await loadDisk(module, drive, file);
        // Cleared so that the same file can be loaded again
        input.value = "";
    });
    ui.save.addEventListener("click", () => saveDisk(module, drive));
    ui.eject.addEventListener("click", () => {
        module.ccall("wasm_fdd_eject", "number", ["string"], [drive.name]);
        pollDrives(module);
    });
    ui.protect.addEventListener("change", () => {
        module.ccall("wasm_fdd_protect", "number", ["string", "number"], [drive.name, ui.protect.checked ? 1 : 0]);
        pollDrives(module);
    });
    releaseFocus(ui.protect);

    return ui;
}

function renderDrives() {
    drives.forEach((drive, i) => {
        const ui = drive.ui;
        ui.title.textContent = t("drive", i + 1);
        ui.block.renderHint();
        ui.file.textContent = drive.loaded ? drive.file : t("noDisk");
        ui.file.title = drive.loaded ? drive.file : "";
        ui.led.classList.toggle("on", drive.led);
        ui.load.textContent = t("load");
        ui.save.textContent = t("save");
        ui.eject.textContent = t("eject");
        ui.protectText.textContent = t("protect");
        ui.save.disabled = !drive.loaded;
        ui.eject.disabled = !drive.loaded;
        ui.protect.checked = drive.protected;
    });
}

async function loadDisk(module, drive, file) {
    setStatus("stDiskLoading", "loading", file.name);
    try {
        const data = new Uint8Array(await file.arrayBuffer());

        // Write file to Emscripten virtual FS, then tell C++ to load it
        const path = "/tmp/" + file.name;
        module.FS.writeFile(path, data);

        const result = module.ccall("wasm_load_file", "number", ["string", "string"], [drive.name, path]);
        if (result === 0) setStatus("stDiskLoaded", "", file.name);
        else setStatus("stDiskFailed", "error", result);
    } catch (err) {
        console.error("Disk load error:", err);
        setStatus("stError", "error", err.message);
    }
    pollDrives(module);
}

// The image is written to the virtual FS in the first format the drive saves
// to, named after the disk in it, and handed to the browser as a download
function saveDisk(module, drive) {
    const exts = filterExtensions(drive.filesSave);
    const ext = exts.length ? exts[0] : ".dsk";
    let base = drive.file.replace(/^.*[\\/]/, "");
    const dot = base.lastIndexOf(".");
    if (dot > 0) base = base.substring(0, dot);
    const fileName = (base || drive.name) + ext;

    mkdirRecursive(module, "/tmp/save");
    const path = "/tmp/save/" + fileName;
    try { module.FS.unlink(path); } catch (e) { /* nothing left from before */ }

    const result = module.ccall("wasm_fdd_save", "number", ["string", "string"], [drive.name, path]);
    if (result !== 0) {
        setStatus("stSaveFailed", "error", result);
        return;
    }

    // save_image() answers success for a format it does not write at all, so
    // the file itself is the proof
    let data;
    try {
        data = module.FS.readFile(path);
        module.FS.unlink(path);
    } catch (e) {
        setStatus("stSaveUnsupported", "error", ext);
        return;
    }

    downloadFile(fileName, data);
    setStatus("stDiskSaved", "", fileName);
}

// ============================================================================
// Tape recorder
// ============================================================================
//
// A small deck per tape recorder of the machine: load, rewind, play/pause,
// record, sound. Where the machine drives the motor the tape starts and stops
// by itself (wasm_load_machine wires that), so the state is polled, not assumed.

const TAPE_READ = 1;
const TAPE_PLAY = 0, TAPE_STOP = 1, TAPE_REWIND = 2, TAPE_RECORD = 3, TAPE_MUTE = 4;

const TAPE_SYMBOL = {
    load:   "⏏︎",
    rewind: "⏮︎",
    play:   "▶︎",
    pause:  "⏸︎",
    record: "●",
    sound:  "♪",
};

let tapes = [];
let tapeTimer = null;

function readTapes(module) {
    const text = module.ccall("wasm_tape_info", "string", [], []);
    const list = [];
    for (const line of (text || "").split("\n")) {
        if (!line) continue;
        const f = line.split("\t");
        list.push({
            name: f[0],
            mode: parseInt(f[1], 10) || 0,
            position: parseInt(f[2], 10) || 0,
            total: parseInt(f[3], 10) || 0,
            recorded: parseInt(f[4], 10) || 0,
            recordName: f[5] || "",
            files: f[6] || "",
        });
    }
    return list;
}

function formatTime(seconds) {
    const s = Math.max(0, seconds | 0);
    return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0");
}

const tapeControl = (module, tape, action, value) =>
    module.ccall("wasm_tape_control", "number", ["string", "number", "number"], [tape.name, action, value || 0]);

function setupTapes(module, configKey) {
    if (tapeTimer !== null) { clearInterval(tapeTimer); tapeTimer = null; }
    const box = document.getElementById("tapes");
    box.innerHTML = "";
    tapes = readTapes(module);
    for (const tape of tapes) {
        tape.file = "";
        tape.recording = false;
        tape.muted = false;
        tape.wasPlaying = false;
        tape.ui = buildTape(module, tape, configKey);
        box.appendChild(tape.ui.root);
    }
    renderTapes();
    if (tapes.length) tapeTimer = setInterval(() => pollTapes(module), 250);
}

function pollTapes(module) {
    const now = readTapes(module);
    for (const tape of tapes) {
        const state = now.find((t) => t.name === tape.name);
        if (!state) continue;
        Object.assign(tape, { mode: state.mode, position: state.position, total: state.total,
                              recorded: state.recorded, recordName: state.recordName });
        // The end of the tape, not a stop in the middle: rewound, as the
        // desktop recorder does, so that the next play starts from the top
        if (tape.wasPlaying && tape.mode !== TAPE_READ && tape.total > 0 && tape.position >= tape.total) {
            tapeControl(module, tape, TAPE_REWIND);
            tape.position = 0;
        }
        tape.wasPlaying = tape.mode === TAPE_READ;
    }
    renderTapes();
}

function buildTape(module, tape, configKey) {
    const ui = {};
    ui.block = collapsibleBlock("tape", "collapsed." + configKey + "_" + tape.name, tape.name);
    ui.root = ui.block.root;
    const body = ui.block.body;

    const head = ui.block.head;
    ui.title = element("span", "drive-title", head);
    if (tapes.length > 1) element("span", "drive-device", head).textContent = tape.name;

    const info = element("div", "tape-info", body);
    ui.name = element("span", "tape-name", info);
    ui.time = element("span", "tape-time", info);

    const row = element("div", "row tape-buttons", body);
    const button = (symbol, className) => {
        const b = element("button", className || "", row);
        b.textContent = symbol;
        return b;
    };
    ui.load = button(TAPE_SYMBOL.load);
    ui.rewind = button(TAPE_SYMBOL.rewind);
    ui.play = button(TAPE_SYMBOL.play);
    ui.record = button(TAPE_SYMBOL.record, "tape-rec");
    ui.sound = button(TAPE_SYMBOL.sound);

    const input = element("input", "", body);
    input.type = "file";
    input.hidden = true;
    const exts = filterExtensions(tape.files);
    if (exts.length) input.accept = exts.join(",");

    ui.load.addEventListener("click", () => input.click());
    input.addEventListener("change", async () => {
        const file = input.files[0];
        if (file) await loadTape(module, tape, file);
        input.value = "";
    });

    ui.rewind.addEventListener("click", () => {
        tapeControl(module, tape, TAPE_STOP);
        tapeControl(module, tape, TAPE_REWIND);
        pollTapes(module);
    });

    ui.play.addEventListener("click", () => {
        tapeControl(module, tape, tape.mode === TAPE_READ ? TAPE_STOP : TAPE_PLAY);
        pollTapes(module);
    });

    ui.record.addEventListener("click", () => {
        tape.recording = !tape.recording;
        tapeControl(module, tape, TAPE_RECORD, tape.recording ? 1 : 0);
        if (!tape.recording) saveTapeRecord(module, tape);
        pollTapes(module);
    });

    ui.sound.addEventListener("click", () => {
        tape.muted = !tape.muted;
        tapeControl(module, tape, TAPE_MUTE, tape.muted ? 1 : 0);
        renderTapes();
    });

    return ui;
}

function renderTapes() {
    for (const tape of tapes) {
        const ui = tape.ui;
        const playing = tape.mode === TAPE_READ;

        ui.title.textContent = t("tape");
        ui.block.renderHint();
        if (tape.recording) {
            ui.name.textContent = t("tapeRecording");
            ui.time.textContent = t("bytes", tape.recorded);
        } else {
            ui.name.textContent = tape.file || t("tapeEmpty");
            ui.time.textContent = tape.file ? formatTime(tape.position) + " / " + formatTime(tape.total) : "";
        }
        ui.name.title = tape.file;

        ui.play.textContent = playing ? TAPE_SYMBOL.pause : TAPE_SYMBOL.play;
        ui.play.classList.toggle("active", playing);
        ui.record.classList.toggle("active", tape.recording);
        ui.sound.classList.toggle("muted", tape.muted);

        ui.load.disabled = tape.recording;
        ui.rewind.disabled = !tape.file || tape.recording;
        ui.play.disabled = !tape.file || tape.recording;

        const hint = (b, key) => { b.title = t(key); b.setAttribute("aria-label", b.title); };
        hint(ui.load, "tapeLoad");
        hint(ui.rewind, "tapeRewind");
        hint(ui.play, playing ? "tapePause" : "tapePlay");
        hint(ui.record, tape.recording ? "tapeRecordStop" : "tapeRecord");
        hint(ui.sound, tape.muted ? "tapeUnmute" : "tapeMute");
    }
}

async function loadTape(module, tape, file) {
    try {
        const data = new Uint8Array(await file.arrayBuffer());
        // The name stays as it is: a БК tape carries it in its header
        mkdirRecursive(module, "/tmp/tape");
        const path = "/tmp/tape/" + file.name;
        module.FS.writeFile(path, data);

        const result = module.ccall("wasm_tape_load", "number", ["string", "string"], [tape.name, path]);
        if (result === 0) {
            tape.file = file.name;
            tape.wasPlaying = false;
            setStatus("stTapeLoaded", "", file.name);
        } else if (result === -3) {
            setStatus("stTapeUnknown", "error", file.name);
        } else {
            setStatus("stTapeFailed", "error", result);
        }
    } catch (err) {
        console.error("Tape load error:", err);
        setStatus("stError", "error", err.message);
    }
    pollTapes(module);
}

// What the machine has written goes to the browser as a download, under the
// name the core suggests - its extension decides how the file is put back on
// the tape - or "tape" with the first extension the machine loads tapes from
function saveTapeRecord(module, tape) {
    const state = readTapes(module).find((s) => s.name === tape.name);
    const ext = filterExtensions(tape.files)[0] || ".bin";
    let fileName = (state && state.recordName) ? state.recordName : "tape" + ext;
    if (fileName.indexOf(".") < 0) fileName += ext;
    fileName = fileName.replace(/[\\/]/g, "_");

    mkdirRecursive(module, "/tmp/tape-out");
    const path = "/tmp/tape-out/" + fileName;
    const size = module.ccall("wasm_tape_save", "number", ["string", "string"], [tape.name, path]);
    if (size <= 0) {
        if (size === 0) setStatus("stTapeNothing", "error");
        else setStatus("stTapeSaveFailed", "error", size);
        return;
    }

    let data;
    try {
        data = module.FS.readFile(path);
        module.FS.unlink(path);
    } catch (e) {
        setStatus("stTapeSaveFailed", "error", -5);
        return;
    }
    downloadFile(fileName, data);
    setStatus("stTapeSaved", "", fileName);
}

// ============================================================================
// Screen scale
// ============================================================================

const SCREEN_MIN = 100;
const SCREEN_MAX = 400;
const SCREEN_STEP = 25;

let screenScale = 200;  // percent

// An aspect mode given in the address stands in for the saved one until the
// user picks one from the list
let aspectFromUrl = urlParams.aspect;

function aspectMode() {
    if (aspectFromUrl !== null) return aspectFromUrl;
    return aspectId(settings.get("aspect", "")) || "4x3";
}

// The same for the filtering
let filterFromUrl = urlParams.filter;

function filterMode() {
    if (filterFromUrl !== null) return filterFromUrl;
    return filterId(settings.get("filter", "")) || "none";
}

// Filtering "Sharp". The core draws into #canvas, which is hidden then; every
// new frame is copied into #canvas-sharp enlarged by whole numbers without
// filtering, and the browser brings that one to the size on the page with its
// linear filter - so only the fraction of the scale is smoothed and a stroke
// one pixel wide comes out the same width everywhere. The copy is made only
// when the core has put a frame: putImageData of its context is wrapped, and
// getContext() hands out that same object every time
const sharpScreen = { active: false, dirty: false, raf: 0, hooked: false };

function copySharpFrame() {
    sharpScreen.raf = requestAnimationFrame(copySharpFrame);
    if (!sharpScreen.dirty) return;
    sharpScreen.dirty = false;
    const src = document.getElementById("canvas");
    const dst = document.getElementById("canvas-sharp");
    const ctx = dst.getContext("2d");
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(src, 0, 0, dst.width, dst.height);
}

function applyFilter(width, height) {
    const src = document.getElementById("canvas");
    const dst = document.getElementById("canvas-sharp");
    const mode = filterMode();
    src.classList.toggle("filter-linear", mode === "linear");

    if (mode !== "sharp") {
        if (sharpScreen.active) {
            cancelAnimationFrame(sharpScreen.raf);
            sharpScreen.active = false;
        }
        dst.hidden = true;
        src.hidden = false;
        return;
    }

    if (!sharpScreen.hooked) {
        const ctx = src.getContext("2d");
        const put = ctx.putImageData.bind(ctx);
        ctx.putImageData = (...args) => { put(...args); sharpScreen.dirty = true; };
        sharpScreen.hooked = true;
    }

    // The whole part of the scale, per axis. The margin is for sizes that come
    // in fractions of a layout pixel: 1023.99 device pixels over a raster 512
    // wide are still two of them, not one
    const dpr = window.devicePixelRatio || 1;
    const kx = Math.max(1, Math.floor(width * dpr / src.width + 0.01));
    const ky = Math.max(1, Math.floor(height * dpr / src.height + 0.01));
    // Assigning the size clears the canvas, so only when it changes
    if (dst.width !== src.width * kx) dst.width = src.width * kx;
    if (dst.height !== src.height * ky) dst.height = src.height * ky;
    dst.style.width = width + "px";
    dst.style.height = height + "px";

    // The last frame is drawn again at once: a machine that stands still puts
    // no new one, and the canvas may just have been cleared
    sharpScreen.dirty = true;
    src.hidden = true;
    dst.hidden = false;
    if (!sharpScreen.active) {
        sharpScreen.active = true;
        sharpScreen.raf = requestAnimationFrame(copySharpFrame);
    }
}

function updateScreenScaleUi() {
    const range = document.getElementById("screen-scale");
    range.value = screenScale;
    paintRange(range);
    document.getElementById("screen-scale-value").textContent = t("percent", screenScale);
    document.getElementById("screen-minus").disabled = screenScale <= SCREEN_MIN;
    document.getElementById("screen-plus").disabled = screenScale >= SCREEN_MAX;
    document.getElementById("aspect").value = aspectMode();
    document.getElementById("filter").value = filterMode();
}

function updateCanvasSize() {
    updateScreenScaleUi();

    const canvas = document.getElementById("canvas");
    const canvasW = canvas.width;
    const canvasH = canvas.height;
    if (canvasW === 0 || canvasH === 0) return;

    const scale = screenScale / 100;
    // A CSS pixel is not a pixel of the screen: under the display scaling of
    // Windows (125%, 150%) or a zoomed page one of them is 1.25 or 1.5 device
    // pixels, and a raster line drawn over 2.5 of them is rounded to 2 or 3 -
    // lines of uneven height, as if scaled by a fraction
    const dpr = window.devicePixelRatio || 1;
    // Device pixels per pixel of the raster, a whole number near the scale asked for
    const whole = Math.max(1, Math.round(scale * dpr));
    let width, height;

    switch (aspectMode()) {
        case "1x1":
            // Square pixels, each a whole number of device pixels on both axes
            width = canvasW * whole / dpr;
            height = canvasH * whole / dpr;
            break;
        case "square":
            // A square screen: the height as in 4:3, the width equal to it
            height = Math.round(canvasH * scale);
            width = height;
            break;
        default:
            // 4:3, the way the desktop renderers do it: the height is the lines
            // of the raster times the scale and the width follows from it
            // (render_w = sx * ss * ps, render_h = sy * ss). A machine that
            // changes only its horizontal resolution - the БК between colour and
            // monochrome - keeps its size; stretching the height of a wide
            // raster instead made it jump. Whole CSS pixels: a fractional size
            // would smear the edges of the picture
            width = Math.round(canvasH * 4 / 3 * scale);
            height = Math.round(canvasH * scale);
            break;
    }

    canvas.style.width = width + "px";
    canvas.style.height = height + "px";
    applyFilter(width, height);

    // A keyboard that follows the screen follows it here
    layoutKeyboard();
}

// devicePixelRatio changes with the zoom of the page and when the window moves
// to a screen of another scale; the query matches only the ratio it was made
// for, so it is made anew after every change
function watchPixelRatio() {
    const query = window.matchMedia("(resolution: " + window.devicePixelRatio + "dppx)");
    const changed = () => {
        query.removeEventListener("change", changed);
        updateCanvasSize();
        watchPixelRatio();
    };
    query.addEventListener("change", changed);
}

function setScreenScale(value) {
    screenScale = clamp(Math.round(value / SCREEN_STEP) * SCREEN_STEP, SCREEN_MIN, SCREEN_MAX);
    settings.set("screenScale", screenScale);
    updateCanvasSize();
}

function setupCanvasScaling() {
    const canvas = document.getElementById("canvas");
    const range = document.getElementById("screen-scale");

    // Before the slider the scale was a whole multiple kept under "scale". A
    // scale from the address wins, and is saved only once the slider moves
    const saved = parseInt(settings.get("screenScale", ""), 10);
    const multiple = parseInt(settings.get("scale", ""), 10);
    const value = urlParams.scale !== null ? urlParams.scale
        : !isNaN(saved) ? saved : (!isNaN(multiple) ? multiple * 100 : 200);
    screenScale = clamp(Math.round(value / SCREEN_STEP) * SCREEN_STEP, SCREEN_MIN, SCREEN_MAX);

    range.addEventListener("input", () => setScreenScale(parseInt(range.value, 10)));
    releaseFocus(range);
    document.getElementById("screen-minus").addEventListener("click", () => setScreenScale(screenScale - SCREEN_STEP));
    document.getElementById("screen-plus").addEventListener("click", () => setScreenScale(screenScale + SCREEN_STEP));

    const aspect = document.getElementById("aspect");
    aspect.addEventListener("change", () => {
        aspectFromUrl = null;
        settings.set("aspect", aspect.value);
        updateCanvasSize();
    });
    releaseFocus(aspect);

    const filter = document.getElementById("filter");
    filter.addEventListener("change", () => {
        filterFromUrl = null;
        settings.set("filter", filter.value);
        updateCanvasSize();
    });
    releaseFocus(filter);

    new MutationObserver(updateCanvasSize).observe(canvas, { attributes: true, attributeFilter: ["width", "height"] });
    watchPixelRatio();
    updateCanvasSize();
}

// ============================================================================
// Audio context activation
// ============================================================================

let audioActivated = false;

function unlockAudio() {
    audioActivated = true;
    if (window.miniaudio && window.miniaudio.devices) {
        for (let i = 0; i < window.miniaudio.devices.length; i++) {
            let dev = window.miniaudio.devices[i];
            if (dev && dev.webaudio) {
                dev.webaudio.resume();
            }
        }
    }
}

// A machine that starts by itself - named in the address or left from the
// previous visit - would run before the page has had a click or a key press,
// and until then the browser keeps its sound off: whatever the machine plays
// first, the start-up beep of an Агат, is lost for good. So such a machine
// waits, its files already loaded, behind a curtain asking for the click. A
// page that has had one already - a machine picked from the list - goes on at
// once, and so does a browser that cannot say, as the page did before.
function waitForActivation(machine) {
    const active = navigator.userActivation ? navigator.userActivation.hasBeenActive : audioActivated;
    if (active) return Promise.resolve();

    const overlay = document.getElementById("overlay");
    document.getElementById("overlay-machine").textContent = machine.name;
    overlay.classList.remove("hidden");

    return new Promise((resolve) => {
        const go = () => {
            overlay.removeEventListener("click", go);
            document.removeEventListener("keydown", go, true);
            overlay.classList.add("hidden");
            unlockAudio();
            resolve();
        };
        // A tap ends in a click as well. Any key will do, and it goes on to
        // the machine as usual - there is none running yet to take it
        overlay.addEventListener("click", go);
        document.addEventListener("keydown", go, true);
    });
}

function setupAudioActivation() {
    document.addEventListener("keydown", unlockAudio);
    document.addEventListener("mousedown", unlockAudio);
    document.addEventListener("touchstart", unlockAudio);
}

// ============================================================================
// Initialization
// ============================================================================

// The module is downloaded by the page and compiled from the bytes, not with
// the WebAssembly.instantiateStreaming() the runtime would use: that one tells
// nothing until it is done. Here every step is named for the startup watcher,
// so a load that hangs says whether it is the network or the compiler - an
// iPhone behind a VPN that cut every response after a few dozen kilobytes
// showed "downloading 35 KB", where streaming had only ever said "loading".
// The module object is handed on: the runtime posts it to the thread workers
async function instantiateWasmBytes(url, imports, startup) {
    startup.wasm("downloading");
    const response = await fetch(url, { credentials: "same-origin" });
    if (!response.ok) throw new Error(url + ": HTTP " + response.status);

    let bytes;
    if (response.body && response.body.getReader) {
        const reader = response.body.getReader();
        const chunks = [];
        let size = 0;
        for (;;) {
            const { done, value } = await reader.read();
            if (done) break;
            chunks.push(value);
            size += value.length;
            startup.wasm("downloading " + Math.round(size / 1024) + " KB");
        }
        bytes = new Uint8Array(size);
        let at = 0;
        for (const chunk of chunks) { bytes.set(chunk, at); at += chunk.length; }
    } else {
        bytes = new Uint8Array(await response.arrayBuffer());
    }

    startup.wasm("compiling " + Math.round(bytes.length / 1024) + " KB");
    const module = await WebAssembly.compile(bytes);
    startup.wasm("instantiating");
    const instance = await WebAssembly.instantiate(module, imports);
    startup.wasm("compiled");
    return { instance, module };
}

// EmuModule() settles only once every thread worker has loaded the module, and
// the runtime passes no failure on the way: a worker whose script does not
// load, a rejection inside the runtime itself. The page would wait forever with
// nothing to show for it, so here such a failure ends the wait, and a wait that
// goes on reports what it is still waiting for. A slow network is not taken for
// a failure: the message changes, the load goes on
function watchStartup() {
    const NativeWorker = window.Worker;
    const started = Date.now();
    const workers = { created: 0, loaded: 0 };
    // How far the load got, for a report from a device with no console at hand
    const stage = { wasm: "loading", error: "" };
    const details = () => "wasm " + stage.wasm
        + ", workers " + workers.loaded + "/" + workers.created
        + ", isolated " + window.crossOriginIsolated
        + (stage.error ? ", " + stage.error : "");

    let fail;
    const failed = new Promise((resolve, reject) => { fail = reject; });
    const failWith = (what) => fail(new Error(what + " (" + details() + ")"));

    window.Worker = function (url, options) {
        const worker = new NativeWorker(url, options);
        workers.created++;
        worker.addEventListener("message", (e) => {
            if (e.data && e.data.cmd === "loaded") workers.loaded++;
        });
        worker.addEventListener("error", (e) => failWith("worker: " + (e.message || "script error")));
        worker.addEventListener("messageerror", () => failWith("worker: message error"));
        return worker;
    };
    window.Worker.prototype = NativeWorker.prototype;

    const onRejection = (e) => {
        const reason = e.reason;
        failWith(String((reason && reason.message) || reason));
    };
    window.addEventListener("unhandledrejection", onRejection);

    const timer = setInterval(() => {
        const seconds = Math.round((Date.now() - started) / 1000);
        if (seconds >= 10) setStatus("stWasmWaiting", "loading", seconds, details());
    }, 5000);

    return {
        failed,
        // The last complaint of the runtime, shown with the rest
        note(text) { stage.error = String(text); },
        // What instantiateWasmBytes() is doing
        wasm(text) { stage.wasm = text; },
        fail(e) { failWith(String((e && e.message) || e)); },
        stop() {
            window.Worker = NativeWorker;
            window.removeEventListener("unhandledrejection", onRejection);
            clearInterval(timer);
        },
    };
}

async function initEcat() {
    let selectEl = document.getElementById("machine-select");

    // Before the language: it renders the hints of the blocks
    setupPageBlocks();
    applyLanguage(detectLanguage());
    setupLanguageSelect();
    for (const range of document.querySelectorAll('input[type="range"]')) paintRange(range);
    setupAudioActivation();
    setupCanvasScaling();
    setupKeyboardSize();
    setupLinkButton();

    // The version of the package, which build-wasm writes next to the page from
    // the VERSION file. A page without it shows none; a server that answers
    // every path with its own page is not taken for a version either
    fetch("version.txt")
        .then((resp) => (resp.ok ? resp.text() : ""))
        .then((text) => {
            const version = text.trim();
            if (!/^[0-9A-Za-z.+-]{1,32}$/.test(version)) return;
            const el = document.getElementById("version");
            el.textContent = "eCat3 v" + version;
            el.hidden = false;
        })
        .catch(() => { /* no version shown, nothing else lost */ });

    // Load machines manifest
    setStatus("stListLoading", "loading");

    let machines;
    try {
        let resp = await fetch("machines.json");
        machines = await resp.json();
    } catch (err) {
        setStatus("stListFailed", "error", err.message);
        return;
    }

    // Populate dropdown; the placeholder is translated with the rest of the page
    selectEl.innerHTML = "";
    const placeholder = element("option", "", selectEl);
    placeholder.value = "";
    placeholder.dataset.i18n = "selectMachine";
    placeholder.textContent = t("selectMachine");
    // The manifest lists the machines family by family, in the order of the
    // desktop chooser; each family becomes a group of the list
    let group = null;
    for (let m of machines) {
        if (m.family && (group === null || group.dataset.type !== m.type)) {
            group = element("optgroup", "", selectEl);
            group.label = m.family;
            group.dataset.type = m.type;
        }
        const opt = element("option", "", m.family ? group : selectEl);
        opt.value = m.id;
        opt.textContent = m.name;
    }

    // [TapeFiles] says how a tape file of every extension goes onto the tape;
    // package_machines.py copies it out of deploy/.ecat.ini, the desktop defaults
    let tapeFiles = "";
    try {
        const resp = await fetch("tapefiles.ini");
        if (resp.ok) tapeFiles = await resp.text();
    } catch (e) {
        // No tape will load then; everything else works
    }

    const iniText = [
        "[Core]",
        "mapper_cache=8",
        "",
        "[Video]",
        "scale=2",
        "ratio=1",
        "filtering=0",
        "",
        tapeFiles
    ].join("\n");

    // The core runs in threads, and memory shared between them exists only on
    // a cross-origin isolated page. Without it EmuModule() never settles: the
    // memory cannot be posted to the thread workers, and the runtime loses that
    // rejection (a DataCloneError in the console, nothing on the page). An
    // iPhone opening the page at http://192.168.… is exactly that case
    if (!window.crossOriginIsolated) {
        if (!window.isSecureContext) setStatus("stNotSecure", "error", location.origin);
        else setStatus("stNotIsolated", "error");
        return;
    }

    // Initialize WASM module
    setStatus("stWasmLoading", "loading");
    // EmuModule() does its synchronous part - the shared memory, the thread
    // workers - in this same task, so without a frame in between a start that
    // fails right there would leave the message about the machine list on the
    // screen. rAF alone never fires in a background tab, hence the timeout
    await new Promise((resolve) => {
        requestAnimationFrame(() => setTimeout(resolve, 0));
        setTimeout(resolve, 100);
    });

    let module;
    const startup = watchStartup();
    try {
        module = await Promise.race([startup.failed, EmuModule({
            print: (text) => console.log("eCat3:", text),
            printErr: (text) => { console.error("eCat3:", text); startup.note(text); },
            // The module is instantiated by the page, see instantiateWasmBytes().
            // The runtime waits for receiveInstance and has no failure path of
            // its own here, so a failure ends the wait through the watcher
            instantiateWasm: (imports, receiveInstance) => {
                instantiateWasmBytes("ecat3.wasm", imports, startup)
                    .then((r) => receiveInstance(r.instance, r.module))
                    .catch((e) => startup.fail(e));
                return {};
            },
            // The emulator reads its ini once, when main() creates it, so the
            // file has to be there before main() runs - a write afterwards is
            // never seen by the core
            preRun: [(m) => m.FS.writeFile("/ecat.ini", iniText)],
        })]);
    } catch (err) {
        setStatus("stWasmFailed", "error", (err && err.message) || String(err));
        return;
    } finally {
        startup.stop();
    }

    // Create required FS directories
    mkdirRecursive(module, "/computers");
    mkdirRecursive(module, "/data");
    mkdirRecursive(module, "/software");
    mkdirRecursive(module, "/tmp");


    setupKeyboard(module);
    setupMouse(module);
    setupInfoDialog(module);

    selectEl.disabled = false;
    setStatus("stReady", "");

    // auto: the machine starts by itself, not by the user's choice, and waits
    // for the first click or key press before it runs
    const startMachine = async (machine, remember = true, auto = false) => {
        selectEl.value = machine.id;
        selectEl.disabled = true;
        currentMachine = machine;
        document.getElementById("btn-info").disabled = false;
        const ok = await loadMachine(module, machine.cfg_path, machine.bundle_url, machine.data_bundle_url || null,
                                     auto ? () => waitForActivation(machine) : null);
        selectEl.disabled = false;

        // Remembered only once it runs, so that the next visit starts it again;
        // a machine that fails to start is not brought back. One named in the
        // address is not the user's own choice and leaves the saved one alone
        if (remember) {
            if (ok) settings.set("machine", machine.id);
            else if (settings.get("machine", "") === machine.id) settings.set("machine", "");
        }
        return ok;
    };

    // Machine selection handler
    selectEl.addEventListener("change", async () => {
        let machine = machines.find(m => m.id === selectEl.value);
        if (machine) await startMachine(machine);
    });

    // Control buttons
    document.getElementById("btn-reset").addEventListener("click", () => {
        module.ccall("wasm_reset", null, ["number"], [0]);
    });

    document.getElementById("btn-power").addEventListener("click", () => {
        module.ccall("wasm_reset", null, ["number"], [1]);
    });

    const volume = document.getElementById("volume");
    volume.addEventListener("input", (e) => {
        unlockAudio();
        module.ccall("wasm_set_volume", null, ["number"], [parseInt(e.target.value)]);
    });
    releaseFocus(volume);

    // The machine named in the address, otherwise the one of the previous
    // visit, starts by itself - after the first click or key press, so that
    // the browser lets it be heard from the very beginning
    let first = null;
    let remember = true;
    if (urlParams.machine !== null) {
        first = findMachine(machines, urlParams.machine);
        remember = false;
        if (!first) setStatus("stUrlMachine", "error", urlParams.machine);
    } else {
        first = machines.find((m) => m.id === settings.get("machine", "")) || null;
    }
    if (first && await startMachine(first, remember, true) && urlParams.keyboard) {
        // Every load closes the panel, so it is opened after this one. A
        // machine without a drawing has no button, and nothing opens
        const button = document.getElementById("btn-keyboard");
        if (!button.hidden && !button.disabled && document.getElementById("kbd-panel").hidden) button.click();
    }
}

// Start when DOM is ready
if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", initEcat);
} else {
    initEcat();
}
