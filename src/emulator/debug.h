#ifndef DEBUG_H
#define DEBUG_H

#include "emulator.h"

using DebugWndCreateFunc = QDialog * (QWidget * parent, Emulator * e, ComputerDevice * d);

struct DebugWndFuncData {
    QString device_type;
    DebugWndCreateFunc * f;
};

class DebugWindowsManager: public QObject
{
    Q_OBJECT

private:
    unsigned int count;
    DebugWndFuncData WndFuncData[100];

public:
    DebugWindowsManager():count(0){};

    void register_debug_window(QString device_type, DebugWndCreateFunc * f)
    {
        WndFuncData[this->count++] = {.device_type = device_type, .f = f};
    };

    DebugWndCreateFunc * get_create_func(QString device_type)
    {
        for (unsigned int i=0; i < this->count; i++)
            if (WndFuncData[i].device_type == device_type)
                return (WndFuncData[i].f);
        return nullptr;
    }
};

//DebugWindowsManager * DWM = new DebugWindowsManager();

#endif // DEBUG_H
