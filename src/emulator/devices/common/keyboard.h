// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Abstract keyboard device

#pragma once

#include "emulator/core.h"

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

class Keyboard: public ComputerDevice
{
protected:
    bool rus_mode;
    bool use_remap = true;
    unsigned int translate_key(const std::string &key);
    bool known_key(unsigned int code);
    unsigned int rus_translate(unsigned int code);
    virtual void set_rus(bool new_rus);

public:
    Keyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    virtual void key_event(unsigned int key, unsigned int native_key, bool press);
    virtual void key_down(unsigned int key) = 0;
    virtual void key_up(unsigned int key) = 0;
};