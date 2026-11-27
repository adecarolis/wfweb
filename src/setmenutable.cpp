#include "setmenutable.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace SetMenu {

const Item *Table::find(const QString &no) const
{
    auto it = index.constFind(no);
    return it == index.constEnd() ? nullptr : &items.at(it.value());
}

bool parse(const QByteArray &json, Table *table, QString *error)
{
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = QStringLiteral("not JSON: ") + perr.errorString();
        return false;
    }
    const QJsonObject root = doc.object();
    Table t;
    t.model = root.value(QStringLiteral("model")).toString();
    if (t.model.isEmpty() || !root.value(QStringLiteral("groups")).isArray()) {
        *error = QStringLiteral("a table needs model and groups");
        return false;
    }
    static const QRegularExpression itemRe(QStringLiteral("^[0-9]{4}$"));
    for (const QJsonValue &gv : root.value(QStringLiteral("groups")).toArray()) {
        const QJsonObject g = gv.toObject();
        const QString path = g.value(QStringLiteral("path")).toString();
        for (const QJsonValue &iv : g.value(QStringLiteral("items")).toArray()) {
            const QJsonObject o = iv.toObject();
            Item it;
            it.no = o.value(QStringLiteral("item")).toString();
            if (!itemRe.match(it.no).hasMatch() || t.index.contains(it.no)) {
                *error = QStringLiteral("bad or duplicate item number: ") + it.no;
                return false;
            }
            it.name = o.value(QStringLiteral("name")).toString();
            it.path = path;
            it.type = o.value(QStringLiteral("type")).toString();
            it.readonly = o.value(QStringLiteral("readonly")).toBool(false);
            it.restore = o.value(QStringLiteral("restore")).toBool(true);
            if (it.type == QLatin1String("bool") || it.type == QLatin1String("enum")) {
                QJsonArray opts = o.value(QStringLiteral("options")).toArray();
                if (opts.isEmpty() && it.type == QLatin1String("bool"))
                    opts = QJsonArray{QJsonArray{"00", "OFF"}, QJsonArray{"01", "ON"}};
                for (const QJsonValue &ov : opts) {
                    const QJsonArray pair = ov.toArray();
                    it.codes << pair.at(0).toString().toUpper();
                    it.labels << pair.at(1).toString();
                }
            } else if (it.type == QLatin1String("level") || it.type == QLatin1String("number")) {
                it.digits = o.value(QStringLiteral("digits")).toInt(4);
                it.min = o.value(QStringLiteral("min")).toInt(0);
                it.max = o.value(QStringLiteral("max")).toInt(-1);
            } else if (it.type != QLatin1String("raw")) {
                *error = QStringLiteral("item %1: unknown type %2").arg(it.no, it.type);
                return false;
            }
            t.index.insert(it.no, t.items.size());
            t.items.append(it);
        }
    }
    *table = t;
    return true;
}

static bool isHex(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^([0-9A-F]{2})+$"));
    return re.match(s).hasMatch();
}

QString validate(const Item &item, const QString &dataIn, const QString &currentData)
{
    const QString d = dataIn.toUpper().remove(QLatin1Char(' '));
    if (!isHex(d))
        return QStringLiteral("not hex bytes");
    if (item.type == QLatin1String("bool") || item.type == QLatin1String("enum")) {
        if (!item.codes.contains(d))
            return QStringLiteral("%1 is not one of the item's values").arg(d);
        return QString();
    }
    if (item.type == QLatin1String("level") || item.type == QLatin1String("number")) {
        static const QRegularExpression bcd(QStringLiteral("^[0-9]+$"));
        if (d.size() != item.digits || !bcd.match(d).hasMatch())
            return QStringLiteral("expected %1 BCD digits").arg(item.digits);
        const int v = d.toInt();
        int max = item.max;
        if (max < 0) {
            max = 1;
            for (int i = 0; i < item.digits; ++i) max *= 10;
            --max;
        }
        if (v < item.min || v > max)
            return QStringLiteral("%1 is out of range %2-%3").arg(v).arg(item.min).arg(max);
        return QString();
    }
    // raw: whatever the radio holds, in the same shape.
    if (!currentData.isEmpty() && currentData.size() != d.size())
        return QStringLiteral("%1 bytes where the radio holds %2").arg(d.size() / 2).arg(currentData.size() / 2);
    for (int i = 0; i + 1 < d.size(); i += 2) {
        const QString b = d.mid(i, 2);
        if (b == QLatin1String("FD") || b == QLatin1String("FE"))
            return QStringLiteral("FE / FD inside the value");
    }
    return QString();
}

QString label(const Item &item, const QString &dataHex)
{
    const int i = item.codes.indexOf(dataHex.toUpper());
    return i >= 0 ? item.labels.value(i) : QString();
}

static QString spaced(const QString &hex)
{
    QString out;
    for (int i = 0; i < hex.size(); i += 2) {
        if (i) out += QLatin1Char(' ');
        out += hex.mid(i, 2);
    }
    return out;
}

QString readHex(const Item &item)
{
    return QStringLiteral("1A 05 ") + spaced(item.no);
}

QString setHex(const Item &item, const QString &dataHex)
{
    return readHex(item) + QLatin1Char(' ') + spaced(dataHex.toUpper().remove(QLatin1Char(' ')));
}

} // namespace SetMenu
