#ifndef CRYPTOUTIL_H
#define CRYPTOUTIL_H

#include <QByteArray>
#include <QString>

class CryptoUtil
{
public:
    static QString generateSalt();
    static QString hashPassword(const QString& password, const QString& salt);
    static bool verifyPassword(const QString& password, const QString& salt, const QString& storedHash);
};

#endif // CRYPTOUTIL_H
