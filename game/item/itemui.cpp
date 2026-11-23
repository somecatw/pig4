#include "itemui.h"
#include "itemdrag.h"
#include "itemicons.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QStackedLayout>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

ItemViewContent::ItemViewContent(QWidget *parent)
    : QWidget(parent)
{
}

namespace {

// 默认内容：只画当前 ItemDef view 的静态图片。
class ItemImageContent : public ItemViewContent
{
public:
    explicit ItemImageContent(QWidget *parent = nullptr)
        : ItemViewContent(parent)
    {
        setFixedSize(420, 420);
        // 拖放和点击继续由 ItemDetailView 处理。
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    void setItemView(const ItemDef &def, int viewIndex) override
    {
        m_image = (viewIndex >= 0 && viewIndex < def.views.size())
            ? def.views[viewIndex].image : QImage();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_image.isNull())
            return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QImage scaled = m_image.scaled(size(), Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
        painter.drawImage(QPoint((width() - scaled.width()) / 2,
                                 (height() - scaled.height()) / 2), scaled);
    }

private:
    QImage m_image;
};

QRectF imageDrawRect(const QImage &image, const QSize &widgetSize)
{
    if (image.isNull() || widgetSize.isEmpty())
        return {};
    const QSize scaled = image.size().scaled(widgetSize, Qt::KeepAspectRatio);
    return QRectF((widgetSize.width() - scaled.width()) / 2.0,
                  (widgetSize.height() - scaled.height()) / 2.0,
                  scaled.width(), scaled.height());
}

// 纸团：初始 fold，点一下展开；展开后叠上沾水前的红色图案。
class PaperItemContent : public ItemViewContent
{
public:
    explicit PaperItemContent(QWidget *parent = nullptr)
        : ItemViewContent(parent)
    {
        setFixedSize(420, 420);
        setCursor(Qt::PointingHandCursor);
    }

    void setItemView(const ItemDef &, int) override
    {
        ensureImages();
        update();
    }

    QString caption() const override
    {
        return m_unfolded ? QStringLiteral("展开了纸团。")
                          : QStringLiteral("一个纸团。");
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        ensureImages();
        const QImage &image = m_unfolded ? m_unfoldedWithClue : m_fold;
        if (image.isNull())
            return;

        const QRectF target = imageDrawRect(image, size());
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(target, image);
    }

    void mousePressEvent(QMouseEvent *evt) override
    {
        if (evt->button() == Qt::LeftButton && !m_unfolded) {
            m_unfolded = true;
            update();
            emit captionChanged(caption());
        }
        evt->accept();
    }

private:
    bool m_loaded = false;
    bool m_unfolded = false;
    QImage m_fold;
    QImage m_unfoldedWithClue;

    void ensureImages()
    {
        if (m_loaded)
            return;
        m_loaded = true;

        m_fold = ItemIcons::imageIcon(QStringLiteral("paper_fold.png"));
        QImage unfolded = ItemIcons::imageIcon(QStringLiteral("paper_unfold.png"));
        if (unfolded.isNull())
            return;

        m_unfoldedWithClue = unfolded.convertToFormat(QImage::Format_ARGB32);
        QPainter painter(&m_unfoldedWithClue);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        QFont font(QStringLiteral("Segoe Print"));
        font.setPixelSize(34);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor(193, 32, 32));
        painter.drawText(QRectF(35, 80, 484, 75), Qt::AlignCenter,
                         QStringLiteral("a=1      b=2      ..."));
        painter.drawText(QRectF(25, 215, 504, 75), Qt::AlignCenter,
                         QStringLiteral("18    05    20    01    23"));
    }
};

// 月亮积木：正面点一下翻到背面，背面再点一下翻回来。
class MoonBlockItemContent : public ItemViewContent
{
public:
    explicit MoonBlockItemContent(QWidget *parent = nullptr)
        : ItemViewContent(parent)
    {
        setFixedSize(420, 420);
        setCursor(Qt::PointingHandCursor);
    }

