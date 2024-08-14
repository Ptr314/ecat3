#include <QException>

#include "emulator/utils.h"
#include "tape.h"

TapeRecorder::TapeRecorder(InterfaceManager *im, EmulatorConfigDevice *cd)
    : ComputerDevice(im, cd)
    , baud_rate(0)
{
    //TODO: TapeRecorder: Implement
    i_input =  create_interface(1, "input", MODE_R);
    i_output = create_interface(1, "output", MODE_W);
    i_speaker = create_interface(1, "output", MODE_W);

    system_clock = (dynamic_cast<CPU*>(im->dm->get_device_by_name("cpu")))->clock;

    EmulatorConfigDevice speaker_config(name + "-speaker", "speaker");
    speaker_config.add_parameter("~input", "", name + ".speaker", "", "");

    // speaker = new Speaker(im, &speaker_config);

}

void TapeRecorder::load_config(SystemData *sd)
{

    ComputerDevice::load_config(sd);

    baud_rate = read_confg_value(cd, "baudrate", false, 1200);

    // speaker->load_config(sd);
}

void TapeRecorder::play()
{

}

void TapeRecorder::stop()
{

}

void TapeRecorder::rewind()
{

}

void TapeRecorder::load_file(QString file_name, QString fmt)
{
    QStringList parts = fmt.split(";");
    QStringList first = parts[0].split(":");

    QString tape_format = first[0];
    baud_rate = parse_numeric_value(first[1]);

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

    //TODO: continue here

}


ComputerDevice * create_tape_recorder(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new TapeRecorder(im, cd);
}
