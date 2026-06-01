#ifndef TESTDATABASE_H
#define TESTDATABASE_H

#include "data/database.h"

#include <QTemporaryDir>
#include <QtGlobal>

class ScopedTestDatabase
{
public:
    ScopedTestDatabase()
    {
        Q_ASSERT_X(m_tempDir.isValid(), "ScopedTestDatabase", "failed to create temp dir");
        Database::getInstance().close();
        const bool ok = Database::getInstance().open(m_tempDir.filePath(QStringLiteral("app.db")));
        Q_ASSERT_X(ok, "ScopedTestDatabase", "failed to open temp database");
    }

    ~ScopedTestDatabase()
    {
        Database::getInstance().close();
    }

    QString databasePath() const
    {
        return m_tempDir.filePath(QStringLiteral("app.db"));
    }

private:
    QTemporaryDir m_tempDir;
};

#endif // TESTDATABASE_H
