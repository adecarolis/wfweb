#ifdef USESQL

#include "database.h"
#include "logcategories.h"

database::database()
{
    open();
}

database::~database()
{
}

bool database::open()
{
    auto name = "my_db_" + QString::number((quint64)QThread::currentThread(), 16);
    if (QSqlDatabase::contains(name))
    {
        db = QSqlDatabase::database(name);
        qu = QSqlQuery(db);
        return true;
    }
    else {
        qInfo(logCluster()) << "Creating new connection" << name;
        db = QSqlDatabase::addDatabase("QSQLITE", name);
        qu = QSqlQuery(db);
    }

    //QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/" + "wfview.db";
    QString path = ":memory:";
    qInfo(logCluster()) << "DB Filename" << path;
    db.setDatabaseName(path);
    if (db.isValid())
    {
        db.open();
        if (check()) {
            return true;
        }
    }
    qWarning(logCluster()) << "Database is not valid!";
    return false;
}

void database::close()
{
    auto name = "my_db_" + QString::number((quint64)QThread::currentThread(), 16);
    qInfo(logCluster()) << "Closing database connection:" << name;
    db.close();
}



QSqlQuery database::query(QString query)
{
    if (!db.isOpen())
    {
        qWarning(logCluster()) << "Query Database is not open!";
        db.open();
    }
    qu.exec(query);
    return qu;
}



bool database::check() 
{
    if (db.isOpen()) {
        for (const auto& table : db.tables())
        {
            if (table == "spots")
            {
                qInfo(logCluster()) << "DB Contains spots table";
                return true;
            }
        }
        qInfo(logCluster()) << "Creating spots table";
        // Spots table does not exist, need to create it.
        /*
            QString dxcall;
            double frequency;
            QString spottercall;
            QDateTime timestamp;
            QString mode;
            QString comment;
            QCPItemText* text = Q_NULLPTR;*/
        qu.exec("CREATE TABLE spots "
            "(id INTEGER PRIMARY KEY, "
            "type VARCHAR(3),"
            "dxcall VARCHAR(30),"
            "spottercall VARCHAR(30),"
            "frequency DOUBLE,"
            "timestamp DATETIME,"
            "mode VARCHAR(30),"
            "comment VARCHAR(255) )");
        qu.exec("CREATE INDEX spots_index ON spots(type,dxcall,frequency,timestamp)");
        return true;
    }
    else {
        qWarning(logCluster()) << "Database is not open";

    }
    return false;
}

#endif