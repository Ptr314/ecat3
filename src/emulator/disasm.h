#ifndef DISASM_H
#define DISASM_H

#include <QObject>

struct InstructionByte {
    bool is_instr;
    uint8_t value;
};

struct Instruction {
    InstructionByte bytes[16];
    unsigned int length;
    QString text;
};

typedef uint8_t (*CommandBytes)[15];

class DisAsm : public QObject
{
    Q_OBJECT
private:
    Instruction ins[2048];
    unsigned int count;

public:
    unsigned int max_command_length;

    explicit DisAsm(QObject *parent = nullptr);
    DisAsm(QObject *parent, QString file_name);

    void load_file(QString file_name);
    unsigned int disassemle(CommandBytes bytes, unsigned int PC, unsigned int max_len, QString * output);

signals:

};

QString bytes_dump(CommandBytes bytes, unsigned int length);

#endif // DISASM_H
