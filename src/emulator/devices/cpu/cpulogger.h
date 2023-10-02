#ifndef CPULOGGER_H
#define CPULOGGER_H

#include <QObject>
#include <QDateTime>
#include <QDir>

#include "emulator/core.h"
#include "emulator/emulator.h"
#include "emulator/devices/cpu/i8080_context.h"

#define CPU_LOGGER_8080     1

class CPULogger:public QObject
{
    Q_OBJECT
private:
    CPU * cpu;
    void * context;
    int cpu_type;
    unsigned int logged_count;
    QString log;
    QString log_name;

public:
    CPULogger(CPU * cpu, int cpu_type, void * context, QString log_name):
        cpu(cpu),
        cpu_type(cpu_type),
        context(context),
        logged_count(0),
        log(""),
        log_name(log_name)
    {}

    void log_state(uint8_t command, bool before, unsigned int cycles=0)
    {
        unsigned int pc = cpu->get_pc();
        QString cs;
        if (cpu_type == CPU_LOGGER_8080) {
            i8080context * c = static_cast<i8080context*>(context);
            cs =   QString("%1").arg(pc, 4, 16, QChar('0')) \
                 + QString(" %1").arg(command, 2, 16, QChar('0')) + ((before)?"+":"-")
                 + ((!before)?(QString(" %1").arg(cycles, 2, 10, QChar('0'))):"   ")
                 + QString(" AF:%1%2").arg(c->registers.regs.A, 2, 16, QChar('0')).arg(c->registers.regs.F, 2, 16, QChar('0'))
                 //+ QString(" B, C:%1,%2").arg(c->registers.regs.B, 2, 16, QChar('0')).arg(c->registers.regs.C, 2, 16, QChar('0'))
                 + QString(" BC:%1").arg(c->registers.reg_pairs.BC, 4, 16, QChar('0'))
                 //+ QString(" D, E:%1,%2").arg(c->registers.regs.D, 2, 16, QChar('0')).arg(c->registers.regs.E, 2, 16, QChar('0'))
                 + QString(" DE:%1").arg(c->registers.reg_pairs.DE, 4, 16, QChar('0'))
                 //+ QString(" H, L:%1,%2").arg(c->registers.regs.H, 2, 16, QChar('0')).arg(c->registers.regs.L, 2, 16, QChar('0'))
                 + QString(" HL:%1").arg(c->registers.reg_pairs.HL, 4, 16, QChar('0'))
                 + QString(" SP:%1").arg(c->registers.regs.SP, 4, 16, QChar('0'))
                 + "\x0D\x0A";

        }
        log += cs;
        logged_count++;
    }

    ~CPULogger()
    {
        QDateTime date = QDateTime::currentDateTime();
        QString formattedTime = date.toString("yyyy-MM-dd-hh-mm-ss");
        QString log_file = QDir::currentPath() + "/" + log_name + +"_" + formattedTime + ".log";
        qDebug() << "Logged " << logged_count << "entries";
        qDebug() << "Wrting log to " << log_file;
        QFile qFile(log_file);
        if (qFile.open(QIODevice::WriteOnly))
        {
            qFile.write(log.toUpper().toUtf8());
            qFile.close();
        }
    }
};

#endif // CPULOGGER_H
