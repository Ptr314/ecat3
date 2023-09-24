#include "keyboard.h"
#include "qevent.h"
#include "emulator/utils.h"

Keyboard::Keyboard(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)
{}

void Keyboard::key_event(QKeyEvent *event, bool press)
{
    qDebug() << "Key pressed: scan " << event->nativeScanCode() << "virtual" << event->nativeVirtualKey() << "key" << Qt::hex << event->key();

    unsigned int key;
    if (known_key(event->key()))
        key = event->key();
    else if (known_key(event->nativeVirtualKey()))
        key = event->nativeVirtualKey();
    else return;
    if (press)
        key_down(key);
    else
        key_up(key);
}

bool Keyboard::known_key(unsigned int code)
{
    for (unsigned int i=0; i<sizeof(KEYS)/sizeof(KeyDescription); i++)
        if (KEYS[i].code == code)
            return true;

    return false;
}

unsigned int Keyboard::translate_key(QString key)
{
    for (unsigned int i=0; i<sizeof(KEYS)/sizeof(KeyDescription); i++)
        if (KEYS[i].name.toLower() == key.toLower())
            return KEYS[i].code;

    return _FFFF;
}
