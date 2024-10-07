#include <QException>

#include "wd1793.h"

WD1793::WD1793(InterfaceManager *im, EmulatorConfigDevice *cd):
    FDC(im, cd),
    drives_count(0),
    command(0),
    delay(0),
    register_delay(0),
    step_dir(1)
{
    i_address = create_interface(2, "address", MODE_R);
    i_data = create_interface(8, "data", MODE_R);
    i_INTRQ = create_interface(1, "intrq", MODE_W);
    i_DRQ = create_interface(1, "drq", MODE_W);
    i_HLD = create_interface(1, "hld", MODE_W);

    memset(&registers, 0, sizeof(registers));
}

void WD1793::load_config(SystemData *sd)
{
    FDC::load_config(sd);
    QString s;
    try {
        s = cd->get_parameter("drives").value;
    } catch (QException &e) {
        QMessageBox::critical(0, WD1793::tr("Error"), WD1793::tr("Incorrect fdd list for '%1'").arg(name));
        throw QException();
    }

    QStringList parts = s.split('|', Qt::SkipEmptyParts);
    drives_count = parts.size();
    for (unsigned int i = 0; i < drives_count; i++)
        drives[i] = dynamic_cast<FDD*>(im->dm->get_device_by_name(parts[i]));
}

void WD1793::SetDRQ()
{
    SetFlag(wd1793_FLAG_DRQ);
    i_DRQ->change(1);
}

bool WD1793::GetDRQ()
{
    return (registers[wd1793_REG_STATUS] & wd1793_FLAG_DRQ) != 0;
}

void WD1793::ClearDRQ()
{
    ClearFlag(wd1793_FLAG_DRQ);
    i_DRQ->change(0);
}

void WD1793::SetFlag(unsigned int flag)
{
    registers[wd1793_REG_STATUS] |= flag;
    if (flag == wd1793_FLAG_HLD) i_HLD->change(1);
}

void WD1793::ClearFlag(unsigned int flag)
{
    registers[wd1793_REG_STATUS] &= ~flag;
    if (flag == wd1793_FLAG_HLD) i_HLD->change(0);
}

void WD1793::SetINTRQ()
{
    i_INTRQ->change(1);
}

void WD1793::ClearINTRQ()
{
    i_INTRQ->change(0);
}

void WD1793::FindSelectedDrive()
{
    for (unsigned int i=0; i < drives_count; i++)
        if (drives[i]->is_selected())
        {
            selected_drive = i;
            return void();
        }
    selected_drive = -1;
}

