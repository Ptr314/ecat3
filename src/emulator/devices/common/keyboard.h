#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "emulator/core.h"

struct KeyDescription {
    unsigned int code;
    QString name;
};

static const KeyDescription KEYS[] ={
    {.code = Qt::Key_Space, .name = "space"},
    {.code = Qt::Key_Backspace, .name = "back"},
    {.code = Qt::Key_Return, .name = "ret"},
    {.code = Qt::Key_Shift, .name = "shift"},
    {.code = Qt::Key_Control, .name = "ctrl"},
    {.code = Qt::Key_Alt, .name = "alt"},
    {.code = Qt::Key_CapsLock, .name = "caps"},
    {.code = Qt::Key_Escape, .name = "esc"},
    {.code = Qt::Key_PageUp, .name = "pgup"},
    {.code = Qt::Key_PageDown, .name = "pgdn"},
    {.code = Qt::Key_End, .name = "end"},
    {.code = Qt::Key_Home, .name = "home"},
    {.code = Qt::Key_Left, .name = "left"},
    {.code = Qt::Key_Up, .name = "up"},
    {.code = Qt::Key_Right, .name = "right"},
    {.code = Qt::Key_Down, .name = "down"},
    {.code = Qt::Key_Insert, .name = "ins"},
    {.code = Qt::Key_Delete, .name = "del"},
    //{.code = Qt::NUMPAD0, .name = "num0"},
    //{.code = Qt::NUMPAD1, .name = "num1"},
    //{.code = Qt::NUMPAD2, .name = "num2"},
    //{.code = Qt::NUMPAD3, .name = "num3"},
    //{.code = Qt::NUMPAD4, .name = "num4"},
    //{.code = Qt::NUMPAD5, .name = "num5"},
    //{.code = Qt::NUMPAD6, .name = "num6"},
    //{.code = Qt::NUMPAD7, .name = "num7"},
    //{.code = Qt::NUMPAD8, .name = "num8"},
    //{.code = Qt::NUMPAD9, .name = "num9"},
    {.code = Qt::Key_Asterisk, .name = "mult"},
    {.code = Qt::Key_Plus, .name = "plus"},
    {.code = Qt::Key_Period, .name = "del2"},
    //{.code = Qt::Key_Minus, .name = "minus"},
    {.code = Qt::Key_Enter, .name = "ret2"},
    //{.code = Qt::Key_Slash, .name = "div"},
    {.code = Qt::Key_F1, .name = "f1"},
    {.code = Qt::Key_F2, .name = "f2"},
    {.code = Qt::Key_F3, .name = "f3"},
    {.code = Qt::Key_F4, .name = "f4"},
    {.code = Qt::Key_F5, .name = "f5"},
    {.code = Qt::Key_F6, .name = "f6"},
    {.code = Qt::Key_F7, .name = "f7"},
    {.code = Qt::Key_F8, .name = "f8"},
    {.code = Qt::Key_F9, .name = "f9"},
    {.code = Qt::Key_F10, .name = "f10"},
    {.code = Qt::Key_F11, .name = "f11"},
    {.code = Qt::Key_F12, .name = "f12"},
    {.code = Qt::Key_NumLock, .name = "num"},
    {.code = Qt::Key_ScrollLock, .name = "scroll"},
    {.code = Qt::Key_0, .name = "0"},
    {.code = Qt::Key_1, .name = "1"},
    {.code = Qt::Key_2, .name = "2"},
    {.code = Qt::Key_3, .name = "3"},
    {.code = Qt::Key_4, .name = "4"},
    {.code = Qt::Key_5, .name = "5"},
    {.code = Qt::Key_6, .name = "6"},
    {.code = Qt::Key_7, .name = "7"},
    {.code = Qt::Key_8, .name = "8"},
    {.code = Qt::Key_9, .name = "9"},
    {.code = Qt::Key_A, .name = "A"},
    {.code = Qt::Key_B, .name = "B"},
    {.code = Qt::Key_C, .name = "C"},
    {.code = Qt::Key_D, .name = "D"},
    {.code = Qt::Key_E, .name = "E"},
    {.code = Qt::Key_F, .name = "F"},
    {.code = Qt::Key_G, .name = "G"},
    {.code = Qt::Key_H, .name = "H"},
    {.code = Qt::Key_I, .name = "I"},
    {.code = Qt::Key_J, .name = "J"},
    {.code = Qt::Key_K, .name = "K"},
    {.code = Qt::Key_L, .name = "L"},
    {.code = Qt::Key_M, .name = "M"},
    {.code = Qt::Key_N, .name = "N"},
    {.code = Qt::Key_O, .name = "O"},
    {.code = Qt::Key_P, .name = "P"},
    {.code = Qt::Key_Q, .name = "Q"},
    {.code = Qt::Key_R, .name = "R"},
    {.code = Qt::Key_S, .name = "S"},
    {.code = Qt::Key_T, .name = "T"},
    {.code = Qt::Key_U, .name = "U"},
    {.code = Qt::Key_V, .name = "V"},
    {.code = Qt::Key_W, .name = "W"},
    {.code = Qt::Key_X, .name = "X"},
    {.code = Qt::Key_Y, .name = "Y"},
    {.code = Qt::Key_Z, .name = "Z"},
    {.code = Qt::Key_V, .name = "V"},
    {.code = Qt::Key_QuoteLeft, .name = "~"},
    {.code = Qt::Key_Minus, .name = "-"},
    {.code = Qt::Key_Equal, .name = "="},
    {.code = Qt::Key_Backslash, .name = "\\"},
    {.code = Qt::Key_BracketLeft, .name = "["},
    {.code = Qt::Key_BracketRight, .name = "]"},
    {.code = Qt::Key_Semicolon, .name = ";"},
    //{.code = 222, .name = """},
    {.code = Qt::Key_Comma, .name = ","},
    {.code = Qt::Key_Period, .name = "."},
    {.code = Qt::Key_Slash, .name = "/"},
    {.code = Qt::Key_Tab, .name = "tab"}
};

class Keyboard: public ComputerDevice
{
protected:
    unsigned int translate_key(QString key);
    bool known_key(unsigned int code);

public:
    Keyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void key_event(QKeyEvent *event, bool press);
    virtual void key_down(unsigned int key) = 0;
    virtual void key_up(unsigned int key) = 0;
};

#endif // KEYBOARD_H
