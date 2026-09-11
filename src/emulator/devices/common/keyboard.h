// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Abstract keyboard device

#pragma once

#include "emulator/core.h"
#include "emulator/thread_compat.h"

#define SHIFT_STATE_KEEP    0
#define SHIFT_STATE_ON      1
#define SHIFT_STATE_OFF     2

// Key codes compatible with Qt::Key values, so that the Qt frontend
// can pass event->key() directly while Wasm/SDL frontends use the same constants.
namespace EmuKey {
    enum Key {
        Space        = 0x20,
        Exclam       = 0x21,
        QuoteDbl     = 0x22,
        NumberSign   = 0x23,
        Dollar       = 0x24,
        Percent      = 0x25,
        Ampersand    = 0x26,
        Apostrophe   = 0x27,
        ParenLeft    = 0x28,
        ParenRight   = 0x29,
        Asterisk     = 0x2a,
        Plus         = 0x2b,
        Comma        = 0x2c,
        Minus        = 0x2d,
        Period       = 0x2e,
        Slash        = 0x2f,
        Key_0        = 0x30,
        Key_1        = 0x31,
        Key_2        = 0x32,
        Key_3        = 0x33,
        Key_4        = 0x34,
        Key_5        = 0x35,
        Key_6        = 0x36,
        Key_7        = 0x37,
        Key_8        = 0x38,
        Key_9        = 0x39,
        Colon        = 0x3a,
        Semicolon    = 0x3b,
        Less         = 0x3c,
        Equal        = 0x3d,
        Greater      = 0x3e,
        Question     = 0x3f,
        At           = 0x40,
        Key_A        = 0x41,
        Key_B        = 0x42,
        Key_C        = 0x43,
        Key_D        = 0x44,
        Key_E        = 0x45,
        Key_F        = 0x46,
        Key_G        = 0x47,
        Key_H        = 0x48,
        Key_I        = 0x49,
        Key_J        = 0x4a,
        Key_K        = 0x4b,
        Key_L        = 0x4c,
        Key_M        = 0x4d,
        Key_N        = 0x4e,
        Key_O        = 0x4f,
        Key_P        = 0x50,
        Key_Q        = 0x51,
        Key_R        = 0x52,
        Key_S        = 0x53,
        Key_T        = 0x54,
        Key_U        = 0x55,
        Key_V        = 0x56,
        Key_W        = 0x57,
        Key_X        = 0x58,
        Key_Y        = 0x59,
        Key_Z        = 0x5a,
        BracketLeft  = 0x5b,
        Backslash    = 0x5c,
        BracketRight = 0x5d,
        AsciiCircum  = 0x5e,
        Underscore   = 0x5f,
        QuoteLeft    = 0x60,
        BraceLeft    = 0x7b,
        Bar          = 0x7c,
        BraceRight   = 0x7d,
        AsciiTilde   = 0x7e,
        // Non-ASCII keys (matching Qt::Key values)
        Escape       = 0x01000000,
        Tab          = 0x01000001,
        Backspace    = 0x01000003,
        Return       = 0x01000004,
        Enter        = 0x01000005,
        Insert       = 0x01000006,
        Delete       = 0x01000007,
        Home         = 0x01000010,
        End          = 0x01000011,
        Left         = 0x01000012,
        Up           = 0x01000013,
        Right        = 0x01000014,
        Down         = 0x01000015,
        PageUp       = 0x01000016,
        PageDown     = 0x01000017,
        Shift        = 0x01000020,
        Control      = 0x01000021,
        Alt          = 0x01000023,
        CapsLock     = 0x01000024,
        NumLock      = 0x01000025,
        ScrollLock   = 0x01000026,
        F1           = 0x01000030,
        F2           = 0x01000031,
        F3           = 0x01000032,
        F4           = 0x01000033,
        F5           = 0x01000034,
        F6           = 0x01000035,
        F7           = 0x01000036,
        F8           = 0x01000037,
        F9           = 0x01000038,
        F10          = 0x01000039,
        F11          = 0x0100003a,
        F12          = 0x0100003b,
        Cancel       = 0x01020001,
    };

