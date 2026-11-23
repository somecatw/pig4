#ifndef DOORSYSTEM_H
#define DOORSYSTEM_H

#include <QHash>
#include <QList>
#include <QString>

#include "buildingmanifest.h"
#include "door.h"

class GameEvents;
class Stage3D;

// 门/窗逻辑：状态转换、锁与钥匙、事件连接、碰撞体同步。
class DoorSystem
{
public:
    enum class Action { None, Unlock, Open, Close, Climb };

    struct Query
    {
        QString prompt;
        Action action = Action::None;
    };

    explicit DoorSystem(Stage3D *stage);

    void addDoor(Door *door);
    void applyLock(const LockDef &lock);
    void bindConnections(const QList<ConnectionDef> &connections, GameEvents &events);

    Door *findDoor(const QString &roomId, const QString &markerId) const;
    const QList<Door*> &doors() const { return m_doors; }

    // selectedItemId 是当前背包选中的道具；钥匙必须被选中才能用来开锁。
    // currentRoom 用来判断玩家是否站在 directRoom 这一侧。
    Query query(const Door *door, const QString &selectedItemId,
                const QString &currentRoom) const;
    Action interact(Door *door, const QString &selectedItemId,
                    const QString &currentRoom, QString *notice = nullptr);

    void applyAction(Door *door, const QString &action);
    void closeAndLock(Door *door);
    void forceOpenAll();
    void syncCollider(Door *door);

private:
    static void (Door::*actionFor(const QString &name))();

    Stage3D *m_stage = nullptr;
    QList<Door*> m_doors;
    QHash<Door*, int> m_colliderIds;
};

#endif // DOORSYSTEM_H
