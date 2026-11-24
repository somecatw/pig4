#ifndef ROOMMANAGER_H
#define ROOMMANAGER_H

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include "buildingmanifest.h"
#include "doorsystem.h"
#include "gameevents.h"
#include "room.h"

class Puzzle;
class Stage3D;

// 读取 building.json，加载每个房间，按非 PH AABB 归一化到 grid cell。
// 目前一次性加载所有房间；后续 streaming/active-room 在此基础上加。
class RoomManager
{
public:
    explicit RoomManager(Stage3D *stage);
    ~RoomManager();

    bool loadBuilding(const QString &path);

    Room *room(const QString &id) const;
    QList<Room*> rooms() const { return m_roomOrder; }
    const QList<Door*> &doors() const { return m_doorSystem.doors(); }
    const QList<Puzzle*> &puzzles() const { return m_puzzles; }
    QList<InteractTarget> &interactables() { return m_interactables; }
    const QList<InteractTarget> &interactables() const { return m_interactables; }
    InteractTarget *findInteractableById(const QString &id);
    DoorSystem *doorSystem() { return &m_doorSystem; }
    const DoorSystem *doorSystem() const { return &m_doorSystem; }
    GameEvents &events() { return m_events; }
    const GameEvents &events() const { return m_events; }
    const BuildingDef &building() const { return m_def; }
    GameObject *buildingRoot() const { return m_buildingRoot; }
    QString buildingDir() const { return m_dir; }

    // portal 与房间源坐标转换：进入点按目标房间自己的 OBJ 坐标书写。
    const PortalDef *portalFor(const Room *room, const QString &marker) const;
    Vec3 roomSourceToWorld(const Room *room, const Vec3 &sourcePos) const;
    Room *roomAt(const Vec3 &worldPos) const;

    // L2：只提交当前房间、同层同 col/row 的房间，以及 verticalLinks 的
    // 另一侧。静态 BVH 和资源仍然全部常驻。
    void updateRenderRooms(const QString &currentRoomId);
    QStringList renderRoomIds() const { return m_renderRoomIds; }

    // L1：按房间建立静态 BVH；碰撞/静态遮挡只查询 current、同层直接相邻
    // 房间和 verticalLinks 的另一侧。动态碰撞体仍由 Stage3D 全局处理。
    void buildStaticCollisionBVHs();
    void updateCollisionRooms(const QString &currentRoomId);
    QStringList collisionRoomIds() const { return m_collisionRoomIds; }

private:
    Room *loadRoomScene(const RoomDef &def);
    void createRoomObjects(Room *room);
    InteractTarget &ensureInteractTarget(Room *room, const QString &id);
    bool computeBounds(Room *room);

    Stage3D *m_stage;
    BuildingDef m_def;
    QString m_dir;
    GameObject *m_buildingRoot = nullptr;
    QHash<QString, Room*> m_rooms;
    QList<Room*> m_roomOrder;
    QString m_renderRoomId;
    QStringList m_renderRoomIds;
    QString m_collisionRoomId;
    QStringList m_collisionRoomIds;
    DoorSystem m_doorSystem;
    GameEvents m_events;
    QList<InteractTarget> m_interactables;
    QList<Puzzle*> m_puzzles;
};

#endif // ROOMMANAGER_H
