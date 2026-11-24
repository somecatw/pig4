#include "door.h"
#include <QDebug>
#include <algorithm>
#include <cmath>

Door::Door(QObject *parent)
    : GameObject(parent)
{
}

void Door::setHinge(const Vec3 &pos, float closed, float open, float yaw)
{
    m_motion = DoorMotion::Hinge;
    hingePos = pos;
    closedAngle = closed;
    openAngle = open;
    baseYaw = yaw;
    angle = closed;
    target = closed;
    setTransform(Transform::rotateAroundAxis({0, 1, 0}, baseYaw + angle) *
                 Transform::translate(hingePos));
}

void Door::setSlide(const Vec3 &closed, const Vec3 &open, const Mat3 &rotation)
{
    m_motion = DoorMotion::Slide;
    m_slideClosed = closed;
    m_slideOpen = open;
    m_slideRotation = rotation;
    m_slideT = 0.0f;
    m_slideTarget = 0.0f;
    m_state = DoorState::Closed;
    setTransform({closed, rotation});
}

void Door::setIdentity(const QString &roomId, const QString &markerId)
{
    m_roomId = roomId;
    m_markerId = markerId;
}

void Door::setLock(const QString &type, const QString &id, const QString &label,
                   const QString &directRoom)
{
    m_lockType = type;
    m_lockId = id;
    m_lockLabel = label;
    m_lockDirectRoom = directRoom;
    m_autoCloseRemaining = 0.0f;
    if (m_motion == DoorMotion::Slide) {
        m_slideT = 0.0f;
        m_slideTarget = 0.0f;
        setTransform({m_slideClosed, m_slideRotation});
    } else {
        target = closedAngle;
    }
    m_state = type.isEmpty() ? DoorState::Closed : DoorState::Locked;
    m_interactionState = type.isEmpty() ? 1 : 0;
    emit stateChanged();
}

QString Door::lockDisplayName() const
{
    if (!m_lockLabel.isEmpty()) return m_lockLabel;
    if (!m_lockId.isEmpty()) return m_lockId;
    return QStringLiteral("机关");
}

void Door::unlock()
{
    m_interactionState = 1;
    if (m_state == DoorState::Locked)
        m_state = DoorState::Closed;
    m_autoCloseRemaining = 0.0f;
    emit stateChanged();
}

void Door::lock()
{
    m_interactionState = 0;
    m_autoCloseRemaining = 0.0f;
    if (m_motion == DoorMotion::Slide) {
        m_slideT = 0.0f;
        m_slideTarget = 0.0f;
        setTransform({m_slideClosed, m_slideRotation});
    } else {
        target = closedAngle;
    }
    m_state = DoorState::Locked;
    emit stateChanged();
}

void Door::open()
{
    if (m_state == DoorState::Locked)
        return;
    m_autoCloseRemaining = m_openDuration;
    if (m_motion == DoorMotion::Slide)
        m_slideTarget = 1.0f;
    else
        target = openAngle;
    m_state = DoorState::Open;
    emit stateChanged();
}

void Door::openTemporarily(float seconds)
{
    if (m_state == DoorState::Locked)
        return;
    open();
    m_autoCloseRemaining = std::max(seconds, 0.0f);
    if (m_autoCloseRemaining <= 0.0f)
        close();
}

void Door::close()
{
    m_autoCloseRemaining = 0.0f;
    if (m_motion == DoorMotion::Slide)
        m_slideTarget = 0.0f;
    else
        target = closedAngle;
    if (m_state == DoorState::Open)
        m_state = DoorState::Closed;
    emit stateChanged();
}

void Door::toggle()
{
    if (m_state == DoorState::Locked)
        return;
    if (m_state == DoorState::Open)
        close();
    else
        open();
}

void Door::tick(float dt)
{
    // 电梯门的临时开门状态：到点自动关上。
    if (m_autoCloseRemaining > 0.0f && m_state == DoorState::Open) {
        m_autoCloseRemaining -= dt;
        if (m_autoCloseRemaining <= 0.0f) {
            m_autoCloseRemaining = 0.0f;
            qDebug() << "door auto-close:" << m_roomId << m_markerId;
            close();
        }
    }

    if (m_motion == DoorMotion::Slide) {
        if (std::abs(m_slideTarget - m_slideT) < 1e-4f)
            return;
        const float step = speed * dt;
        if (m_slideT < m_slideTarget)
            m_slideT = std::min(m_slideTarget, m_slideT + step);
        else
            m_slideT = std::max(m_slideTarget, m_slideT - step);

        const Vec3 pos = m_slideClosed + (m_slideOpen - m_slideClosed) * m_slideT;
        setTransform({pos, m_slideRotation});
        return;
    }

    if (std::abs(target - angle) < 1e-4f)
        return;

    float step = speed * dt;
    if (angle < target)
        angle = std::min(target, angle + step);
    else
        angle = std::max(target, angle - step);

    setTransform(Transform::rotateAroundAxis({0, 1, 0}, baseYaw + angle) *
                 Transform::translate(hingePos));
}

void Door::setColliderVolume(const Transform &worldTransform, const Vec3 &halfSize)
{
    m_hasCollider = true;
    m_colliderTransform = worldTransform;
    m_colliderHalf = halfSize;
}
