#include <QDebug>
#include <QFile>
#include <QMessageBox>

#include "emulator/utils.h"
#include "disasm.h"

DisAsm::DisAsm(QObject *parent)
    : QObject{parent},
    count(0),
    max_command_length(0)
{}

DisAsm::DisAsm(QObject *parent, QString file_name)
    : DisAsm{parent}

{
    load_file(file_name);
}

void DisAsm::load_file(QString file_name)
{
    QString hex_digits = "0123456789ABCDEF";
    QString letters = "abcdefghijklmnopqrstuvwxyz";

    qDebug() << "Loading " << file_name;
    QFile file(file_name);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(0, DisAsm::tr("Error"), DisAsm::tr("Error reading CPU instructions file %1").arg(file_name));
        return;
    }

    QTextStream in(&file);
    while (!in.atEnd())
    {
        QString line = in.readLine();
        QStringList parts = line.split(u'\x09', Qt::SkipEmptyParts);

        QStringList codes = parts.at(0).split(' ', Qt::SkipEmptyParts);

        ins[count].length = 0;

        for (int i = 0; i<codes.size(); i++)
        {
            QString code = codes.at(i);
            if (code.length() == 2 && hex_digits.contains(code.at(0)))
            {
                //HEX byte
                ins[count].bytes[ins[count].length].is_instr = true;
                ins[count].bytes[ins[count].length].value = parse_numeric_value("$" + code);
                ins[count].length++;
            } else
            if (code.length() == 1 && letters.contains(code.at(0)))
            {
                //data byte
                ins[count].bytes[ins[count].length].is_instr = false;
                ins[count].bytes[ins[count].length].value = code.at(0).unicode();
                ins[count].length++;
            }
        }

        if ( ins[count].length != parse_numeric_value(parts.at(2)) )
        {
            QMessageBox::critical(0, DisAsm::tr("Error"), DisAsm::tr("CPU instruction %1 length is incorrect").arg(parts.at(1)));
        }

        if (ins[count].length > max_command_length) max_command_length = ins[count].length;

        ins[count].text = parts.at(1);

        count++;
    }
    file.close();

}

unsigned int DisAsm::disassemle(CommandBytes bytes, unsigned int PC, unsigned int max_len, QString *output)
{
    unsigned int result = 0;
    int p;

    for (unsigned int i = 0; i < count; i++)
    {
        if (ins[i].length > max_len) continue; //Skip too long instructions
        else {
            unsigned int j;
            for (j = 0; j < ins[i].length; j++)
                if (ins[i].bytes[j].is_instr && (ins[i].bytes[j].value != *bytes[j])) break;

            if (j == ins[i].length)
            {
                QString s = ins[i].text;
                result = ins[i].length;
                p = ins[i].text.indexOf("$nn");
                if (p >= 0)
                {
                    unsigned int c = 0;
                    unsigned int v = 0;
                    for (j = 0; j < ins[i].length; j++)
                        if (!ins[i].bytes[j].is_instr && ins[i].bytes[j].value == QChar('n').unicode())
                            v += (*bytes)[j] << (c++*8);
                    QString hexval = QString("%1").arg(v, 4, 16, QChar('0')).toUpper();
                    s = s.replace("$nn", hexval);
                };

                p = ins[i].text.indexOf("$n");
                if (p >= 0)
                {
                    unsigned int v = 0;
                    for (j = 0; j < ins[i].length; j++)
                        if (!ins[i].bytes[j].is_instr && ins[i].bytes[j].value == QChar('n').unicode())
                            v = *bytes[j];
                    QString hexval = QString("%1").arg(v, 2, 16, QChar('0'));
                    s = s.replace("$n", hexval);
                };

                p = ins[i].text.indexOf("$d");
                if (p >= 0)
                {
                    unsigned int v=0;
                    for (j = 0; j < ins[i].length; j++)
                        if (!ins[i].bytes[j].is_instr && ins[i].bytes[j].value == QChar('d').unicode())
                            v = *bytes[j];
                    QString hexval = QString("%1").arg(v, 2, 16, QChar('0'));
                    s = s.replace("$d", hexval);
                };

                //TODO: implement "PC+$e" for Z80
                *output = s;
                return result;
            }
        }
    }
    return result;
}

QString bytes_dump(CommandBytes bytes, unsigned int length)
{
    QString s = "";
    for (unsigned int i = 0; i < length; i++)
        s += QString("%1 ").arg((*bytes)[i], 2, 16,QChar('0')).toUpper();
    return s;
}

