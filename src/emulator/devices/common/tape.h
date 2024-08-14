#ifndef TAPERECORDER_H
#define TAPERECORDER_H

#include "emulator/core.h"
#include "emulator/devices/common/speaker.h"

class TapeRecorder: public ComputerDevice
{
    //TODO: TapeRecorder: Implement
private:
    Interface * i_input;
    Interface * i_output;
    Interface * i_speaker;

    unsigned int system_clock;
    unsigned int baud_rate;

    Speaker * speaker;

public:
    TapeRecorder(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void load_config(SystemData *sd) override;

    virtual void load_file(QString file_name, QString fmt);
    virtual void play();
    virtual void stop();
    virtual void rewind();
};

ComputerDevice * create_tape_recorder(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // TAPERECORDER_H