    enum Modifier {
        NoModifier      = 0x00000000,
        ShiftModifier   = 0x02000000,
        ControlModifier = 0x04000000,
        AltModifier     = 0x08000000,
    };
}

struct KeyDescription {
    unsigned int code;
    std::string name;
};

static const KeyDescription KEYS[] ={
    {EmuKey::Space, "space"},
    {EmuKey::Backspace, "back"},
    {EmuKey::Return, "ret"},
    {EmuKey::Enter, "enter"},
    {EmuKey::Shift, "shift"},
    {EmuKey::Control, "ctrl"},
    {EmuKey::Alt, "alt"},
    {EmuKey::CapsLock, "caps"},
    {EmuKey::Escape, "esc"},
    {EmuKey::PageUp, "pgup"},
    {EmuKey::PageDown, "pgdn"},
    {EmuKey::End, "end"},
    {EmuKey::Home, "home"},
    {EmuKey::Left, "left"},
    {EmuKey::Up, "up"},
    {EmuKey::Right, "right"},
    {EmuKey::Down, "down"},
    {EmuKey::Insert, "ins"},
    {EmuKey::Delete, "del"},
    {EmuKey::ScrollLock, "scroll"},
    {EmuKey::Asterisk, "mult"},
    {EmuKey::Plus, "plus"},
    {EmuKey::Plus, "+"},
    {EmuKey::Period, "del2"},
    {EmuKey::Minus, "minus"},
    {EmuKey::Enter, "ret2"},
    {EmuKey::Slash, "div"},
    {EmuKey::F1, "f1"},
    {EmuKey::F2, "f2"},
    {EmuKey::F3, "f3"},
    {EmuKey::F4, "f4"},
    {EmuKey::F5, "f5"},
    {EmuKey::F6, "f6"},
    {EmuKey::F7, "f7"},
    {EmuKey::F8, "f8"},
    {EmuKey::F9, "f9"},
    {EmuKey::F10, "f10"},
    {EmuKey::F11, "f11"},
    {EmuKey::F12, "f12"},
    {EmuKey::NumLock, "num"},
    {EmuKey::ScrollLock, "scroll"},
    {EmuKey::Key_0, "0"},
    {EmuKey::Key_1, "1"},
    {EmuKey::Key_2, "2"},
    {EmuKey::Key_3, "3"},
    {EmuKey::Key_4, "4"},
    {EmuKey::Key_5, "5"},
    {EmuKey::Key_6, "6"},
    {EmuKey::Key_7, "7"},
    {EmuKey::Key_8, "8"},
    {EmuKey::Key_9, "9"},
    {EmuKey::Key_A, "A"},
    {EmuKey::Key_B, "B"},
    {EmuKey::Key_C, "C"},
    {EmuKey::Key_D, "D"},
    {EmuKey::Key_E, "E"},
    {EmuKey::Key_F, "F"},
    {EmuKey::Key_G, "G"},
    {EmuKey::Key_H, "H"},
    {EmuKey::Key_I, "I"},
    {EmuKey::Key_J, "J"},
    {EmuKey::Key_K, "K"},
    {EmuKey::Key_L, "L"},
    {EmuKey::Key_M, "M"},
    {EmuKey::Key_N, "N"},
    {EmuKey::Key_O, "O"},
    {EmuKey::Key_P, "P"},
    {EmuKey::Key_Q, "Q"},
    {EmuKey::Key_R, "R"},
    {EmuKey::Key_S, "S"},
    {EmuKey::Key_T, "T"},
    {EmuKey::Key_U, "U"},
    {EmuKey::Key_V, "V"},
    {EmuKey::Key_W, "W"},
    {EmuKey::Key_X, "X"},
    {EmuKey::Key_Y, "Y"},
    {EmuKey::Key_Z, "Z"},
    {EmuKey::Key_V, "V"},
    {EmuKey::QuoteLeft, "~"},
    {EmuKey::Minus, "-"},
    {EmuKey::Equal, "="},
    {EmuKey::Backslash, "\\"},
    {EmuKey::BracketLeft, "["},
    {EmuKey::BracketRight, "]"},
    {EmuKey::Semicolon, ";"},
    {EmuKey::Semicolon, "semicolon"},
    {EmuKey::Colon, ":"},
    {EmuKey::Colon, "colon"},
    {EmuKey::Exclam, "!"},
    {EmuKey::QuoteDbl, "\""},
    {EmuKey::NumberSign, "#"},
    {EmuKey::Dollar, "$"},
    {EmuKey::Comma, ","},
    {EmuKey::Percent, "%"},
    {EmuKey::Ampersand, "&"},
    {EmuKey::Apostrophe, "'"},
    {EmuKey::ParenLeft, "("},
    {EmuKey::ParenRight, ")"},
    {EmuKey::Asterisk, "*"},
    {EmuKey::Period, "."},
    {EmuKey::Greater, ">"},
    {EmuKey::Less, "<"},
    {EmuKey::Slash, "/"},
    {EmuKey::Slash, "slash"},
    {EmuKey::Question, "?"},
    {EmuKey::Tab, "tab"},
    {EmuKey::At, "@"},
    {EmuKey::BraceLeft, "{"},
    {EmuKey::BraceRight, "}"},
    {EmuKey::Underscore, "_"},
    {EmuKey::Underscore, "under"},
    {EmuKey::AsciiCircum, "^"},
    {EmuKey::AsciiCircum, "circum"},
    {EmuKey::Bar, "bar"},
};