void WD1793::WriteRegister(unsigned int address, unsigned int  value)
{
    unsigned int a = address & 3;
    if (a==wd1793_REG_COMMAND)
    {
        //qDebug() << "COMMAND" << Qt::hex << (value >> 4);
        //Command register
        registers[4] = value;
        ClearINTRQ();
        FindSelectedDrive();
        unsigned int c = (value & 0xF0) >> 4;
        //Execute commands
        switch (c) {
        case 0x00: //Restore
            delay = wd1793_DELAY_RESTORE;
            command = wd1793_COMMAND_RESTORE;
            SetFlag(wd1793_FLAG_BUSY);
            break;

        case 0x01: //Seek
            delay = wd1793_DELAY_STEP * abs((int)registers[wd1793_REG_TRACK] - (int)registers[wd1793_REG_DATA]);
            command = wd1793_COMMAND_SEEK;
            SetFlag(wd1793_FLAG_BUSY);
            break;

        case 0x02:
        case 0x03: //Step
            delay = wd1793_DELAY_STEP;
            command = wd1793_COMMAND_STEP;
            SetFlag(wd1793_FLAG_BUSY);
            break;

        case 0x04:
        case 0x05: //Step-In
            step_dir = 1;
            delay = wd1793_DELAY_STEP;
            command = wd1793_COMMAND_STEP;
            SetFlag(wd1793_FLAG_BUSY);
            break;

        case 0x06:
        case 0x07: //Step-Out
            step_dir = -1;
            delay = wd1793_DELAY_STEP;
            command = wd1793_COMMAND_STEP;
            SetFlag(wd1793_FLAG_BUSY);
            break;

        case 0x08:
        case 0x09: //Read sector
            if ((value & wd1793_PARAM_m) > 0)
            {
                im->dm->error(this, WD1793::tr("Reading of more than one sector at once is not supported!"));
            } else {
                delay = wd1793_DELAY_SECTOR;
                command = wd1793_COMMAND_READ_SECTOR;
                SetFlag(wd1793_FLAG_BUSY);
            }
            break;

        case 0x0A:
        case 0x0B: //Write sector
            if ((value & wd1793_PARAM_m) > 0)
            {
                im->dm->error(this, WD1793::tr("Writing of more than one sector at once is not supported!"));
            } else {
                delay = wd1793_DELAY_SECTOR;
                command = wd1793_COMMAND_WRITE_SECTOR;
                SetFlag(wd1793_FLAG_BUSY);
            }
            break;

        case 0x0C: //Read address
            SetFlag(wd1793_FLAG_NOT_READY);
            SetINTRQ();
            break;

        case 0x0E: //Read track
            SetFlag(wd1793_FLAG_NOT_READY);
            SetINTRQ();
            break;

        case 0x0F: //Write track
            SetFlag(wd1793_FLAG_NOT_READY);
            SetINTRQ();
            break;

        case 0x0D: //Force interrupt
            ClearFlag(wd1793_FLAG_BUSY);
            command = 0;
            delay = 0;
            ClearDRQ();
            if ((value & 0x0F) !=0)
                im->dm->error(this, WD1793::tr("Force Interrupt command with parameters is not supported!"));
            break;
        }
    } else {
//        switch (a) {
//        case 1:
//            qDebug() << "TRACK=" << Qt::hex << value;
//            break;
//        case 2:
//            qDebug() << "SECT=" << Qt::hex << value;
//            break;
//        default:
//            qDebug() << "DATA=" << Qt::hex << value;
//            break;
//        }
        registers[a] = value;
    }
    if (a==wd1793_REG_DATA) ClearDRQ();
}

bool WD1793::get_busy()
{
    return (registers[wd1793_REG_STATUS] & wd1793_FLAG_BUSY) != 0;
}

unsigned int WD1793::get_selected_drive()
{
    return selected_drive;
}

unsigned int WD1793::get_value(unsigned int address)
{
    unsigned int a = address & 0x03;
    if (a==wd1793_REG_DATA) ClearDRQ();
    if (a==wd1793_REG_STATUS) ClearINTRQ();
//    switch (a) {
//    case 0:
//        qDebug() << "GET STATUS" << Qt::hex << registers[a];
//        break;
//    case 1:
//        qDebug() << "GET TRACK" << Qt::hex << registers[a];
//        break;
//    case 2:
//        qDebug() << "GET SECT" << Qt::hex << registers[a];
//        break;
//    default:
//        qDebug() << "GET DATA" << Qt::hex << registers[a];
//        break;
//    }
    return registers[a];
}

void WD1793::set_value(unsigned int address, unsigned int value, bool force)
{
    if (wd1793_DELAY_REGISTER > 0){
        register_delay = wd1793_DELAY_REGISTER;
        register_to_write = address;
        value_to_write = value;
    } else
        WriteRegister(address, value);
}

