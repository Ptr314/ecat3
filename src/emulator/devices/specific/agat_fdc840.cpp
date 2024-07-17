#include <QException>

#include "agat_fdc840.h"
#include "emulator/utils.h"
#include "libs/mfm_tools.h"

Agat_FDC840::Agat_FDC840(InterfaceManager *im, EmulatorConfigDevice *cd):
    FDC(im, cd),
    dd14(im, cd),
    dd15(im, cd),
    motor_on(false),
    write_mode(false),
    sector_sync(false),
    side(0),
    data_ready(false)
{
    i_select = create_interface(2, "select", MODE_W);
    i_side = create_interface(1, "side", MODE_W);
    selected_drive = -1;
    memset(&current_track, 0, sizeof(current_track));
}

void Agat_FDC840::load_config(SystemData *sd)
{
    FDC::load_config(sd);

    clock_divider = 32;

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

    LinkData ld;
    ld.s.i = i_side;
    ld.s.shift = 0;
    ld.s.mask = create_mask(1, 0);

    for (unsigned int i = 0; i < drives_count; i++) {
        drives[i] = dynamic_cast<FDD*>(im->dm->get_device_by_name(parts[i]));

        ld.d.i = im->get_interface_by_name(parts[i], "side");
        ld.d.shift = 0;
        ld.d.mask = create_mask(1, 0);
        ld.s.i->connect(ld.s, ld.d, false);
    }

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
                         + ((current_track[selected_drive]==0?0:1)       << 6)    // Track 00
                         + ((motor_on?0:1)                               << 7);   // Ready // TODO: check

    dd14.set_value(1, status_fdd, true);

    uint8_t status_fdc =   ((sector_sync?0:1) << 6)                               // sector sync detected (active - 0)
                         + ((data_ready?1:0) << 7);                               // data is ready to be read (active - 1)

    dd15.set_value(2, status_fdc, true);
}

void Agat_FDC840::update_state()
{
    // Check control signals after setting them
    uint8_t state = dd14.get_value(2);

    step_dir       = (state >> 2) & 0x1;
    int new_selected = (state >> 3) & 0x1;
    int new_side = (state >> 4) & 0x1;
    write_mode     = ((state >> 6) & 0x1) == 1;
    motor_on       = ((state >> 7) & 0x1) == 1;

    if ((side != new_side) || (selected_drive != new_selected)) {
        selected_drive = new_selected;
        side = new_side;
        i_select->change(1 << selected_drive);
        i_side->change(side);
        drives[selected_drive]->SeekSector(current_track[selected_drive], 0);
    }


    update_status();
}

uint8_t Agat_FDC840::read_next_byte()
{
    int sector_pos = drives[selected_drive]->get_position() % 282;
    uint8_t data = drives[selected_drive]->ReadNextByte();
    // Sync at a sector prologue (before 0x95) or header (before 0x6A)
    if ( sector_pos == 12 || sector_pos == 21) {
        sector_sync = true;
    }
    dd15.set_value(0, data, true);
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
        case 0x0:
            value = 0x10;
            break;
        case 0x1:
        case 0x2:
            update_status();
            value = dd14.get_value(A & 0x03);
            break;
        case 0x4:
            value = dd15.get_value(A & 0x03);
            data_ready = false;
            update_status();
            break;
        case 0x6:
            value = dd15.get_value(A & 0x03);
            break;
        case 0x7:
            value = dd15.get_value(A & 0x03);
            break;
        default:
            value = 0xFF;
            break;
    }
#ifdef LOG_FDD
    // if (A != 6 || value != 0xC0)
    //     logs(QString("R %1:%2 p:%3").arg(A, 1, 16, QChar('0')).arg(value, 2, 16, QChar('0')).arg(drives[selected_drive]->get_position()));
#endif
    return value;
}

void Agat_FDC840::set_value(unsigned int address, unsigned int value, bool force)
{
    unsigned int A = address & 0x0f;
#ifdef LOG_FDD
    //logs(QString("W %1:%2 p:%3").arg(A, 1, 16, QChar('0')).arg(value, 2, 16, QChar('0')).arg(drives[selected_drive]->get_position()));
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
            // SYNC WRITE
            // TODO: writing
            break;
        case 0x9:
            // STEP
            if (step_dir == 1) {
                if (current_track[selected_drive] < AGAT_840_TRACK_COUNT-1) current_track[selected_drive]++;
            } else {
                if (current_track[selected_drive] > 0) current_track[selected_drive]--;
            }
#ifdef LOG_FDD
            logs(QString("STEP to %1").arg(current_track[selected_drive]));
#endif
            drives[selected_drive]->SeekSector(current_track[selected_drive], 0);
            break;
        case 0xA:
            // SYNC RESET
            sector_sync = false;
            update_status();
#ifdef LOG_FDD
            //logs(QString("SYNC OFF"));
#endif
            break;
        default:
            break;
    }
}

void Agat_FDC840::clock(unsigned int counter)
{
    if (motor_on) read_next_byte();
}

ComputerDevice * create_agat_fdc840(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Agat_FDC840(im, cd);
}
