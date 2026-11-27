#ifndef SETMENUTABLE_H
#define SETMENUTABLE_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

// The SET menu table (resources/web-shared/setmenu/<model>.json, the one
// the page's window draws) as the server needs it for backup and restore:
// which items there are, whether one may be written, and whether a value
// is one the item takes.  Pure Qt5Core; the format is documented at the top
// of resources/web-shared/setmenu.js.
namespace SetMenuDef {

struct Item {
    QString no;           // "0061"
    QString name;
    QString path;         // "SET > Connectors"
    QString type;         // bool | enum | level | number | raw
    bool readonly = false;    // shown, never written (CI-V link, keying lines)
    bool restore = true;      // false: the clock, the radio's own calibration
    QStringList codes;        // bool / enum: the data codes it takes
    QStringList labels;       // their labels, same order
    int digits = 4;           // level / number: BCD digits
    int min = 0;
    int max = -1;             // -1: all the digits allow
    bool writable() const { return !readonly && type != QLatin1String("raw"); }
    bool restorable() const { return !readonly && restore; }
};

struct Table {
    QString model;
    QList<Item> items;
    QHash<QString, int> index;    // item number -> position in items
    const Item *find(const QString &no) const;
};

// false with *error when the JSON is not a table.
bool parse(const QByteArray &json, Table *table, QString *error);

// Data as hex, no spaces, upper case ("0128").  Empty when the value is one
// the item takes; else why not.  currentData (what the radio holds now) is
// used for raw items, whose value must have the same length.
QString validate(const Item &item, const QString &dataHex, const QString &currentData = QString());

// The label of a bool / enum code, else empty.
QString label(const Item &item, const QString &dataHex);

// "1A 05 00 61" and "1A 05 00 61 01"
QString readHex(const Item &item);
QString setHex(const Item &item, const QString &dataHex);

} // namespace SetMenuDef

#endif // SETMENUTABLE_H