    void setItemView(const ItemDef &, int) override
    {
        ensureImages();
        update();
    }

    QString caption() const override
    {
        return m_back ? QStringLiteral("积木背面还有一串数字。")
                      : QStringLiteral("一块月亮形状的积木。");
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        ensureImages();
        const QImage &image = m_back ? m_backImage : m_frontImage;
        if (image.isNull())
            return;

        const QRectF target = imageDrawRect(image, size());
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(target, image);
    }

    void mousePressEvent(QMouseEvent *evt) override
    {
        if (evt->button() == Qt::LeftButton) {
            const QImage &image = m_back ? m_backImage : m_frontImage;
            const QRectF target = imageDrawRect(image, size());
            if (!image.isNull() && target.contains(evt->position())) {
                const qreal u = (evt->position().x() - target.x()) / target.width();
                const qreal v = (evt->position().y() - target.y()) / target.height();
                const int ix = qBound(0, int(u * image.width()), image.width() - 1);
                const int iy = qBound(0, int(v * image.height()), image.height() - 1);
                if (qAlpha(image.pixel(ix, iy)) > 20) {
                    m_back = !m_back;
                    update();
                    emit captionChanged(caption());
                }
            }
        }
        evt->accept();
    }

private:
    bool m_loaded = false;
    bool m_back = false;
    QImage m_frontImage;
    QImage m_backImage;

    void ensureImages()
    {
        if (m_loaded)
            return;
        m_loaded = true;
        m_frontImage = ItemIcons::imageIcon(QStringLiteral("moon.png"));
        m_backImage = ItemIcons::imageIcon(QStringLiteral("moon_block_back.png"));
    }
};

} // namespace

ItemDetailView::ItemDetailView(QWidget *parent)
    : OverlayContent(parent)
{
    setObjectName("itemDetailView");
    setAttribute(Qt::WA_StyledBackground, true);
    setAcceptDrops(true);
    setStyleSheet(
        "QLabel#itemCaption {"
        "  color: #f1f1f3;"
        "  font-size: 17px;"
        "  background: transparent;"
        "}"
        "QToolButton#itemArrow {"
        "  background: transparent;"
        "  border: none;"
        "  color: #b8b8b8;"
        "  font-size: 38px;"
        "}"
        "QToolButton#itemArrow:hover { color: #ffffff; }"
        "QToolButton#itemClose {"
        "  background: transparent;"
        "  border: 1px solid rgba(255, 255, 255, 80);"
        "  color: #d8d8d8;"
        "  font-size: 24px;"
        "}"
        "QToolButton#itemClose:hover { color: #ffffff; border-color: #ffffff; }");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 14, 24, 18);
    root->setSpacing(0);

    auto *topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->addStretch(1);
    auto *closeButton = new QToolButton(this);
    closeButton->setObjectName("itemClose");
    closeButton->setText(QStringLiteral("✕"));
    closeButton->setToolTip(QStringLiteral("关闭 (Esc / R)"));
    closeButton->setFixedSize(44, 44);
    closeButton->setFocusPolicy(Qt::NoFocus);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setAutoRaise(true);
    connect(closeButton, &QToolButton::clicked, this, &ItemDetailView::closeRequested);
    topRow->addWidget(closeButton, 0, Qt::AlignTop);
    root->addLayout(topRow);

    root->addStretch(1);

    auto *centerRow = new QHBoxLayout();
    centerRow->setContentsMargins(0, 0, 0, 0);
    centerRow->setSpacing(18);
    centerRow->addStretch(1);

    m_prev = new QToolButton(this);
    m_prev->setObjectName("itemArrow");
    m_prev->setText(QStringLiteral("◀"));
    m_prev->setToolTip(QStringLiteral("上一面"));
    m_prev->setFixedSize(58, 90);
    m_prev->setFocusPolicy(Qt::NoFocus);
    m_prev->setCursor(Qt::PointingHandCursor);
    m_prev->setAutoRaise(true);
    connect(m_prev, &QToolButton::clicked, this, &ItemDetailView::previousView);
    centerRow->addWidget(m_prev, 0, Qt::AlignVCenter);

    m_contentHost = new QWidget(this);
    m_contentLayout = new QStackedLayout(m_contentHost);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_content = new ItemImageContent(m_contentHost);
    m_contentLayout->addWidget(m_content);
    m_contentLayout->setCurrentWidget(m_content);
    m_contentCache.insert(QStringLiteral("image"), m_content);
    centerRow->addWidget(m_contentHost, 0, Qt::AlignCenter);

    m_next = new QToolButton(this);
    m_next->setObjectName("itemArrow");
    m_next->setText(QStringLiteral("▶"));
    m_next->setToolTip(QStringLiteral("下一面"));
    m_next->setFixedSize(58, 90);
    m_next->setFocusPolicy(Qt::NoFocus);
    m_next->setCursor(Qt::PointingHandCursor);
    m_next->setAutoRaise(true);
    connect(m_next, &QToolButton::clicked, this, &ItemDetailView::nextView);
    centerRow->addWidget(m_next, 0, Qt::AlignVCenter);

    centerRow->addStretch(1);
    root->addLayout(centerRow);

    root->addStretch(2);

    m_caption = new QLabel(this);
    m_caption->setObjectName("itemCaption");
    m_caption->setTextFormat(Qt::RichText);
    m_caption->setAlignment(Qt::AlignCenter);
    m_caption->setWordWrap(true);
    m_caption->setMinimumHeight(42);
    m_caption->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    root->addWidget(m_caption);

    connect(m_content, &ItemViewContent::captionChanged, this,
            [this](const QString &caption) { m_caption->setText(caption); });

    hide();
}

