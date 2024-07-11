#include <QException>

#include "agat_fdc840.h"
#include "emulator/utils.h"

Agat_FDC840::Agat_FDC840(InterfaceManager *im, EmulatorConfigDevice *cd):
    FDC(im, cd),
    dd14(im, cd),
    dd15(im, cd),
    motor_on(false),
    write_mode(false),
    sector_sync(false)
{
    i_select = create_interface(2, "select", MODE_W);
    selected_drive = -1;
    memset(&current_track, 0, sizeof(current_track));
}

void Agat_FDC840::load_config(SystemData *sd)
{
    FDC::load_config(sd);
    QString s;
    try {
        s = cd->get_parameter("drives").value;
    } catch (QException &e) {
        QMessageBox::critical(0, Agat_FDC840::tr("Error"), Agat_FDC840::tr("Incorrect fdd list for '%1'").arg(name));
        throw QException();
    }

    memset(&drives, 0, sizeof(drives));
    QStringList parts = s.split('|', Qt::SkipEmptyParts);
    drives_count = parts.size();
    for (unsigned int i = 0; i < drives_count; i++)
        drives[i] = dynamic_cast<FDD*>(im->dm->get_device_by_name(parts[i]));

    selected_drive = 0;
}


bool Agat_FDC840::get_busy()
{
    return motor_on;
}

unsigned int Agat_FDC840::get_selected_drive()
{
    return selected_drive;
}

void Agat_FDC840::reset(bool cold)
{
    FDC::reset(cold);

    dd14.reset(cold);
    dd15.reset(cold);
}

void Agat_FDC840::update_status()
{
    // Collect status signals and put them to the register for reading

    uint8_t status_fdd =   (0b00                                         << 0)    //FDD2 type
                         + (0b00                                         << 2)    //FDD1 type
                         + ((drives[selected_drive]->is_index()?0:1)     << 4)    // Index hole
                         + ((drives[selected_drive]->is_protected()?0:1) << 5)    // Write protection
                         + ((drives[selected_drive]->is_track_00()?0:1)  << 6)    // Track 00
                         + ((motor_on?0:1)                               << 7);   // Ready // TODO: check

    dd14.set_value(1, status_fdd, true);

    uint8_t status_fdc =   ((sector_sync?0:1) << 6)                               // sector sync detected (active - 0)
                         + ((data_ready?1:0) << 7);                               // data is ready to be read (active - 1)

    dd15.set_value(1, status_fdc, true);
}

void Agat_FDC840::update_state()
{
    // Check control signals after setting them
    uint8_t state = dd14.get_value(2);

    step_dir       = (state >> 2) & 0x1;
    selected_drive = (state >> 3) & 0x1;
    head           = (state >> 4) & 0x1;
    write_mode     = ((state >> 6) & 0x1) == 1;
    motor_on       = ((state >> 7) & 0x1) == 1;

    i_select->change(1 << selected_drive);

    update_status();
}

uint8_t Agat_FDC840::read_next_byte()
{
    uint8_t data = drives[selected_drive]->ReadNextByte();
    if (drives[selected_drive]->get_position() % 282 == 12) {
#ifdef LOG_FDD
        logs(QString("SYNC ON"));
#endif
        sector_sync = true;
    }
    dd15.set_value(2, data, true);
    data_ready = true;
    update_status();
    return data;
}


unsigned int Agat_FDC840::get_value(unsigned int address)
{
    //TODO: check if reading 8255(3) works
    unsigned int A = address & 0x0f;
    uint8_t value;
    switch (A) {
        case 0x1:
        case 0x2:
            update_status();
            value = dd14.get_value(A & 0x03);
            break;
        case 0x4:
        case 0x6:
            read_next_byte();
            value = dd15.get_value(A & 0x03);
            break;
        default:
            value = 0xFF;
            break;
    }
#ifdef LOG_FDD
        logs(QString("R %1:%2").arg(A, 1, 16, QChar('0')).arg(value, 2, 16, QChar('0')));
#endif
    return value;
}

void Agat_FDC840::set_value(unsigned int address, unsigned int value, bool force)
{
    unsigned int A = address & 0x0f;
#ifdef LOG_FDD
    logs(QString("W %1:%2").arg(A, 1, 16, QChar('0')).arg(value, 2, 16, QChar('0')));
#endif
    switch (A) {
        case 0x2:
        case 0x3:
            dd14.set_value(A & 0x03, value);
            update_state();
            break;
        case 0x5:
        case 0x7:
            dd15.set_value(A & 0x03, value);
            break;
        case 0x8:
            // SYNC WRTITE
            break;
        case 0x9:
            // STEP
            break;
        case 0xA:
            // SYNC RESET
            sector_sync = false;
#ifdef LOG_FDD
            logs(QString("SYNC OFF"));
#endif
            break;
        default:
            break;
    }
}

ComputerDevice * create_agat_fdc840(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Agat_FDC840(im, cd);
}
