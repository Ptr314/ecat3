// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Abstract keyboard device

#pragma once

#include <QKeyEvent>

#include "emulator/core.h"

#define SHIFT_STATE_KEEP    0
#define SHIFT_STATE_ON      1
#define SHIFT_STATE_OFF     2

struct KeyDescription {
    unsigned int code;
    std::string name;
};

static const KeyDescription KEYS[] ={
    {Qt::Key_Space, "space"},
    {Qt::Key_Backspace, "back"},
    {Qt::Key_Return, "ret"},
    {Qt::Key_Enter, "enter"},
    {Qt::Key_Shift, "shift"},
    {Qt::Key_Control, "ctrl"},
    {Qt::Key_Alt, "alt"},
    {Qt::Key_CapsLock, "caps"},
    {Qt::Key_Escape, "esc"},
    {Qt::Key_PageUp, "pgup"},
    {Qt::Key_PageDown, "pgdn"},
    {Qt::Key_End, "end"},
    {Qt::Key_Home, "home"},
    {Qt::Key_Left, "left"},
    {Qt::Key_Up, "up"},
    {Qt::Key_Right, "right"},
    {Qt::Key_Down, "down"},
    {Qt::Key_Insert, "ins"},
    {Qt::Key_Delete, "del"},
    {Qt::Key_ScrollLock, "scroll"},
    //{Qt::NUMPAD0, "num0"},
    //{Qt::NUMPAD1, "num1"},
    //{Qt::NUMPAD2, "num2"},
    //{Qt::NUMPAD3, "num3"},
    //{Qt::NUMPAD4, "num4"},
    //{Qt::NUMPAD5, "num5"},
    //{Qt::NUMPAD6, "num6"},
    //{Qt::NUMPAD7, "num7"},
    //{Qt::NUMPAD8, "num8"},
    //{Qt::NUMPAD9, "num9"},
    {Qt::Key_Asterisk, "mult"},
    {Qt::Key_Plus, "plus"},
    {Qt::Key_Plus, "+"},
    {Qt::Key_Period, "del2"},
    {Qt::Key_Minus, "minus"},
    {Qt::Key_Enter, "ret2"},
    {Qt::Key_Slash, "div"},
    {Qt::Key_F1, "f1"},
    {Qt::Key_F2, "f2"},
    {Qt::Key_F3, "f3"},
    {Qt::Key_F4, "f4"},
    {Qt::Key_F5, "f5"},
    {Qt::Key_F6, "f6"},
    {Qt::Key_F7, "f7"},
    {Qt::Key_F8, "f8"},
    {Qt::Key_F9, "f9"},
    {Qt::Key_F10, "f10"},
    {Qt::Key_F11, "f11"},
    {Qt::Key_F12, "f12"},
    {Qt::Key_NumLock, "num"},
    {Qt::Key_ScrollLock, "scroll"},
    {Qt::Key_0, "0"},
    {Qt::Key_1, "1"},
    {Qt::Key_2, "2"},
    {Qt::Key_3, "3"},
    {Qt::Key_4, "4"},
    {Qt::Key_5, "5"},
    {Qt::Key_6, "6"},
    {Qt::Key_7, "7"},
    {Qt::Key_8, "8"},
    {Qt::Key_9, "9"},
    {Qt::Key_A, "A"},
    {Qt::Key_B, "B"},
    {Qt::Key_C, "C"},
    {Qt::Key_D, "D"},
    {Qt::Key_E, "E"},
    {Qt::Key_F, "F"},
    {Qt::Key_G, "G"},
    {Qt::Key_H, "H"},
    {Qt::Key_I, "I"},
    {Qt::Key_J, "J"},
    {Qt::Key_K, "K"},
    {Qt::Key_L, "L"},
    {Qt::Key_M, "M"},
    {Qt::Key_N, "N"},
    {Qt::Key_O, "O"},
    {Qt::Key_P, "P"},
    {Qt::Key_Q, "Q"},
    {Qt::Key_R, "R"},
    {Qt::Key_S, "S"},
    {Qt::Key_T, "T"},
    {Qt::Key_U, "U"},
    {Qt::Key_V, "V"},
    {Qt::Key_W, "W"},
    {Qt::Key_X, "X"},
    {Qt::Key_Y, "Y"},
    {Qt::Key_Z, "Z"},
    {Qt::Key_V, "V"},
    {Qt::Key_QuoteLeft, "~"},
    {Qt::Key_Minus, "-"},
    {Qt::Key_Equal, "="},
    {Qt::Key_Backslash, "\\"},
    {Qt::Key_BracketLeft, "["},
    {Qt::Key_BracketRight, "]"},
    {Qt::Key_Semicolon, ";"},
    {Qt::Key_Semicolon, "semicolon"},
    {Qt::Key_Colon, ":"},
    {Qt::Key_Colon, "colon"},
    {Qt::Key_Exclam, "!"},
    {Qt::Key_QuoteDbl, "\""},
    {Qt::Key_NumberSign, "#"},
    {Qt::Key_Dollar, "$"},
    {Qt::Key_Comma, ","},
    {Qt::Key_Percent, "%"},
    {Qt::Key_Ampersand, "&"},
    {Qt::Key_Apostrophe, "'"},
    {Qt::Key_ParenLeft, "("},
    {Qt::Key_ParenRight, ")"},
    {Qt::Key_Asterisk, "*"},
    {Qt::Key_Period, "."},
    {Qt::Key_Greater, ">"},
    {Qt::Key_Less, "<"},
    {Qt::Key_Slash, "/"},
    {Qt::Key_Slash, "slash"},
    {Qt::Key_Question, "?"},
    {Qt::Key_Tab, "tab"},
    {Qt::Key_At, "@"},
    {Qt::Key_BraceLeft, "{"},
    {Qt::Key_BraceRight, "}"},
    {Qt::Key_Underscore, "_"},
    {Qt::Key_Underscore, "under"},
    {Qt::Key_AsciiCircum, "^"},
    {Qt::Key_AsciiCircum, "circum"},
    {Qt::Key_Bar, "bar"},
};