void ItemDetailView::setItem(const ItemDef &def,
                             const QList<InventoryItem> &combineCandidates)
{
    m_def = def;
    m_viewIndex = 0;

    m_combinableIds.clear();
    for (const InventoryItem &candidate : combineCandidates)
        m_combinableIds.insert(candidate.id);
    setDropHighlight(false);

    m_content = contentForItem(def);
    m_contentLayout->setCurrentWidget(m_content);
    refresh();
}

ItemViewContent *ItemDetailView::contentForItem(const ItemDef &def)
{
    const QString key = def.content.isEmpty() ? QStringLiteral("image") : def.id;
    if (ItemViewContent *cached = m_contentCache.value(key, nullptr))
        return cached;

    ItemViewContent *content = nullptr;
    if (def.content == QStringLiteral("paper"))
        content = new PaperItemContent(m_contentHost);
    else if (def.content == QStringLiteral("moon_block"))
        content = new MoonBlockItemContent(m_contentHost);
    else
        content = new ItemImageContent(m_contentHost);

    m_contentLayout->addWidget(content);
    connect(content, &ItemViewContent::captionChanged, this,
            [this](const QString &caption) { m_caption->setText(caption); });
    m_contentCache.insert(key, content);
    return content;
}

void ItemDetailView::clearItem()
{
    m_def = ItemDef();
    m_viewIndex = 0;
    m_combinableIds.clear();
    setDropHighlight(false);
    if (m_content != nullptr)
        m_content->setItemView(m_def, -1);
    m_caption->clear();
}

void ItemDetailView::setViewContent(ItemViewContent *content)
{
    if (content == nullptr || content == m_content)
        return;

    if (m_contentLayout->indexOf(content) < 0) {
        content->setParent(m_contentHost);
        m_contentLayout->addWidget(content);
        connect(content, &ItemViewContent::captionChanged, this,
                [this](const QString &caption) { m_caption->setText(caption); });
    }
    m_content = content;
    m_contentLayout->setCurrentWidget(content);
    refresh();
}

void ItemDetailView::nextView()
{
    if (!m_def.content.isEmpty() || m_def.views.size() <= 1)
        return;
    m_viewIndex = (m_viewIndex + 1) % m_def.views.size();
    refresh();
}

