#ifndef ITEMDRAG_H
#define ITEMDRAG_H

#include <QImage>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QMouseEvent;
class QPaintEvent;

// 背包物品的拖放载荷。MIME 里只放 item id，拖放目标自己决定接不接受。
namespace ItemDrag
{
inline constexpr char MimeType[] = "application/x-pig4-item";

QString itemId(const QMimeData *mime);
void setItemId(QMimeData *mime, const QString &id);

// 从 source 发起一次拖拽；返回时拖拽已结束。icon 只用于跟随鼠标的预览图。
void exec(QWidget *source, const QString &id, const QImage &icon);
}

// 透明放置目标。既能做整块面板的“拖到这里”，也能用 hitTest 限制在
// 三角槽等具体形状里。命中时画一圈金色描边。
class ItemDropZone : public QWidget
{
    Q_OBJECT
public:
    explicit ItemDropZone(QWidget *parent = nullptr);

    void setAcceptedIds(const QStringList &ids);
    void setAcceptedId(const QString &id);
    void clearAcceptedIds();
    void setHitTest(std::function<bool(const QPoint &)> hitTest);
    bool highlighted() const { return m_highlight; }

signals:
    void itemDropped(const QString &id);
    // 物品 id 对但没落在命中区域（例如粉色槽的三角形外）。
    void dropMissed(const QString &id);
    void clicked();

protected:
    void dragEnterEvent(QDragEnterEvent *evt) override;
    void dragMoveEvent(QDragMoveEvent *evt) override;
    void dragLeaveEvent(QDragLeaveEvent *evt) override;
    void dropEvent(QDropEvent *evt) override;
    void mousePressEvent(QMouseEvent *evt) override;
    void mouseReleaseEvent(QMouseEvent *evt) override;
    void paintEvent(QPaintEvent *evt) override;

private:
    bool acceptsAt(const QPoint &pos) const;
    bool acceptsId(const QString &id) const;
    void setHighlight(bool on);

    QStringList m_ids;
    std::function<bool(const QPoint &)> m_hitTest;
    bool m_highlight = false;
};

#endif // ITEMDRAG_H
