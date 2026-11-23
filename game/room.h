#ifndef ROOM_H
#define ROOM_H

#include "buildingmanifest.h"
#include "gameobject.h"

class Door;
class Hotspot;
class Puzzle;
class Room;

// 一个可交互目标。门、贴纸、热区可以共享同一个 id。
// door/sprite 提供可视/射线网格；hotspot 提供额外的 OBB 触发体积。
struct InteractTarget
{
    Door *door = nullptr;
    MeshActor *sprite = nullptr;
    Hotspot *hotspot = nullptr;
    // 房间 OBJ 里 INTERACT_<id> 命名的真实几何；瞄准高亮和射线测试都用它。
    QList<MeshActor*> interactMeshes;
    Puzzle *puzzle = nullptr;
    Room *room = nullptr;
    QString id;
    // 通用 3D 交互物（HOT/TRIG 等）的配置；空则走默认提示。
    QString prompt;   // 准星提示，代码会加 "[F] " 前缀
    QString notice;   // 按 F 后显示的文本
    // 通用拾取：非空时按 F 把这个 item 放进背包。
    QString pickupItem;
    QString pickupName;
    QString pickupIcon;
    bool collected = false;
};

class Room : public GameObject
{
public:
    explicit Room(const RoomDef &def, QObject *parent = nullptr);

    RoomDef def;
    GameObject *sceneRoot = nullptr;
    Vec3 localMin;
    Vec3 localMax;
    Vec3 localCenter;
};

#endif // ROOM_H
