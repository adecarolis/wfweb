#ifdef USESQL

#ifndef DATABASE_H
#define DATABASE_H

#include <QObject>
#include <QDebug>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QThread>
#include <QStandardPaths>

class database
{
public:
    explicit database();
    virtual ~database();
    bool open();
    void close();
    QSqlQuery query(QString query);

signals:

public slots:
private:
    bool check();
    QSqlDatabase db;
    QSqlQuery qu;

};

#endif

#endif
