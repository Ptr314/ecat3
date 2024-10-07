#ifndef WD1793_H
#define WD1793_H

#include "emulator/core.h"
#include "emulator/devices/common/fdd.h"

#define wd1793_REG_STATUS       0x00
#define wd1793_REG_COMMAND      0x00
#define wd1793_REG_TRACK        0x01
#define wd1793_REG_SECTOR       0x02
#define wd1793_REG_DATA         0x03

#define wd1793_FLAG_BUSY        0x01
#define wd1793_FLAG_DRQ         0x02
#define wd1793_FLAG_INDEX       0x02
#define wd1793_FLAG_TR00        0x04
#define wd1793_FLAG_LOST_DATA   0x04
#define wd1793_FLAG_BAD_CRC     0x08
#define wd1793_FLAG_ERR_SEEK    0x10
#define wd1793_FLAG_HLD         0x20
#define wd1793_FLAG_DATA_TYPE   0x20
#define wd1793_FLAG_ERR_WRITE   0x20
#define wd1793_FLAG_PROTECTED   0x40
#define wd1793_FLAG_NOT_READY   0x80

#define wd1793_PARAM_T          0x10
#define wd1793_PARAM_m          0x10
#define wd1793_PARAM_h          0x08
#define wd1793_PARAM_V          0x04

//задержка записи значения в регистр
//Если 0 - эмулятор не проходит некоторые тесты
//Если слишком большое - не работают программы
//Все значения задержек - в тактах контроллера

#define wd1793_DELAY_REGISTER       10

#define wd1793_DELAY_RESTORE        100
#define wd1793_COMMAND_RESTORE      1

#define wd1793_DELAY_SECTOR         100
#define wd1793_COMMAND_READ_SECTOR  2
#define wd1793_COMMAND_WRITE_SECTOR 3

#define wd1793_DELAY_NEXT_BYTE      10
#define wd1793_COMMAND_READ_BYTE    4
#define wd1793_COMMAND_WRITE_BYTE   5

#define wd1793_DELAY_STEP           100
#define wd1793_COMMAND_SEEK         6
#define wd1793_COMMAND_STEP         7

class WD1793 : public FDC
{
private:
    Interface * i_address;
    Interface * i_data;
    Interface * i_INTRQ;
    Interface * i_DRQ;
    Interface * i_HLD;
    FDD * drives[4];
    unsigned int drives_count;
    unsigned int selected_drive;
    unsigned int command;
    int delay;
    int register_delay;
    unsigned int register_to_write;
    unsigned int value_to_write;
    int sector_size;
    int bytes;
    int step_dir;

    void SetDRQ();
    bool GetDRQ();
    void ClearDRQ();
    void SetFlag(unsigned int flag);
    void ClearFlag(unsigned int flag);
    void SetINTRQ();
    void ClearINTRQ();
    void FindSelectedDrive();
    void WriteRegister(unsigned int address, unsigned int  value);

public:
    uint8_t registers[5];
    WD1793(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual bool get_busy() override;
    virtual unsigned int get_selected_drive() override;
    virtual unsigned int get_value(unsigned int address) override;
    virtual void set_value(unsigned int address, unsigned int value, bool force=false) override;
    virtual void load_config(SystemData *sd) override;
    virtual void clock(unsigned int counter) override;
};

ComputerDevice * create_WD1793(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // WD1793_H
