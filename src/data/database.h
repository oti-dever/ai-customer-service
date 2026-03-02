#ifndef DATABASE_H
#define DATABASE_H

#include <QSqlDatabase>
#include <QString>

class Database {
private:
    Database() = default;
    ~Database() = default;
    QString m_path;
public:
    static Database& getInstance() {
        static Database db;
        return db;
    }
    bool open(const QString& path = QString());
    void close();
    bool isOpen() const;
    QSqlDatabase connection() const;
    bool runMigrations();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
};

#endif // DATABASE_H
