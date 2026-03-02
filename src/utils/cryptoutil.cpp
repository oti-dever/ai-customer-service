#include "cryptoutil.h"
#include <QCryptographicHash>
#include <QRandomGenerator>

QString CryptoUtil::generateSalt()
{
    QByteArray bytes(16, 0);
    QRandomGenerator::global()->fillRange(reinterpret_cast<quint32*>(bytes.data()), bytes.size() / sizeof(quint32));
    return QString::fromUtf8(bytes.toHex());
}

QString CryptoUtil::hashPassword(const QString& password, const QString& salt)
{
    QByteArray data = (salt + password).toUtf8();
    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    return QString::fromUtf8(hash.toHex());
}

bool CryptoUtil::verifyPassword(const QString& password, const QString& salt, const QString& storedHash)
{
    QString computed = hashPassword(password, salt);
    return computed == storedHash;
}
