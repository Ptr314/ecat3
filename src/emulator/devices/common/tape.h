#ifndef TAPERECORDER_H
#define TAPERECORDER_H

#include "emulator/core.h"
#include "emulator/devices/common/speaker.h"

#define TAPE_STOPPED 0
#define TAPE_READ    1
#define TAPE_WRITE   0

class TapeRecorder: public ComputerDevice
{

private:
    Interface i_input;
    Interface i_output;
    Interface i_speaker;

    Speaker * speaker;

protected:
    unsigned int system_clock;
    unsigned int baud_rate;
    unsigned int ticks_per_bit;
    unsigned int ticks_counter;
    unsigned int tape_mode;
    QByteArray data;
    unsigned int data_size;
    unsigned int data_position;
    int bit_shift;
    unsigned int total_seconds;
    void set_baud_rate(unsigned int baud);
    void set_data(QByteArray new_data);

public:
    QString files;

    TapeRecorder(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void load_config(SystemData *sd) override;
    virtual void clock(unsigned int counter) override;

    virtual void load_file(QString file_name, QString fmt);
    virtual void play();
    virtual void stop();
    virtual void rewind();
    virtual void mute(bool muted);
    virtual void volume(unsigned int volume);
    virtual int get_position();
    virtual int get_total();
    virtual int get_mode();
};

ComputerDevice * create_tape_recorder(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // TAPERECORDER_H
