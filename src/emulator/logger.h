#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QDateTime>
#include <QFile>
#include <QDir>

#include "globals.h"

#ifndef LOG_LIMIT
#define LOG_LIMIT 0
#endif

class Logger:public QObject
{
    Q_OBJECT
private:
    unsigned int logged_count;
    QString log;
    QString log_name;

public:
    Logger(QString log_name):
        logged_count(0),
        log(""),
        log_name(log_name)
    {}

    bool log_available()
    {
        return logged_count < LOG_LIMIT;
    }

    void logs(QString s)
    {
        if (log_available())
        {
            log += s + "\x0D\x0A";
            logged_count++;
        }
    }


    ~Logger()
    {
        if (logged_count > 0)
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
        } else {
            qDebug() << "Log is empty, none to write";
        }
    }
};


#endif // LOGGER_H
