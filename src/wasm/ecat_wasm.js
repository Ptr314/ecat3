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
]);

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
    function onKey(event, press) {
        // Don't intercept when focused on UI controls
        if (event.target.tagName === "SELECT" || event.target.tagName === "INPUT") return;

        let emuKey = translateKeyEvent(event);
        if (emuKey === null) return;

        let mods = getModifiers(event);
        module.ccall("wasm_key_event", null,
            ["number", "number", "number"],
            [emuKey, mods, press ? 1 : 0]);

        if (INTERCEPT_KEYS.has(event.code)) {
            event.preventDefault();
        }
    }

    document.addEventListener("keydown", (e) => onKey(e, true));
    document.addEventListener("keyup", (e) => onKey(e, false));
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

async function loadMachine(module, machinePath, bundleUrl, dataBundleUrl) {
    let statusEl = document.getElementById("status");

    try {
        statusEl.textContent = "Loading machine assets...";
        statusEl.className = "loading";

        // Load shared data bundle (charmaps, keyboard maps) if provided
        if (dataBundleUrl) {
            console.log("Loading data bundle:", dataBundleUrl);
            await fetchAndMount(module, dataBundleUrl, "/data");
        }

        // Load machine-specific bundle
        console.log("Loading machine bundle:", bundleUrl);
        await fetchAndMount(module, bundleUrl, "");

        console.log("Bundles loaded. Calling wasm_load_machine:", machinePath);
        statusEl.textContent = "Starting emulation...";

        let result = module.ccall("wasm_load_machine", "number", ["string"], [machinePath]);
        if (result !== 0) {
            statusEl.textContent = "Failed to load machine (error " + result + ")";
            statusEl.className = "error";
            return;
        }

        statusEl.textContent = "Running";
        statusEl.className = "";

        // Resume audio context (miniaudio creates it but browsers suspend it without user gesture)
        if (window.miniaudio && window.miniaudio.unlock) {
            window.miniaudio.unlock();
        }

        // Enable controls
        document.getElementById("btn-reset").disabled = false;
        document.getElementById("btn-cold-reset").disabled = false;
        document.getElementById("disk-file").disabled = false;

    } catch (err) {
        console.error("loadMachine error:", err);
        let msg = (err instanceof Error) ? err.message : String(err);
        statusEl.textContent = "Error: " + msg;
        statusEl.className = "error";
    }
}

// ============================================================================
// Disk image loading
// ============================================================================

function setupDiskLoader(module) {
    let fileInput = document.getElementById("disk-file");
    fileInput.addEventListener("change", async (event) => {
        let file = event.target.files[0];
        if (!file) return;

        let statusEl = document.getElementById("status");
        statusEl.textContent = "Loading disk image: " + file.name;
        statusEl.className = "loading";

        try {
            let buffer = await file.arrayBuffer();
            let data = new Uint8Array(buffer);

            // Write file to Emscripten virtual FS, then tell C++ to load it
            let tempPath = "/tmp/" + file.name;
            module.FS.writeFile(tempPath, data);

            // Default to fdd0 - user can be prompted for device selection in future
            let result = module.ccall("wasm_load_file", "number",
                ["string", "string"],
                ["fdd0", tempPath]);

            if (result === 0) {
                statusEl.textContent = "Disk image loaded: " + file.name;
                statusEl.className = "";
            } else {
                statusEl.textContent = "Failed to load disk image (error " + result + ")";
                statusEl.className = "error";
            }
        } catch (err) {
            console.error("Disk load error:", err);
            statusEl.textContent = "Error: " + err.message;
            statusEl.className = "error";
        }

        // Reset file input so the same file can be loaded again
        fileInput.value = "";
    });
}

// ============================================================================
// Canvas scaling
// ============================================================================

