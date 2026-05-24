#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QCoreApplication>
#include <QSettings>
#include <QString>

namespace AppSettings {

inline void configureApplication(QCoreApplication& app)
{
    app.setOrganizationName(QStringLiteral("YangYangAI"));
    app.setApplicationName(QStringLiteral("CustomerServiceDemo"));
}

inline QSettings create()
{
    return QSettings(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
}

} // namespace AppSettings

#endif // APPSETTINGS_H