void ItemDetailView::previousView()
{
    if (!m_def.content.isEmpty() || m_def.views.size() <= 1)
        return;
    m_viewIndex = (m_viewIndex - 1 + m_def.views.size()) % m_def.views.size();
    refresh();
}

void ItemDetailView::refresh()
{
    QString caption;
    if (m_content != nullptr) {
        m_content->setItemView(m_def, m_viewIndex);
        caption = m_content->caption();
    }

    if (m_def.views.isEmpty()) {
        m_caption->setText(caption.isEmpty() ? m_def.name : caption);
        m_prev->hide();
        m_next->hide();
        return;
    }

    m_viewIndex = std::clamp(m_viewIndex, 0, int(m_def.views.size()) - 1);
    const ItemViewDef &view = m_def.views.at(m_viewIndex);
    if (caption.isEmpty())
        caption = view.caption.isEmpty() ? m_def.name : view.caption;
    m_caption->setText(caption);

    const bool multiple = m_def.content.isEmpty() && m_def.views.size() > 1;
    m_prev->setVisible(multiple);
    m_next->setVisible(multiple);
}

bool ItemDetailView::handleContentKey(QKeyEvent *evt)
{
    if (evt->key() == Qt::Key_R && evt->isAutoRepeat())
        return true;
    if (evt->key() == Qt::Key_Escape || evt->key() == Qt::Key_R) {
        emit closeRequested();
        return true;
    }
    if (evt->key() == Qt::Key_Left) {
        previousView();
        return true;
    }
    if (evt->key() == Qt::Key_Right) {
        nextView();
        return true;
    }
    return OverlayContent::handleContentKey(evt);
}

bool ItemDetailView::isCompatibleDrag(const QMimeData *mime) const
{
    const QString id = ItemDrag::itemId(mime);
    return !id.isEmpty() && m_combinableIds.contains(id);
}

bool ItemDetailView::overImage(const QPoint &pos) const
{
    return m_contentHost != nullptr && m_contentHost->isVisible()
        && m_contentHost->geometry().contains(pos);
}

void ItemDetailView::setDropHighlight(bool on)
{
    if (m_dropHighlight == on)
        return;
    m_dropHighlight = on;
    m_contentHost->setStyleSheet(on
        ? QStringLiteral("border: 2px solid #ffd15c; background: transparent;")
        : QString());
}

bool ItemDetailView::handleItemDrop(const QString &otherId)
{
    if (otherId.isEmpty() || !m_combinableIds.contains(otherId))
        return false;
    emit combineRequested(otherId);
    return true;
}

void ItemDetailView::dragEnterEvent(QDragEnterEvent *evt)
{
    if (isCompatibleDrag(evt->mimeData())) {
        setDropHighlight(overImage(evt->position().toPoint()));
        evt->setDropAction(Qt::CopyAction);
        evt->accept();
        return;
    }
    evt->ignore();
}

void ItemDetailView::dragMoveEvent(QDragMoveEvent *evt)
{
    if (isCompatibleDrag(evt->mimeData())) {
        setDropHighlight(overImage(evt->position().toPoint()));
        evt->setDropAction(Qt::CopyAction);
        evt->accept();
        return;
    }
    setDropHighlight(false);
    evt->ignore();
}

void ItemDetailView::dragLeaveEvent(QDragLeaveEvent *evt)
{
    setDropHighlight(false);
    evt->accept();
}

void ItemDetailView::dropEvent(QDropEvent *evt)
{
    const QString id = ItemDrag::itemId(evt->mimeData());
    const bool ok = isCompatibleDrag(evt->mimeData())
                 && overImage(evt->position().toPoint());
    setDropHighlight(false);
    if (!ok) {
        evt->ignore();
        return;
    }
    evt->setDropAction(Qt::CopyAction);
    evt->accept();
    handleItemDrop(id);
}
