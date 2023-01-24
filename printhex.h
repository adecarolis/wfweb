#ifndef PRINTHEX_H
#define PRINTHEX_H
#include <QByteArray>
#include <QString>

#include "logcategories.h"

QString inline getHex(const QByteArray &pdata)
{
    QString head = "---- Begin hex dump -----:\n";
    QString sdata("DATA:  ");
    QString index("INDEX: ");

    for(int i=0; i < pdata.length(); i++)
    {
        sdata.append(QString("%1 ").arg((unsigned char)pdata[i], 2, 16, QChar('0')) );
        index.append(QString("%1 ").arg(i, 2, 10, QChar('0')));
    }

        sdata.append("\n");
        index.append("\n");

    QString tail = "----  End hex dump  -----\n";
    return head + sdata + index + tail;
}

void inline printHexNow(const QByteArray &pdata, const QLoggingCategory &cat)
{
    QString d = getHex(pdata);
    // These lines are needed to keep the formatting as expected in the log file
    if(d.endsWith("\n"))
    {
        d.chop(1);
    }
    QStringList s = d.split("\n");
    for(int i=0; i < s.length(); i++)
    {
        qDebug(cat) << s.at(i);
    }
}

#endif // PRINTHEX_H
