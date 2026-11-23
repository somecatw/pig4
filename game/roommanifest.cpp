#include "roommanifest.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDebug>

static Vec3 vec3From(const QJsonArray &a, const Vec3 &fallback = {})
{
    if (a.size() != 3) return fallback;
    return Vec3(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()));
}

QList<RoomMarker> RoomManifest::load(const QString &path)
{
    QList<RoomMarker> result;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "room manifest not found:" << path;
        return result;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "room manifest parse error:" << path << err.errorString();
        return result;
    }

    const QJsonArray markers = doc.object().value("markers").toArray();
    for (const QJsonValue &v : markers) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        RoomMarker m;
        m.name = o.value("name").toString();
        m.type = o.value("type").toString().toUpper();
        m.id = o.value("id").toString();
        m.side = o.value("side").toString();
        m.style = o.value("style").toString();
        m.asset = o.value("asset").toString();
        m.texture = o.value("texture").toString();
        m.position = vec3From(o.value("position").toArray());
        m.basisX = vec3From(o.value("basisX").toArray(), {1, 0, 0});
        m.basisY = vec3From(o.value("basisY").toArray(), {0, 1, 0});
        m.basisZ = vec3From(o.value("basisZ").toArray(), {0, 0, 1});
        const QJsonArray size = o.value("size").toArray();
        if (size.size() == 2) {
            m.width = float(size[0].toDouble());
            m.height = float(size[1].toDouble());
        }
        m.thickness = float(o.value("thickness").toDouble());
        m.openAngle = float(o.value("openAngle").toDouble());
        m.billboard = o.value("billboard").toBool(false);
        m.halfSize = vec3From(o.value("halfSize").toArray());
        result.push_back(m);
    }
    return result;
}
