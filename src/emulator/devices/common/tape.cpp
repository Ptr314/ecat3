#include <QException>

#include "emulator/utils.h"
#include "emulator/devices/cpu/cpu_utils.h"
#include "tape.h"

#define TAPE_STOPPED 0
#define TAPE_READ    1
#define TAPE_WRITE   0

TapeRecorder::TapeRecorder(InterfaceManager *im, EmulatorConfigDevice *cd)
    : ComputerDevice(im, cd)
    , baud_rate(0)
    , tape_mode(TAPE_STOPPED)
    , data_position(0)
    , bit_shift(7)
    , ticks_counter(0)
{
    //TODO: TapeRecorder: Implement
    i_input =  create_interface(1, "input", MODE_R);
    i_output = create_interface(1, "output", MODE_W);
    i_speaker = create_interface(1, "speaker", MODE_W);

    system_clock = (dynamic_cast<CPU*>(im->dm->get_device_by_name("cpu")))->clock;

    EmulatorConfigDevice * speaker_config = new EmulatorConfigDevice(name + "-speaker", "speaker");
    speaker_config->add_parameter("~input", "", name + ".speaker", "", "");

    speaker = new Speaker(im, speaker_config);

}

void TapeRecorder::load_config(SystemData *sd)
{

    ComputerDevice::load_config(sd);

    baud_rate = read_confg_value(cd, "baudrate", false, 1200);

    files = cd->get_parameter("files", false).value;

    if (files.isEmpty()) files = sd->allowed_files;

    speaker->load_config(sd);
    speaker->reset(true);
}

void TapeRecorder::play()
{
    tape_mode = TAPE_READ;
}

void TapeRecorder::stop()
{
    tape_mode = TAPE_STOPPED;
}

void TapeRecorder::rewind()
{
    data_position = 0;
    bit_shift = 7;
    ticks_counter = 0;
}

void TapeRecorder::set_baud_rate(unsigned int baud)
{
    baud_rate = baud;
    ticks_per_bit = system_clock / baud_rate;
}

void TapeRecorder::set_data(QByteArray new_data){
    data = new_data;
    tape_mode = TAPE_STOPPED;
    data_size = data.length();
    data_position = 0;
    bit_shift = 7;
    total_seconds = (data_size * 8) / baud_rate;
    ticks_counter = 0;
}

void TapeRecorder::load_file(QString file_name, QString fmt)
{
    QStringList parts = fmt.split(";");
    QStringList first = parts[0].split(":");

    QString tape_format = first[0];
    int baud = parse_numeric_value(first[1]);

    QByteArray buffer;

    for (int i=1; i< parts.length(); i++ ) {
        if (parts[i] == "data") {
            QFile file(file_name);
            if (file.open(QIODevice::ReadOnly)){
                QByteArray data = file.readAll();
                file.close();
                buffer.append(data);
            }
        } else {
            QStringList bytes = parts[i].split(":");
            uint8_t b = parse_numeric_value(bytes[0]);
            unsigned int count = parse_numeric_value(bytes[1]);
            buffer.append(count, b);
        }
    }

    unsigned int data_size = buffer.size();

    QByteArray buffer_encoded;
    buffer_encoded.reserve(data_size*2);

    if (tape_format == "rk86") {
        set_baud_rate(baud*2);
        for (int i=0; i < data_size; i++) {
            PartsRecLE T;
            T.w = 0;
            uint8_t b = buffer.at(i);
            for (int j=0; j<8; j++)
                T.w += (b & (1 << j)) << j;
            T.w |= ~(T.w << 1) & 0xAAAA;
            buffer_encoded.append(T.b.H);
            buffer_encoded.append(T.b.L);
        }
        set_data(buffer_encoded);
    } else {
        QMessageBox::warning(0, TapeRecorder::tr("Error"), TapeRecorder::tr("Unknown tape format!"));
    }

}

void TapeRecorder::clock(unsigned int counter)
{
    if (tape_mode != TAPE_STOPPED) {
        if (ticks_counter < ticks_per_bit) {
            ticks_counter += counter;
        } else {
            ticks_counter -= ticks_per_bit;
            if (data_position < data_size) {
                if (tape_mode == TAPE_READ) {
                    unsigned int v = (data.at(data_position) >> bit_shift) & 1;
                    i_output->change(v);
                    i_speaker->change(v);
                } else {
                    //TODO: write
                }
                if (--bit_shift < 0) {
                    bit_shift = 7;
                    data_position++;
                }
            } else {
                stop();
            }
        }
    }
    speaker->clock(counter);
}

ComputerDevice * create_tape_recorder(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new TapeRecorder(im, cd);
}
