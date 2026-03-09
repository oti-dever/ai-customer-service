#ifndef LOGGER_H
#define LOGGER_H

#include <QDebug>
#include <QFile>
#include <QMessageLogContext>
#include <QMutex>
#include <QString>

class Logger
{
public:
    static void init();
    static void shutdown();
    static QString logFilePath();

private:
    static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);

    static QFile s_logFile;
    static QMutex s_mutex;
    static bool s_initialized;
};

#endif // LOGGER_H
