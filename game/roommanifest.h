#ifndef ROOMMANIFEST_H
#define ROOMMANIFEST_H

#include <QString>
#include <QList>
#include "gameobject.h"

struct RoomMarker
{
    QString name;
    QString type;
    QString id;
    QString side;
    QString style;
    QString asset;
    QString texture;

    Vec3 position;
    Vec3 basisX = {1, 0, 0};
    Vec3 basisY = {0, 1, 0};
    Vec3 basisZ = {0, 0, 1};
    Vec3 halfSize;

    float width = 0.0f;
    float height = 0.0f;
    float thickness = 0.0f;
    float openAngle = 0.0f;
    bool billboard = false;
};

class Hotspot : public GameObject
{
public:
    QString id;
    Vec3 center;
    Vec3 halfSize;
    explicit Hotspot(QObject *parent = nullptr) : GameObject(parent) {}
};

class RoomManifest
{
public:
    static QList<RoomMarker> load(const QString &path);
};

#endif // ROOMMANIFEST_H
