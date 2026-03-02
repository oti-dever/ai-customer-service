#include "logger.h"
#include <QDateTime>
#include <QtGlobal>

static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    QByteArray localMsg = msg.toLocal8Bit();
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString level;

    switch (type) {
    case QtDebugMsg:
        level = "DEBUG";
        break;
    case QtInfoMsg:
        level = "INFO";
        break;
    case QtWarningMsg:
        level = "WARN";
        break;
    case QtCriticalMsg:
        level = "ERROR";
        break;
    case QtFatalMsg:
        level = "FATAL";
        break;
    }

    QString formatted = QString("[%1] [%2] %3").arg(timestamp, level, msg);
    if (context.file && context.line > 0) {
        formatted += QString(" (%1:%2)").arg(context.file).arg(context.line);
    }

    fprintf(stderr, "%s\n", formatted.toLocal8Bit().constData());
}

void Logger::init()
{
    qInstallMessageHandler(messageHandler);
}
