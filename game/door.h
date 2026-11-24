#ifndef DOOR_H
#define DOOR_H

#include "gameobject.h"
#include <QString>

enum class DoorKind { Door, Window };
enum class DoorState { Locked, Closed, Open };
enum class DoorMotion { Hinge, Slide };

// 门/窗实体：几何、开合动画、锁状态、portal 和碰撞体定义。
// 状态转换和钥匙/事件规则放在 DoorSystem 里，避免门本身依赖背包和 UI。
class Door : public GameObject
{
    Q_OBJECT
public:
    explicit Door(QObject *parent = nullptr);

    void setHinge(const Vec3 &pos, float closedAngle, float openAngle, float baseYaw = 0.0f);
    // 平移门：closed 是关门时 Door 的局部位移，open 是开门后的局部位移。
    void setSlide(const Vec3 &closed, const Vec3 &open, const Mat3 &rotation);
    void setKind(DoorKind kind) { m_kind = kind; }
    DoorKind kind() const { return m_kind; }
    bool isWindow() const { return m_kind == DoorKind::Window; }

    void setIdentity(const QString &roomId, const QString &markerId);
    const QString &roomId() const { return m_roomId; }
    const QString &markerId() const { return m_markerId; }

    void setLock(const QString &type, const QString &id, const QString &label,
                 const QString &directRoom = QString());
    const QString &lockType() const { return m_lockType; }
    const QString &lockId() const { return m_lockId; }
    const QString &lockLabel() const { return m_lockLabel; }
    const QString &lockDirectRoom() const { return m_lockDirectRoom; }
    QString lockDisplayName() const;

    DoorState state() const { return m_state; }
    bool isLocked() const { return m_state == DoorState::Locked; }
    bool isOpen() const { return m_state == DoorState::Open; }

    // 剧情/交互层状态：0=锁定，1=未锁定，2+=配置表里的自定义状态。
    // 它和物理门板状态（Locked/Closed/Open）分开，避免往 DoorState 里
    // 堆叠动画状态与剧情状态。
    int interactionState() const { return m_interactionState; }
    void setInteractionState(int state) { m_interactionState = state; }

    void unlock();
    void lock();
    void open();
    // 电梯门之类的临时开门：开门后 seconds 秒自动关上。
    void openTemporarily(float seconds);
    // 普通 open()（例如玩家按 F）也自动关门的默认停留时间；0 表示不自动关。
    void setOpenDuration(float seconds) { m_openDuration = seconds; }
    void close();
    void toggle();
    void tick(float dt) override;

    void setPortalId(const QString &portalId) { m_portalId = portalId; }
    const QString &portalId() const { return m_portalId; }

    // 世界空间的静态 OBB 碰撞体：门在关着时启用，窗始终启用。
    void setColliderVolume(const Transform &worldTransform, const Vec3 &halfSize);
    bool hasColliderVolume() const { return m_hasCollider; }
    const Transform &colliderTransform() const { return m_colliderTransform; }
    const Vec3 &colliderHalfSize() const { return m_colliderHalf; }

    float getAngle() const { return angle; }

signals:
    // 状态/开合目标变化时通知 DoorSystem 同步碰撞体。
    void stateChanged();

private:
    DoorMotion m_motion = DoorMotion::Hinge;
    Vec3 hingePos;
    float angle = 0.0f;
    float target = 0.0f;
    float closedAngle = 0.0f;
    float openAngle = 1.4f;
    float baseYaw = 0.0f;
    float speed = 2.5f;

    // Slide motion: t in [0,1] maps closed -> open.
    Vec3 m_slideClosed;
    Vec3 m_slideOpen;
    Mat3 m_slideRotation = Mat3::eye();
    float m_slideT = 0.0f;
    float m_slideTarget = 0.0f;
    float m_autoCloseRemaining = 0.0f;
    float m_openDuration = 0.0f;

    DoorKind m_kind = DoorKind::Door;
    DoorState m_state = DoorState::Closed;
    int m_interactionState = 1;

    QString m_roomId;
    QString m_markerId;
    QString m_lockType;
    QString m_lockId;
    QString m_lockLabel;
    QString m_lockDirectRoom;
    QString m_portalId;

    bool m_hasCollider = false;
    Transform m_colliderTransform;
    Vec3 m_colliderHalf;
};

#endif // DOOR_H
