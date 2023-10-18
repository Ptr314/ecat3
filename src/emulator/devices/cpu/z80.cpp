#include "z80.h"

#define CALLBACK_NMI    1
#define CALLBACK_INT    2

//----------------------- Library wrapper -----------------------------------
z80Core::z80Core(z80 * emulator_device):
    z80core()
{
    this->emulator_device = emulator_device;
}

uint8_t z80Core::read_mem(uint16_t address)
{
    return emulator_device->read_mem(address);
}

void z80Core::write_mem(uint16_t address, uint8_t value)
{
    emulator_device->write_mem(address, value);
}

uint8_t z80Core::read_port(uint16_t address)
{
    return emulator_device->read_port(address);
}

void z80Core::write_port(uint16_t address, uint8_t value)
{
    emulator_device->write_port(address, value);
}

//void z80Core::inte_changed(unsigned int inte)
//{
//    emulator_device->inte_changed(inte);
//}

//----------------------- Emulator device -----------------------------------

z80::z80(InterfaceManager *im, EmulatorConfigDevice *cd):
    CPU(im, cd)
{
    i_address = create_interface(16, "address", MODE_R, 1); //TODO: check mode
    i_data =    create_interface(8, "data", MODE_RW);
    i_nmi =     create_interface(1, "nmi", MODE_R, CALLBACK_NMI);
    i_int =     create_interface(1, "int", MODE_R, CALLBACK_INT);
    //i_inte =    create_interface(1, "inte", MODE_W);
    i_m1 =      create_interface(1, "m1", MODE_W);

    core = new z80Core(this);

    over_commands.push_back(0xCD);
    over_commands.push_back(0xDD);
    over_commands.push_back(0xED);
    over_commands.push_back(0xFD);

#ifdef LOG_8080
    logger = new CPULogger(this, CPU_LOGGER_8080, core->get_context(), "8080_my");
#endif

}

z80::~z80()
{
#ifdef LOG_8080
    delete logger;
#endif
}

unsigned int z80::get_pc()
{
    return core->get_context()->registers.regs.PC;
}

unsigned int z80::read_mem(unsigned int address)
{
    return mm->read(address);
}

void z80::write_mem(unsigned int address, unsigned int data)
{
    mm->write(address, data);
}

unsigned int z80::read_port(unsigned int address)
{
    return mm->read_port(address);
}

void z80::write_port(unsigned int address, unsigned int data)
{
    mm->write_port(address, data);
}

void z80::reset(bool cold)
{
    CPU::reset(cold);
}

//void z80::inte_changed(unsigned int inte)
//{
//    i_inte->change(inte);
//}

QList<QString> z80::get_registers()
{
    QList<QString> l;
    z80context * c = core->get_context();

    l << QString("AF=%1").arg((c->registers.regs.A << 8) + c->registers.regs.F, 4, 16, QChar('0')).toUpper()
      << QString("BC=%1").arg(c->registers.reg_pairs.BC, 4, 16, QChar('0')).toUpper()
      << QString("DE=%1").arg(c->registers.reg_pairs.DE, 4, 16, QChar('0')).toUpper()
      << QString("HL=%1").arg(c->registers.reg_pairs.HL, 4, 16, QChar('0')).toUpper()
      << QString("IX=%1").arg(c->registers.reg_pairs.IX, 4, 16, QChar('0')).toUpper()
      << QString("IY=%1").arg(c->registers.reg_pairs.IY, 4, 16, QChar('0')).toUpper()
      << ""
      << QString("AF'=%1").arg(c->registers.regs.AF_, 4, 16, QChar('0')).toUpper()
      << QString("BC'=%1").arg(c->registers.regs.BC_, 4, 16, QChar('0')).toUpper()
      << QString("DE'=%1").arg(c->registers.regs.DE_, 4, 16, QChar('0')).toUpper()
      << QString("HL'=%1").arg(c->registers.regs.HL_, 4, 16, QChar('0')).toUpper()
      << ""
      << QString("SP=%1").arg(c->registers.regs.SP, 4, 16, QChar('0')).toUpper()
      << QString("PC=%1").arg(c->registers.regs.PC, 4, 16, QChar('0')).toUpper()
      << ""
      << QString("I=%1").arg(c->registers.regs.I, 2, 16, QChar('0')).toUpper()
      << QString("R=%1").arg(c->registers.regs.R, 2, 16, QChar('0')).toUpper()
      << QString("IFF1=%1").arg(c->IFF1, 1, 16, QChar('0')).toUpper()
      << QString("IFF2=%1").arg(c->IFF2, 1, 16, QChar('0')).toUpper()
    ;
    return l;
}

QList<QString> z80::get_flags()
{
    QList<QString> l;
    z80context * c = core->get_context();

    l << QString("S=%1").arg( ((c->registers.regs.F & F_SIGN) != 0)?1:0)
      << QString("Z=%1").arg( ((c->registers.regs.F & F_ZERO) != 0)?1:0)
      << QString("5=%1").arg( ((c->registers.regs.F & F_B5) != 0)?1:0)
      << QString("H=%1").arg( ((c->registers.regs.F & F_HALF_CARRY) != 0)?1:0)
      << QString("3=%1").arg( ((c->registers.regs.F & F_B3) != 0)?1:0)
      << QString("PV=%1").arg( ((c->registers.regs.F & F_PARITY) != 0)?1:0)
      << QString("N=%1").arg( ((c->registers.regs.F & F_SUB) != 0)?1:0)
      << QString("C=%1").arg( ((c->registers.regs.F & F_CARRY) != 0)?1:0)
    ;
    return l;
}


unsigned int z80::execute()
{
    if (reset_mode)
    {
        core->reset();
        reset_mode = false;
    }

    //TODO: use HALT imitation
    if (debug == DEBUG_STOPPED)
        return 10;

#ifdef LOG_8080
    uint16_t address = core->get_pc();
    uint8_t log_cmd = core->get_command();
    //z80context * context = core->get_context();
    //uint8_t f1 = context->registers.regs.F & 0x10;
    bool do_log = (address < 0xF800)
                  //&& ((log_cmd == 0x3C) || (log_cmd == 0x3D));
                  && (log_cmd == 0x27);
                  //&& ((log_cmd == 0xC6) || (log_cmd == 0xD6) || (log_cmd == 0xE6) || (log_cmd == 0xF6) || (log_cmd == 0xCE) || (log_cmd == 0xDE) || (log_cmd == 0xEE) || (log_cmd == 0xFE));
    if (do_log) logger->log_state(log_cmd, true);
#endif

    unsigned int cycles = core->execute();

#ifdef LOG_8080
    //uint8_t f2 = context->registers.regs.F & 0x10;
    //bool do_log = (address < 0xF800) && (f1 == 0) && (f2 != 0);
    if (do_log) logger->log_state(log_cmd, false, cycles);
#endif


    switch (debug) {
    case DEBUG_STEP:
        debug = DEBUG_STOPPED;
        break;
    case DEBUG_BRAKES:
        if (check_breakpoint(get_pc())) debug = DEBUG_STOPPED;
        break;
    default:
        break;
    }

    return cycles;
}

void z80::set_context_value(QString name, unsigned int value)
{
    if (name == "PC")
    {
        core->get_context()->registers.regs.PC = value;
    }
}

unsigned int z80::get_command()
{
    return core->get_command();
}

void z80::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    switch (callback_id) {
    case CALLBACK_NMI:
        core->set_nmi(new_value & 1);
        break;
    case CALLBACK_INT:
        core->set_int(new_value & 1);
        break;
    }
}

ComputerDevice * create_z80(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new z80(im, cd);
}
