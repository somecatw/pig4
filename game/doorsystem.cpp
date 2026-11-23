#include "doorsystem.h"

#include "gameevents.h"
#include "stage3d.h"

#include <QDebug>

namespace {

// 锁名用 rich text 标绿，交叉准星提示和底部对话条共用。
// 文本来自 JSON，记得转义，避免 label 里出现意外的 HTML。
QString lockedMessage(const Door *door, const QString &currentRoom)
{
    QString firstLine;
    if (door == nullptr || door->lockType() == QStringLiteral("event")) {
        firstLine = QStringLiteral("锁住了。");
    } else {
        const QString name = door->lockDisplayName();
        firstLine = name.isEmpty()
            ? QStringLiteral("锁住了。")
            : QStringLiteral("锁住了，需要 <span style=\"color:#38c838\">“%1”</span>")
                  .arg(name.toHtmlEscaped());
    }

    // 只有站在指定房间这一侧，才额外显示可以直接打开反锁。
    if (door != nullptr && !door->lockDirectRoom().isEmpty()
        && door->lockDirectRoom() == currentRoom) {
        return firstLine + QStringLiteral("<br>从这一侧反锁着，可以直接解锁");
    }
    return firstLine;
}

QString keyUnlockPrompt(const Door *door)
{
    const QString name = door->lockDisplayName();
    if (name.isEmpty())
        return QStringLiteral("[F] 开锁");
    return QStringLiteral("[F] 用 <span style=\"color:#38c838\">%1</span> 开锁")
        .arg(name.toHtmlEscaped());
}

} // namespace

DoorSystem::DoorSystem(Stage3D *stage)
    : m_stage(stage)
{
}

void DoorSystem::addDoor(Door *door)
{
    if (door == nullptr || m_doors.contains(door))
        return;
    m_doors.push_back(door);

    // 自动关门等 Door 内部状态变化也要同步碰撞体；用 door 作为 context，
    // door 销毁时连接自动断开。
    QObject::connect(door, &Door::stateChanged, door, [this, door] {
        syncCollider(door);
    });

    if (m_stage != nullptr && door->hasColliderVolume()) {
        const int colliderId = m_stage->addDynamicCollider(
            door->colliderTransform(), door->colliderHalfSize(), true);
        m_colliderIds.insert(door, colliderId);
        qDebug() << "door registered:" << door->roomId() << door->markerId()
                 << "kind" << (door->isWindow() ? "window" : "door")
                 << "collider" << colliderId;
    }
    syncCollider(door);
}

Door *DoorSystem::findDoor(const QString &roomId, const QString &markerId) const
{
    for (Door *door : m_doors) {
        if (door->roomId() == roomId && door->markerId() == markerId)
            return door;
    }
    return nullptr;
}

void DoorSystem::applyLock(const LockDef &lock)
{
    Door *door = findDoor(lock.room, lock.marker);
    if (door == nullptr) {
        qWarning() << "door lock target not found:" << lock.room << lock.marker;
        return;
    }
    door->setLock(lock.type, lock.id, lock.label, lock.directRoom);
    syncCollider(door);
    qDebug() << "door lock:" << lock.room << lock.marker
             << lock.type << lock.id << "state" << int(door->state());
}

void (Door::*DoorSystem::actionFor(const QString &name))()
{
    static const QHash<QString, void (Door::*)()> actions = {
        {QStringLiteral("unlock"), &Door::unlock},
        {QStringLiteral("lock"),   &Door::lock},
        {QStringLiteral("open"),   &Door::open},
        {QStringLiteral("close"),  &Door::close},
        {QStringLiteral("toggle"), &Door::toggle},
    };
    return actions.value(name, nullptr);
}

void DoorSystem::bindConnections(const QList<ConnectionDef> &connections, GameEvents &events)
{
    for (const ConnectionDef &connection : connections) {
        Door *door = findDoor(connection.room, connection.marker);
        auto action = actionFor(connection.action);
        if (door == nullptr || action == nullptr) {
            qWarning() << "bad connection:" << connection.event
                       << connection.room << connection.marker << connection.action;
            continue;
        }
        events.on(connection.event, [this, door, action] {
            (door->*action)();
            syncCollider(door);
            qDebug() << "door event action:" << door->roomId() << door->markerId()
                     << "state" << int(door->state());
        });
        qDebug() << "connection:" << connection.event << "->"
                 << connection.room << connection.marker << connection.action;
    }
}

