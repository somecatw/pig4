#include "itemdrag.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>

namespace ItemDrag
{

QString itemId(const QMimeData *mime)
{
    if (mime == nullptr || !mime->hasFormat(MimeType))
        return {};
    return QString::fromUtf8(mime->data(MimeType));
}

void setItemId(QMimeData *mime, const QString &id)
{
    if (mime != nullptr)
        mime->setData(MimeType, id.toUtf8());
}

void exec(QWidget *source, const QString &id, const QImage &icon)
{
    if (source == nullptr || id.isEmpty())
        return;

    auto *mime = new QMimeData();
    setItemId(mime, id);

    auto *drag = new QDrag(source);
    drag->setMimeData(mime);

    QPixmap preview;
    if (!icon.isNull()) {
        preview = QPixmap::fromImage(icon).scaled(
            64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (!preview.isNull()) {
        drag->setPixmap(preview);
        drag->setHotSpot(QPoint(preview.width() / 2, preview.height() / 2));
    }

    drag->exec(Qt::CopyAction);
}

} // namespace ItemDrag

ItemDropZone::ItemDropZone(QWidget *parent)
    : QWidget(parent)
{
    setAcceptDrops(true);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setCursor(Qt::ArrowCursor);
}

void ItemDropZone::setAcceptedIds(const QStringList &ids)
{
    m_ids = ids;
}

void ItemDropZone::setAcceptedId(const QString &id)
{
    m_ids = id.isEmpty() ? QStringList{} : QStringList{id};
}

void ItemDropZone::clearAcceptedIds()
{
    m_ids.clear();
}

void ItemDropZone::setHitTest(std::function<bool(const QPoint &)> hitTest)
{
    m_hitTest = std::move(hitTest);
}

bool ItemDropZone::acceptsId(const QString &id) const
{
    return !id.isEmpty() && m_ids.contains(id);
}

bool ItemDropZone::acceptsAt(const QPoint &pos) const
{
    if (!rect().contains(pos))
        return false;
    return !m_hitTest || m_hitTest(pos);
}

void ItemDropZone::setHighlight(bool on)
{
    if (m_highlight == on)
        return;
    m_highlight = on;
    update();
}

void ItemDropZone::dragEnterEvent(QDragEnterEvent *evt)
{
    // 只要物品 id 对就先接住 enter。光标通常从面板边缘进入，此时还没到
    // 三角槽；如果这里因为 hitTest 失败而 ignore，Qt 会认为整个目标不接收，
    // 之后移到槽上也不会再发 dragMove/drop。
    if (acceptsId(ItemDrag::itemId(evt->mimeData()))) {
        setHighlight(acceptsAt(evt->position().toPoint()));
        evt->setDropAction(Qt::CopyAction);
        evt->accept();
        return;
    }
    setHighlight(false);
    evt->ignore();
}

void ItemDropZone::dragMoveEvent(QDragMoveEvent *evt)
{
    if (acceptsId(ItemDrag::itemId(evt->mimeData()))) {
        setHighlight(acceptsAt(evt->position().toPoint()));
        evt->setDropAction(Qt::CopyAction);
        evt->accept();
        return;
    }
    setHighlight(false);
    evt->ignore();
}

void ItemDropZone::dragLeaveEvent(QDragLeaveEvent *evt)
{
    setHighlight(false);
    evt->accept();
}

void ItemDropZone::dropEvent(QDropEvent *evt)
{
    const QString id = ItemDrag::itemId(evt->mimeData());
    const bool idOk = acceptsId(id);
    const bool areaOk = idOk && acceptsAt(evt->position().toPoint());
    setHighlight(false);
    if (!areaOk) {
        evt->ignore();
        if (idOk)
            emit dropMissed(id);
        return;
    }
    evt->setDropAction(Qt::CopyAction);
    evt->accept();
    emit itemDropped(id);
}

void ItemDropZone::mousePressEvent(QMouseEvent *evt)
{
    // 接住 press，保证 release 会回到这里；否则 QWidget 默认 ignore 后
    // 事件冒泡到父 widget，点击槽位就收不到 clicked。
    if (evt->button() == Qt::LeftButton) {
        evt->accept();
        return;
    }
    QWidget::mousePressEvent(evt);
}

void ItemDropZone::mouseReleaseEvent(QMouseEvent *evt)
{
    if (evt->button() == Qt::LeftButton && rect().contains(evt->position().toPoint())) {
        evt->accept();
        emit clicked();
        return;
    }
    QWidget::mouseReleaseEvent(evt);
}

void ItemDropZone::paintEvent(QPaintEvent *)
{
    if (!m_highlight)
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(QColor(255, 209, 92, 38));
    painter.setPen(QPen(QColor(0xff, 0xd1, 0x5c), 2));
    painter.drawRoundedRect(rect().adjusted(1, 1, -2, -2), 4, 4);
}
