#include <QtTest>
#include "utils/cryptoutil.h"

class TestCryptoUtil : public QObject
{
    Q_OBJECT

private slots:
    void verifyPassword_roundtrip();
};

void TestCryptoUtil::verifyPassword_roundtrip()
{
    const QString salt = CryptoUtil::generateSalt();
    QVERIFY(!salt.isEmpty());
    const QString hash = CryptoUtil::hashPassword(QStringLiteral("secret"), salt);
    QVERIFY(CryptoUtil::verifyPassword(QStringLiteral("secret"), salt, hash));
    QVERIFY(!CryptoUtil::verifyPassword(QStringLiteral("wrong"), salt, hash));
}

QTEST_MAIN(TestCryptoUtil)
#include "test_cryptoutil.moc"
