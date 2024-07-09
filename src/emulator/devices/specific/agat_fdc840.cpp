#include <QException>

#include "agat_fdc840.h"

Agat_FDC840::Agat_FDC840(InterfaceManager *im, EmulatorConfigDevice *cd):
    FDC(im, cd),
    motor_on(false),
    write_mode(false)
{
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

unsigned int Agat_FDC840::get_value(unsigned int address)
{
    unsigned int A = address & 0x0f;
    switch (A) {
        case 0x0:
        case 0x2:
        case 0x4:
        case 0x6:
        case 0x1:
        case 0x3:
        case 0x5:
        case 0x7:
        case 0x8:
        case 0x9:
        case 0xA:
        case 0xB:
        case 0xC:
        case 0xD:
        case 0xE:
            break;
        default: // 0x0F
            break;
    }
    return 0;
}

void Agat_FDC840::set_value(unsigned int address, unsigned int value, bool force)
{
    unsigned int A = address & 0x0f;
    switch (A) {
    case 0x0:
    case 0x2:
    case 0x4:
    case 0x6:
    case 0x1:
    case 0x3:
    case 0x5:
    case 0x7:
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC:
    case 0xD:
    case 0xE:
        break;
    default: // 0x0F
        break;
    }
}

ComputerDevice * create_agat_fdc840(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Agat_FDC840(im, cd);
}
