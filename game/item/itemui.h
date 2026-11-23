#ifndef ITEMUI_H
#define ITEMUI_H

#include <QHash>
#include <QImage>
#include <QList>
#include <QSet>
#include <QString>
#include <QWidget>

#include "gameui.h"
#include "itemcatalog.h"
#include "overlayui.h"

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QKeyEvent;
class QLabel;
class QMimeData;
class QStackedLayout;
class QToolButton;

// 道具内容区。以后月亮积木/纸团各自继承它，自己画图、自己处理触发器；
// 默认实现只显示 ItemDef 当前 view 的静态图片。
class ItemViewContent : public QWidget
{
    Q_OBJECT
public:
    explicit ItemViewContent(QWidget *parent = nullptr);
    ~ItemViewContent() override = default;

    virtual void setItemView(const ItemDef &def, int viewIndex) = 0;
    // 内容自己决定 caption（例如纸团展开后）；为空时外壳用 view 的 caption。
    virtual QString caption() const { return {}; }

signals:
    void captionChanged(const QString &caption);
};

// 道具放大查看界面。R 打开；X/Esc/R 关闭。
// 原版是通过把另一个道具拖到当前道具上完成组合，所以这里不再画组合按钮。
class ItemDetailView : public OverlayContent
{
    Q_OBJECT
public:
    explicit ItemDetailView(QWidget *parent = nullptr);

    void setItem(const ItemDef &def, const QList<InventoryItem> &combineCandidates);
    void clearItem();
    void nextView();
    void previousView();

    // 替换中间的内容 widget；默认是 ItemImageContent。
    void setViewContent(ItemViewContent *content);
    ItemViewContent *viewContent() const { return m_content; }

    // 拖放和无头测试共用；返回 true 表示触发了组合。
    bool handleItemDrop(const QString &otherId);

signals:
    void combineRequested(const QString &otherId);

protected:
    bool handleContentKey(QKeyEvent *evt) override;
    int backdropOpacity() const override { return 175; }
    void dragEnterEvent(QDragEnterEvent *evt) override;
    void dragMoveEvent(QDragMoveEvent *evt) override;
    void dragLeaveEvent(QDragLeaveEvent *evt) override;
    void dropEvent(QDropEvent *evt) override;

private:
    ItemDef m_def;
    int m_viewIndex = 0;
    QSet<QString> m_combinableIds;
    bool m_dropHighlight = false;

    QWidget *m_contentHost = nullptr;
    QStackedLayout *m_contentLayout = nullptr;
    ItemViewContent *m_content = nullptr;
    QHash<QString, ItemViewContent*> m_contentCache;
    QLabel *m_caption = nullptr;
    QToolButton *m_prev = nullptr;
    QToolButton *m_next = nullptr;

    ItemViewContent *contentForItem(const ItemDef &def);
    void refresh();
    bool isCompatibleDrag(const QMimeData *mime) const;
    bool overImage(const QPoint &pos) const;
    void setDropHighlight(bool on);
};

#endif // ITEMUI_H
