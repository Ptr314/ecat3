#ifndef DEBUG_H
#define DEBUG_H

#include "emulator.h"

using DebugWndCreateFunc = void (*)(QWidget * parent, Emulator * e, ComputerDevice * d);

struct DebugWndFuncData {
    QString device_type;
    DebugWndCreateFunc * f;
};

//DebugWndFuncData WndFuncData[100];

class DebugWindowsManager: public QObject
{
    Q_OBJECT
private:
    unsigned int count;
public:
    DebugWindowsManager():count(0){};

    void register_debug_window(QString device_type, DebugWndCreateFunc * f)
    {
        //WndFuncData[this->count++] = {.device_type = device_type, .f = f};
    };
};

//DebugWindowsManager DWM();

#endif // DEBUG_H