static const unsigned int RUS_REMAP[][2] = {
    {EmuKey::Key_Q, EmuKey::Key_J},                     // Й
    {EmuKey::Key_W, EmuKey::Key_C},                     // Ц
    {EmuKey::Key_E, EmuKey::Key_U},                     // У
    {EmuKey::Key_R, EmuKey::Key_K},                     // К
    {EmuKey::Key_T, EmuKey::Key_E},                     // Е
    {EmuKey::Key_Y, EmuKey::Key_N},                     // Н
    {EmuKey::Key_U, EmuKey::Key_G},                     // Г
    {EmuKey::Key_I, EmuKey::BracketLeft},               // Ш
    {EmuKey::Key_O, EmuKey::BracketRight},              // Щ
    {EmuKey::Key_P, EmuKey::Key_Z},                     // З
    {EmuKey::BracketLeft, EmuKey::Key_H},               // Х
    {EmuKey::BracketRight, EmuKey::Underscore},         // Ъ

    {EmuKey::Key_A, EmuKey::Key_F},                     // Ф
    {EmuKey::Key_S, EmuKey::Key_Y},                     // Ы
    {EmuKey::Key_D, EmuKey::Key_W},                     // В
    {EmuKey::Key_F, EmuKey::Key_A},                     // А
    {EmuKey::Key_G, EmuKey::Key_P},                     // П
    {EmuKey::Key_H, EmuKey::Key_R},                     // Р
    {EmuKey::Key_J, EmuKey::Key_O},                     // О
    {EmuKey::Key_K, EmuKey::Key_L},                     // Л
    {EmuKey::Key_L, EmuKey::Key_D},                     // Д
    {EmuKey::Semicolon, EmuKey::Key_V},                 // Ж
    {EmuKey::Apostrophe, EmuKey::Backslash},            // Э

    {EmuKey::Key_Z, EmuKey::Key_Q},                     // Я
    {EmuKey::Key_X, EmuKey::AsciiCircum},               // Ч
    {EmuKey::Key_C, EmuKey::Key_S},                     // С
    {EmuKey::Key_V, EmuKey::Key_M},                     // М
    {EmuKey::Key_B, EmuKey::Key_I},                     // И
    {EmuKey::Key_N, EmuKey::Key_T},                     // Т
    {EmuKey::Key_M, EmuKey::Key_X},                     // Ь
    {EmuKey::Comma, EmuKey::Key_B},                     // Б
    {EmuKey::Period, EmuKey::At},                        // Ю
    {EmuKey::Slash, EmuKey::Period},                     // Точка
    {EmuKey::Question, EmuKey::Comma},                   // Запятая
};