function setupCanvasScaling() {
    let canvas = document.getElementById("canvas");
    let container = document.getElementById("canvas-container");

    function updateScale() {
        let cw = container.clientWidth - 32;
        let ch = container.clientHeight - 32;
        let canvasW = canvas.width;
        let canvasH = canvas.height;
        if (canvasW === 0 || canvasH === 0) return;

        // Apply 4:3 aspect ratio correction
        let displayW = canvasW;
        let displayH = canvasH;
        let aspectRatio = (4/3) / (canvasW / canvasH);
        if (aspectRatio > 1) {
            displayW = Math.round(canvasW * aspectRatio);
        } else {
            displayH = Math.round(canvasH / aspectRatio);
        }

        let scale = Math.min(cw / displayW, ch / displayH);
        scale = Math.max(1, Math.floor(scale)); // Integer scaling

        canvas.style.width = (displayW * scale) + "px";
        canvas.style.height = (displayH * scale) + "px";
    }

    window.addEventListener("resize", updateScale);
    new MutationObserver(updateScale).observe(canvas, { attributes: true, attributeFilter: ["width", "height"] });
    updateScale();
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

function setupAudioActivation() {
    let overlay = document.getElementById("overlay");

    function activate() {
        overlay.classList.add("hidden");
        unlockAudio();
    }

    overlay.addEventListener("click", activate);
    overlay.addEventListener("touchstart", activate);

    document.addEventListener("keydown", unlockAudio);
    document.addEventListener("mousedown", unlockAudio);
    document.addEventListener("touchstart", unlockAudio);
}

// ============================================================================
// Initialization
// ============================================================================

async function initEcat() {
    let statusEl = document.getElementById("status");
    let selectEl = document.getElementById("machine-select");

    setupAudioActivation();
    setupCanvasScaling();

    // Load machines manifest
    statusEl.textContent = "Loading machine list...";
    statusEl.className = "loading";

    let machines;
    try {
        let resp = await fetch("machines.json");
        machines = await resp.json();
    } catch (err) {
        statusEl.textContent = "Failed to load machines.json: " + err.message;
        statusEl.className = "error";
        return;
    }

    // Populate dropdown
    selectEl.innerHTML = '<option value="">-- Select a machine --</option>';
    for (let m of machines) {
        let opt = document.createElement("option");
        opt.value = m.id;
        opt.textContent = m.name;
        selectEl.appendChild(opt);
    }

    // Initialize WASM module
    statusEl.textContent = "Loading WASM module...";

    let module;
    try {
        module = await EmuModule({
            print: (text) => console.log("eCat3:", text),
            printErr: (text) => console.error("eCat3:", text),
        });
    } catch (err) {
        statusEl.textContent = "Failed to load WASM: " + err.message;
        statusEl.className = "error";
        return;
    }

    // Create required FS directories
    mkdirRecursive(module, "/computers");
    mkdirRecursive(module, "/data");
    mkdirRecursive(module, "/software");
    mkdirRecursive(module, "/tmp");

    // Write default ecat.ini
    module.FS.writeFile("/ecat.ini", [
        "[Core]",
        "TimerResolution=1",
        "TimerDelay=20",
        "mapper_cache=8",
        "",
        "[Video]",
        "scale=2",
        "ratio=1",
        "filtering=0",
        ""
    ].join("\n"));

    setupKeyboard(module);
    setupDiskLoader(module);

    selectEl.disabled = false;
    statusEl.textContent = "Ready. Select a machine to start.";
    statusEl.className = "";

    // Machine selection handler
    selectEl.addEventListener("change", async () => {
        let machineId = selectEl.value;
        if (!machineId) return;

        selectEl.disabled = true;
        let machine = machines.find(m => m.id === machineId);
        if (!machine) return;

        await loadMachine(module, machine.cfg_path, machine.bundle_url, machine.data_bundle_url || null);
        selectEl.disabled = false;
    });

    // Control buttons
    document.getElementById("btn-reset").addEventListener("click", () => {
        module.ccall("wasm_reset", null, ["number"], [0]);
    });

    document.getElementById("btn-cold-reset").addEventListener("click", () => {
        module.ccall("wasm_reset", null, ["number"], [1]);
    });

    document.getElementById("volume").addEventListener("input", (e) => {
        unlockAudio();
        module.ccall("wasm_set_volume", null, ["number"], [parseInt(e.target.value)]);
    });
}

// Start when DOM is ready
if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", initEcat);
} else {
    initEcat();
}
