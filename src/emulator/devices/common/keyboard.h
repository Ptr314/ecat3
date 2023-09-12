#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "emulator/core.h"

struct KeyDescription {
    unsigned int code;
    QString name;
};

class Keyboard: public ComputerDevice
{
protected:
    unsigned int translate_key(QString key);

public:
    Keyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void key_down(unsigned int key) = 0;
    virtual void key_up(unsigned int key) = 0;
};

#endif // KEYBOARD_H