static const unsigned int RUS_REMAP[][2] = {
    {Qt::Key_Q, Qt::Key_J},                     // Й
    {Qt::Key_W, Qt::Key_C},                     // Ц
    {Qt::Key_E, Qt::Key_U},                     // У
    {Qt::Key_R, Qt::Key_K},                     // К
    {Qt::Key_T, Qt::Key_E},                     // Е
    {Qt::Key_Y, Qt::Key_N},                     // Н
    {Qt::Key_U, Qt::Key_G},                     // Г
    {Qt::Key_I, Qt::Key_BracketLeft},           // Ш
    {Qt::Key_O, Qt::Key_BracketRight},          // Ш
    {Qt::Key_P, Qt::Key_Z},                     // З
    {Qt::Key_BracketLeft, Qt::Key_H},           // Х
    {Qt::Key_BracketRight, Qt::Key_Underscore}, // Ъ

    {Qt::Key_A, Qt::Key_F},                     // Ф
    {Qt::Key_S, Qt::Key_Y},                     // Ы
    {Qt::Key_D, Qt::Key_W},                     // В
    {Qt::Key_F, Qt::Key_A},                     // А
    {Qt::Key_G, Qt::Key_P},                     // П
    {Qt::Key_H, Qt::Key_R},                     // Р
    {Qt::Key_J, Qt::Key_O},                     // О
    {Qt::Key_K, Qt::Key_L},                     // Л
    {Qt::Key_L, Qt::Key_D},                     // Д
    {Qt::Key_Semicolon, Qt::Key_V},             // Ж
    {Qt::Key_Apostrophe, Qt::Key_Backslash},    // Э

    {Qt::Key_Z, Qt::Key_Q},                     // Я
    {Qt::Key_X, Qt::Key_AsciiCircum},           // Ч
    {Qt::Key_C, Qt::Key_S},                     // С
    {Qt::Key_V, Qt::Key_M},                     // М
    {Qt::Key_B, Qt::Key_I},                     // И
    {Qt::Key_N, Qt::Key_T},                     // Т
    {Qt::Key_M, Qt::Key_X},                     // Ь
    {Qt::Key_Comma, Qt::Key_B},                 // Б
    {Qt::Key_Period, Qt::Key_At},               // Ю
    {Qt::Key_Slash, Qt::Key_Period},            // Точка
    {Qt::Key_Question, Qt::Key_Comma},          // Запятая
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
    virtual void key_event(QKeyEvent *event, bool press);
    virtual void key_down(unsigned int key) = 0;
    virtual void key_up(unsigned int key) = 0;
};
