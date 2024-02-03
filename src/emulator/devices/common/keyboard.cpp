#include "keyboard.h"
#include "qevent.h"
#include "emulator/utils.h"

Keyboard::Keyboard(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd),
    rus_mode(false)
{
    reset_priority = 100;
}

void Keyboard::key_event(QKeyEvent *event, bool press)
{
    //qDebug() << "Key: nativeScanCode()" << Qt::hex << event->nativeScanCode() << "nativeVirtualKey()" << Qt::hex<< event->nativeVirtualKey() << "key()" << Qt::hex << event->key();

    unsigned int key;
    if (known_key(event->key()))
        key = event->key();
    else if (known_key(event->nativeVirtualKey()))
        key = event->nativeVirtualKey();
    else return;
    if (press)
        key_down(rus_translate(key));
    else
        key_up(rus_translate(key));
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

void Keyboard::set_rus(bool new_rus)
{
    rus_mode = new_rus;
}

unsigned int Keyboard::rus_translate(unsigned int code)
{
    if (rus_mode) {
        for (int i=0; i < RUS_REMAP_SIZE; i++)
            if (RUS_REMAP[i][0] == code) return RUS_REMAP[i][1];
        return code;
    } else
        return code;
}