DoorSystem::Query DoorSystem::query(const Door *door, const QString &selectedItemId,
                                    const QString &currentRoom) const
{
    Query result;
    if (door == nullptr)
        return result;

    const QString openPrompt = door->isWindow()
        ? QStringLiteral("[F] 开窗") : QStringLiteral("[F] 开门");
    const QString closePrompt = door->isWindow()
        ? QStringLiteral("[F] 关窗") : QStringLiteral("[F] 关门");

    if (door->isOpen()) {
        if (!door->portalId().isEmpty()) {
            result.prompt = door->isWindow()
                ? QStringLiteral("[F] 翻窗") : QStringLiteral("[F] 进入");
            result.action = Action::Climb;
        } else {
            result.prompt = closePrompt;
            result.action = Action::Close;
        }
        return result;
    }

    if (door->isLocked()) {
        // 站在 directRoom 这一侧时，门虽然锁着，但反锁就在手边，
        // 可以直接打开；钥匙那行仍然显示，作为另一侧的说明。
        const bool directHere = !door->lockDirectRoom().isEmpty()
                             && door->lockDirectRoom() == currentRoom;
        if (directHere) {
            result.prompt = lockedMessage(door, currentRoom);
            result.action = Action::Unlock;
            return result;
        }
        // 钥匙必须被选中，才会出现明确的“用钥匙开锁”动作。
        // 事件锁只能等事件解锁（不要告诉玩家，原汁原味这一块）。
        if (door->lockType() == QStringLiteral("key")
            && !door->lockId().isEmpty()
            && selectedItemId == door->lockId()) {
            result.prompt = keyUnlockPrompt(door);
            result.action = Action::Unlock;
        } else {
            result.prompt = lockedMessage(door, currentRoom);
            result.action = Action::None;
        }
        return result;
    }

    result.prompt = openPrompt;
    result.action = Action::Open;
    return result;
}

DoorSystem::Action DoorSystem::interact(Door *door, const QString &selectedItemId,
                                        const QString &currentRoom, QString *notice)
{
    if (door == nullptr)
        return Action::None;

    if (door->isOpen()) {
        if (!door->portalId().isEmpty())
            return Action::Climb;
        door->close();
        syncCollider(door);
        return Action::Close;
    }

    if (door->isLocked()) {
        const bool directHere = !door->lockDirectRoom().isEmpty()
                             && door->lockDirectRoom() == currentRoom;
        if (directHere) {
            // 反锁就在这一侧，但“解锁”和“开门”仍然是两个动作。
            door->unlock();
            syncCollider(door);
            return Action::Unlock;
        }
        if (door->lockType() == QStringLiteral("key")
            && !door->lockId().isEmpty()
            && selectedItemId == door->lockId()) {
            // 只开锁，不顺手开门；下一次 F 才会执行开门/开窗。
            door->unlock();
            syncCollider(door);
            return Action::Unlock;
        }
        if (notice != nullptr) {
            // 同上
            *notice = lockedMessage(door, currentRoom);
        }
        return Action::None;
    }

    door->open();
    syncCollider(door);
    return Action::Open;
}

void DoorSystem::applyAction(Door *door, const QString &action)
{
    if (door == nullptr)
        return;
    auto fn = actionFor(action);
    if (fn == nullptr) {
        qWarning() << "unknown door action:" << action;
        return;
    }
    (door->*fn)();
    syncCollider(door);
}

void DoorSystem::closeAndLock(Door *door)
{
    if (door == nullptr)
        return;
    door->close();
    door->lock();
    syncCollider(door);
}

void DoorSystem::forceOpenAll()
{
    for (Door *door : m_doors) {
        if (door->isLocked())
            door->unlock();
        if (!door->isOpen())
            door->open();
        syncCollider(door);
    }
}

void DoorSystem::syncCollider(Door *door)
{
    if (door == nullptr || m_stage == nullptr)
        return;
    auto it = m_colliderIds.constFind(door);
    if (it == m_colliderIds.constEnd())
        return;
    // 窗户永远挡人，门只在关着/锁住时挡人。
    const bool enabled = door->isWindow() || !door->isOpen();
    m_stage->setDynamicColliderEnabled(*it, enabled);
}