#define RUS_REMAP_SIZE (sizeof(RUS_REMAP) / sizeof(unsigned int) / 2)

// Resolves a key name from the KEYS[] table above into an EmuKey code.
// Case insensitive, returns _FFFF when the name is unknown.
// Used by keyboard layouts and by the scripting engine (KEY and TYPE).
unsigned int translate_key_name(const std::string &key);

// The reverse of translate_key_name(): the script name of an EmuKey code.
// A code may have several names in KEYS[]; the single character one wins
// ("." over "del2", "*" over "mult"), otherwise the first entry is taken.
// Empty string for a code that has no name. Used by the script recorder.
std::string key_name(unsigned int code);

// What a key of the machine's own keyboard does. Everything but KEY_ROLE_NORMAL
// is declared in the header of the native key table (shift:, ctrl:, rus: ...).
enum KeyRole {
    KEY_ROLE_NORMAL = 0,
    KEY_ROLE_SHIFT,         // momentary upper register
    KEY_ROLE_CTRL,          // momentary control
    KEY_ROLE_RUS_TOGGLE,    // one key flipping the alphabet
    KEY_ROLE_RUS_ON,        // separate РУС, like the БК has
    KEY_ROLE_RUS_OFF,       // separate ЛАТ
    KEY_ROLE_RUS_LINE,      // РУС/ЛАТ on a line of its own, the firmware keeps the register (Микроша)
    KEY_ROLE_CASE_UPPER,    // latching capitals (БК: ЗАГЛ)
    KEY_ROLE_CASE_LOWER,    // latching small letters (БК: СТР)
    KEY_ROLE_ALT,           // second control key (БК: АР2), code goes through the alternative vector
    KEY_ROLE_STOP,          // drives the ~stop line instead of sending a code (БК: СТОП)
    KEY_ROLE_REPEAT,        // repeats whatever went last (БК: ПОВТ)
    KEY_ROLE_RESET          // restarts the machine instead of sending a code (Агат: СБР)
};

class Keyboard: public ComputerDevice
{
protected:
    // Some keys are not part of the code matrix at all: on the БК СТОП is wired
    // to the processor's HALT input, so it asserts a line and sends nothing.
    Interface i_stop;

    bool rus_mode;
    bool use_remap = true;
    unsigned int translate_key(const std::string &key);
    bool known_key(unsigned int code);
    unsigned int rus_translate(unsigned int code);
    virtual void set_rus(bool new_rus);

    // The drawing of this machine's keyboard and the table naming its keys.
    // Both are optional: a machine without them simply has no on-screen keyboard.
    std::string m_picture_file;
    std::vector<std::string> m_key_ids;
    std::vector<std::pair<std::string, KeyRole> > m_key_roles;

    // Ids the machine currently sees as held, in the machine's own naming.
    // Written by every input path, read by the frontends to light the drawing
    // up, so it is guarded: the GUI polls it while emulation presses keys.
    std::vector<std::string> m_ids_held;
    mutable compat_mutex m_held_mutex;

    // Set by a latching small-letters key (БК: СТР), cleared by the capitals
    // one (ЗАГЛ). This is not the momentary shift a subclass tracks: it applies
    // to letters alone, which is why digits keep working under it.
    bool m_case_lower = false;

    // How many times this keyboard has been reset, see reset_count()
    unsigned int m_reset_count = 0;

    void register_key_id(const std::string &id, KeyRole role = KEY_ROLE_NORMAL);
    void note_id(const std::string &id, bool press);
    emulator::Result load_key_table(SystemData *sd);

    // Body of the native key table, everything the header did not claim.
    // The format is per keyboard type, so the base class only splits the file.
    virtual emulator::Result parse_key_table(const std::vector<std::string> &body, const std::string &file);