void WD1793::clock(unsigned int counter)
{
    auto Set_I_Flags = [this](uint8_t T, uint8_t S)
    {
        int seek_res;
        ClearFlag(wd1793_FLAG_BUSY);
        if ((registers[4] & wd1793_PARAM_h)  != 0)
            SetFlag(wd1793_FLAG_HLD);
        else
            ClearFlag(wd1793_FLAG_HLD);

        if (registers[wd1793_REG_TRACK]==0)
            SetFlag(wd1793_FLAG_TR00);
        else
            ClearFlag(wd1793_FLAG_TR00);

        if (selected_drive >= 0)
        {
            seek_res = drives[selected_drive]->SeekSector(T, S);
            if (seek_res < 0)
                SetFlag(wd1793_FLAG_NOT_READY);
            else
                ClearFlag(wd1793_FLAG_NOT_READY);

            if (drives[selected_drive]->is_protected())
                SetFlag(wd1793_FLAG_PROTECTED);
            else
                ClearFlag(wd1793_FLAG_PROTECTED);
        } else {
            SetFlag(wd1793_FLAG_NOT_READY);
            SetFlag(wd1793_FLAG_PROTECTED);
        };
    };

    auto Set_II_Flags = [this]()
    {
        ClearFlag(wd1793_FLAG_BUSY);
        ClearFlag(wd1793_FLAG_LOST_DATA);
        ClearFlag(wd1793_FLAG_BAD_CRC);
    };

    //Delay before writing to a register
    if (register_delay > 0)
    {
        register_delay -= counter;
        if (register_delay <= 0)
            WriteRegister(register_to_write, value_to_write);
    }

    //Other delays
    if (delay > 0)
    {
        delay -= counter;
    } else {
        //Executing a command
        switch (command) {
        case wd1793_COMMAND_RESTORE:
            registers[wd1793_REG_TRACK] = 0;
            Set_I_Flags(0, 1);
            SetINTRQ();
            command = 0;
            break;

        case wd1793_COMMAND_SEEK:
            registers[wd1793_REG_TRACK] = registers[wd1793_REG_DATA];
            Set_I_Flags(registers[wd1793_REG_TRACK], 1);
            SetINTRQ();
            command = 0;
            break;

        case wd1793_COMMAND_STEP:
            if ((registers[4] & wd1793_PARAM_T) > 0)
                registers[wd1793_REG_TRACK] += step_dir;
            Set_I_Flags(registers[wd1793_REG_TRACK], 1);
            SetINTRQ();
            command = 0;
            break;

        case wd1793_COMMAND_READ_SECTOR:
            if (selected_drive >=0)
            {
                sector_size = drives[selected_drive]->SeekSector(registers[wd1793_REG_TRACK], registers[wd1793_REG_SECTOR]);
                if (sector_size > 0)
                {
                    delay = wd1793_DELAY_NEXT_BYTE;
                    command = wd1793_COMMAND_READ_BYTE;
                    bytes = 0;
                } else {
                    Set_II_Flags();
                    SetFlag(wd1793_FLAG_NOT_READY);
                    SetINTRQ();
                    command = 0;
                }
            } else {
                SetFlag(wd1793_FLAG_NOT_READY);
            };
            break;
        case wd1793_COMMAND_WRITE_SECTOR:
            if (selected_drive >=0)
            {
                sector_size = drives[selected_drive]->SeekSector(registers[wd1793_REG_TRACK], registers[wd1793_REG_SECTOR]);
                if ((sector_size > 0) && !drives[selected_drive]->is_protected())
                {
                    delay = wd1793_DELAY_NEXT_BYTE;
                    command = wd1793_COMMAND_WRITE_BYTE;
                    bytes = 0;
                    SetDRQ();
                } else {
                    Set_II_Flags();
                    if (sector_size < 0)
                        SetFlag(wd1793_FLAG_NOT_READY);
                    else
                        SetFlag(wd1793_FLAG_PROTECTED);
                    SetINTRQ();
                    command = 0;
                }
            } else {
                SetFlag(wd1793_FLAG_NOT_READY);
            };
            break;
        case wd1793_COMMAND_READ_BYTE:
            if (!GetDRQ()) {
                if (bytes < sector_size)
                {
                    //Reading bytes
                    registers[wd1793_REG_DATA] = drives[selected_drive]->ReadNextByte();
                    bytes++;
                    SetDRQ();
                } else {
                    //Reached sector's end
                    Set_II_Flags();
                    ClearFlag(wd1793_FLAG_PROTECTED);
                    ClearFlag(wd1793_FLAG_DATA_TYPE);
                    SetINTRQ();
                    command = 0;
                };
            }
            break;
        case wd1793_COMMAND_WRITE_BYTE:
            if (bytes < sector_size)
            {
                if (!GetDRQ())
                {
                    //Writing bytes
                    drives[selected_drive]->WriteNextByte(registers[wd1793_REG_DATA]);
                    bytes++;
                    SetDRQ();
                }
            } else {
                //Reached sector's end
                Set_II_Flags();
                ClearFlag(wd1793_FLAG_ERR_WRITE);
                SetINTRQ();
                command = 0;
            };
            break;
        }
    }
}

ComputerDevice * create_WD1793(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new WD1793(im, cd);
}
