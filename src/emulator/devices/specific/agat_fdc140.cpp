#include <QException>

#include "agat_fdc140.h"

// https://github.com/allender/apple2emu/blob/df9eff703dd70b7dbc3b817734daf95133437143/src/disk_image.cpp#L263

#define AGAT_FDC_READ   0;
#define AGAT_FDC_WRITE  1;

Agat_FDC140::Agat_FDC140(InterfaceManager *im, EmulatorConfigDevice *cd):
    FDC(im, cd),
    prev_phase(-1),
    current_phase(-1),
    motor_on(false),
    write_mode(false)
{
    i_select = create_interface(2, "select", MODE_W);
    i_motor_on = create_interface(1, "motor_on", MODE_W);

    selected_drive = -1;

    memset(&current_track, 0, sizeof(current_track));
}

void Agat_FDC140::load_config(SystemData *sd)
{
    FDC::load_config(sd);
    QString s;
    try {
        s = cd->get_parameter("drives").value;
    } catch (QException &e) {
        QMessageBox::critical(0, Agat_FDC140::tr("Error"), Agat_FDC140::tr("Incorrect fdd list for '%1'").arg(name));
        throw QException();
    }

    memset(&drives, 0, sizeof(drives));
    QStringList parts = s.split('|', Qt::SkipEmptyParts);
    drives_count = parts.size();
    for (unsigned int i = 0; i < drives_count; i++)
        drives[i] = dynamic_cast<FDD*>(im->dm->get_device_by_name(parts[i]));

    selected_drive = 0;
}


bool Agat_FDC140::get_busy()
{
    return motor_on;
}

unsigned int Agat_FDC140::get_selected_drive()
{
    // TODO: how is it selected?
    return selected_drive;
}

void Agat_FDC140::phase_on(int n)
{
#ifdef LOG_FDD
    //logs(QString("P%1+").arg(n));
#endif
    if (prev_phase >= 0) {
        if ( ((prev_phase-1) & 0x03) == n ) {
            //Step down
#ifdef LOG_FDD
            //logs(QString(" DOWN"));
#endif
            if (current_track[selected_drive] > 0) {
                current_track[selected_drive]--;
                drives[selected_drive]->SeekSector(current_track[selected_drive] / 2, 0);
#ifdef LOG_FDD
                //logs("["+QString::number(current_track[selected_drive]) + "]");
#endif
            }
        } else
        if ( ((prev_phase+1) & 0x03) == n ) {
            //Step up
#ifdef LOG_FDD
            //logs(QString(" UP"));
#endif
            if (current_track[selected_drive] < 68) {
                current_track[selected_drive]++;
                drives[selected_drive]->SeekSector(current_track[selected_drive] / 2, 0);
#ifdef LOG_FDD
                //logs("["+QString::number(current_track[selected_drive]) + "]");
#endif
            }
        }
    }
    prev_phase = n;
}

void Agat_FDC140::phase_off(int n)
{
#ifdef LOG_FDD
    //logs(QString("P%1-").arg(n));
#endif
    //prev_phase = current_phase;
    //current_phase = n;
}

void Agat_FDC140::select_drive(int n)
{
#ifdef LOG_FDD
    logs(QString(" SEL %1").arg(n));
#endif
    // TODO: check selection on a drive
    selected_drive = n;
    i_select->change(n);
    current_phase = -1;
}

unsigned int Agat_FDC140::get_value(unsigned int address)
{
    unsigned int A = address & 0x0f;
    switch (A) {
        case 0x0:
        case 0x2:
        case 0x4:
        case 0x6:
            phase_off(A >> 1);
            break;
        case 0x1:
        case 0x3:
        case 0x5:
        case 0x7:
            phase_on(A >> 1);
            break;
        case 0x8:
            motor_on = false;
            i_motor_on->change(0);
            break;
        case 0x9:
            motor_on = true;
            i_motor_on->change(1);
            break;
        case 0xA:
        case 0xB:
            select_drive(A & 0x01);
            break;
        case 0xC:
            // TODO: timings imitation
            if (write_mode) {
                // Writing
                drives[selected_drive]->WriteNextByte(write_register);
            } else {
                // Reading
                if (drives[selected_drive] != nullptr)
                    return drives[selected_drive]->ReadNextByte();
                else
                    return 0xFF;
            }
            break;
        case 0xD:
            break;
        case 0xE:
            write_mode = false;
            if (drives[selected_drive] != nullptr)
                return drives[selected_drive]->is_protected()?0x80:0;
            break;
        default: // 0x0F
            write_mode = true;
            break;
    }
    return 0;
}

void Agat_FDC140::set_value(unsigned int address, unsigned int value, bool force)
{
    unsigned int A = address & 0x0f;
    switch (A) {
        case 0x0:
        case 0x2:
        case 0x4:
        case 0x6:
            phase_off(A >> 1);
            break;
        case 0x1:
        case 0x3:
        case 0x5:
        case 0x7:
            phase_on(A >> 1);
            break;
        case 0x8:
            break;
        case 0x9:
            break;
        case 0xA:
        case 0xB:
            select_drive(A & 0x01);
            break;
        case 0xC:
            if (write_mode)
                drives[selected_drive]->WriteNextByte(write_register);
            break;
        case 0xD:
            write_register = value;
            break;
        case 0xE:
            write_mode = false;
            break;
        default: // 0x0F
            write_mode = true;
            write_register = value;
            break;
    }
}

ComputerDevice * create_agat_fdc140(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Agat_FDC140(im, cd);
}
