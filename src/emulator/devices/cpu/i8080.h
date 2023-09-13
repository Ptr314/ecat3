#ifndef I8080_H
#define I8080_H

#include "emulator/core.h"

struct I8080Context {
    //TODO: Implement
};

class I8080: public CPU
{
private:
    Interface * i_nmi;
    Interface * i_int;
    Interface * i_inte;
    Interface * i_m1;
protected:
    virtual unsigned int get_pc();
public:
    I8080Context context;

    I8080(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual unsigned int execute();
};

ComputerDevice * create_i8080(InterfaceManager *im, EmulatorConfigDevice *cd);


#endif // I8080_H