    // A key of the machine that carries a code. Called for every key of the
    // drawing, modifiers included; an id the table does not know is ignored.
    virtual void send_key_id(const std::string &id, bool press) { (void)id; (void)press; }

    // Modifier state, held either by a momentary key or by a latching one.
    virtual void set_shift_state(bool pressed) { (void)pressed; }
    virtual void set_ctrl_state(bool pressed) { (void)pressed; }
    virtual void set_alt_state(bool pressed) { (void)pressed; }

    // Sends whatever the keyboard sent last, once more. The БК repeats it in
    // the keyboard controller; here the code goes out again and the "a key is
    // held" line stays up, which is what the monitor's own auto repeat watches.
    virtual void repeat_key(const std::string &id, bool press) { (void)id; (void)press; }

public:
    Keyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    virtual void key_event(unsigned int key, unsigned int native_key, bool press);
    virtual void key_down(unsigned int key) = 0;
    virtual void key_up(unsigned int key) = 0;

    // A key of the machine's own keyboard, named the way the native table and
    // the SVG drawing name it. Nothing here is a host key, so rus_translate()
    // must not run: the picture shows the machine's layout, and pressing Й on
    // an Орион drawing is EmuKey::Key_J already - remapping it again gives О.
    virtual void key_event_id(const std::string &id, bool press);

    const std::vector<std::string> & key_ids() const { return m_key_ids; }
    KeyRole key_role(const std::string &id) const;
    bool is_latching(const std::string &id) const;

    // How a pointer should treat this key on a drawing of the keyboard. A mouse
    // or a finger has one contact point, so a modifier the hardware expects to
    // be held down has to be clicked on and off instead of following the button.
    // Both frontends ask this, so the policy lives in one place.
    enum ClickMode {
        CLICK_HOLD = 0,     // ordinary key: down on press, up on release
        CLICK_TOGGLE,       // momentary modifier: click it on, click it off
        CLICK_TAP           // latch: pressed and let go at once, machine keeps the state
    };
    ClickMode click_mode(const std::string &id) const;

    // Whether a letter key should send its shifted code right now. The Rus
    // register inverts the latch, because КОИ-7 puts the Russian capitals
    // exactly where the Latin small letters are: in ЛАТ "$41" prints A and
    // "$61" prints a, in РУС "$41" prints а and "$61" prints А. Without the
    // inversion ЗАГЛ and СТР would swap meaning the moment РУС is pressed.
    bool case_shift() const { return m_case_lower != rus_mode; }
    std::vector<std::string> ids_held() const;
    const std::string & picture_file() const { return m_picture_file; }

    // Indicator lamps of the drawing. Unlike a key, a lamp shows what the
    // machine is in rather than what the user is doing, so it is derived from
    // state and never pressed. The names are a convention of the drawing: a
    // picture that has no such element simply ignores the answer, which is why
    // nothing changes for a machine that shows its register on a key instead
    // (the БК lights РУС and ЛАТ).
    struct Indicator {
        std::string id;     // element of the drawing
        bool        lit;
        // The key this lamp says the same thing as. A drawing that carries the
        // lamp should stop lighting that key: the register belongs on the
        // picture once, where the machine itself puts it.
        std::string key;
    };
    virtual std::vector<Indicator> indicators() const;

    // Grows by one on every reset of this device. A frontend that keeps a
    // mirror of the keyboard state - the on-screen keyboard latches modifiers
    // on its own, a pointer having only one contact point - watches it and
    // starts over: a reset drops whatever the machine was holding, and a
    // picture still showing СУ or УПР latched would send control codes into a
    // machine that thinks nothing is pressed.
    unsigned int reset_count() const { return m_reset_count; }

    // Tells whether the character is only reachable with Shift held on this
    // machine. The scripting engine uses it so that TYPE can produce quotes
    // and other symbols of the upper register.
    virtual bool needs_shift(unsigned int key) { (void)key; return false; }

    void reset(bool cool) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};