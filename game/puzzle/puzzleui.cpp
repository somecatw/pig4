#include "puzzleui.h"
#include "itemdrag.h"
#include "puzzleart.h"
#include "shardart.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// 这张 panel 的背景图就是世界里 sprite 用的同一张 texture。
class KeypadPanel : public QWidget
{
public:
    explicit KeypadPanel(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(356, 356);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.drawImage(rect(), keypadPuzzleImage());
    }
};

} // namespace

PuzzleView::PuzzleView(QWidget *parent)
    : OverlayContent(parent)
{
}

bool PuzzleView::handleContentKey(QKeyEvent *evt)
{
    if (evt->key() >= Qt::Key_0 && evt->key() <= Qt::Key_9) {
        activateKey(evt->key() - Qt::Key_0);
        return true;
    }
    return OverlayContent::handleContentKey(evt);
}

QWidget *PuzzleView::appearanceWidget() const
{
    return const_cast<PuzzleView *>(this);
}

QImage PuzzleView::renderAppearance() const
{
    QWidget *widget = appearanceWidget();
    if (widget == nullptr || widget->size().isEmpty())
        return {};

    if (widget->layout() != nullptr)
        widget->layout()->activate();

    QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    // 不要 DrawWindowBackground：电梯面板画布本身是透明的，默认 render()
    // 会把 palette 的 window 底色（白色）也画进去，同步到 3D 后变成白底。
    widget->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    return image;
}

void PuzzleView::syncAppearance()
{
    emit appearanceChanged(renderAppearance());
}

KeypadPuzzleView::KeypadPuzzleView(QWidget *parent)
    : PuzzleView(parent)
{
    setObjectName("keypadPuzzleView");
    setStyleSheet(
        "QToolButton#puzzleKey {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QToolButton#puzzleKey:hover {"
        "  background: rgba(255, 255, 255, 30);"
        "  border: 2px solid rgba(255, 240, 228, 180);"
        "}"
        "QToolButton#puzzleKey:pressed { background: rgba(0, 0, 0, 80); }"
        "QLabel#puzzleFeedback {"
        "  color: #c9b5a5;"
        "  font-size: 12px;"
        "  background: transparent;"
        "}"
        "QToolButton#puzzleClose {"
        "  background: transparent;"
        "  border: none;"
        "  color: #f0ece8;"
        "  font-size: 26px;"
        "}"
        "QToolButton#puzzleClose:hover { color: #ffd15c; }");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addStretch(1);

    auto *panelRow = new QHBoxLayout();
    panelRow->setContentsMargins(0, 0, 0, 0);
    panelRow->addStretch(1);

    auto *panel = new KeypadPanel(this);
    m_panel = panel;
    auto *grid = new QGridLayout(panel);
    grid->setContentsMargins(18, 18, 18, 18);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);
    for (int i = 0; i < 9; ++i) {
        auto *key = new QToolButton(panel);
        key->setObjectName("puzzleKey");
        key->setText(QString());
        key->setAccessibleName(QString::number(i + 1));
        key->setToolButtonStyle(Qt::ToolButtonTextOnly);
        key->setFixedSize(100, 100);
        key->setFocusPolicy(Qt::NoFocus);
        key->setCursor(Qt::PointingHandCursor);
        connect(key, &QToolButton::clicked, this, [this, i] {
            emit viewEvent(PuzzleViewEvent{
                QString::fromLatin1(KeypadViewEvents::Digit),
                {{QString::fromLatin1(KeypadViewEvents::Value), i + 1}}
            });
        });
        m_keys[i] = key;
        grid->addWidget(key, i / 3, i % 3);
    }

    panelRow->addWidget(panel, 0, Qt::AlignCenter);
    panelRow->addStretch(1);
    root->addLayout(panelRow);
    root->addSpacing(2);

    m_feedback = new QLabel(this);
    m_feedback->setObjectName("puzzleFeedback");
    m_feedback->setAlignment(Qt::AlignCenter);
    m_feedback->setFixedHeight(22);
    root->addWidget(m_feedback);
    root->addSpacing(4);

    auto *closeButton = new QToolButton(this);
    closeButton->setObjectName("puzzleClose");
    closeButton->setText(QStringLiteral("▼"));
    closeButton->setToolTip(QStringLiteral("关闭 (Esc)"));
    closeButton->setFixedSize(48, 36);
    closeButton->setFocusPolicy(Qt::NoFocus);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setAutoRaise(true);
    connect(closeButton, &QToolButton::clicked, this, &PuzzleView::closeRequested);
    root->addWidget(closeButton, 0, Qt::AlignHCenter);
    root->addStretch(1);

    m_feedbackTimer = new QTimer(this);
    m_feedbackTimer->setSingleShot(true);
    connect(m_feedbackTimer, &QTimer::timeout, this, [this] {
        m_feedback->clear();
    });
}

void KeypadPuzzleView::setFeedback(const QString &text, int durationMs)
{
    m_feedback->setText(text);
    m_feedbackTimer->stop();
    if (durationMs > 0)
        m_feedbackTimer->start(durationMs);
}

void KeypadPuzzleView::applyEvent(const PuzzleViewEvent &event)
{
    if (event.type == QLatin1String(KeypadViewEvents::Feedback)) {
        const QString text = event.payload.value(
            QString::fromLatin1(KeypadViewEvents::Text)).toString();
        const int duration = event.payload.value(
            QString::fromLatin1(KeypadViewEvents::DurationMs), 900).toInt();
        setFeedback(text, duration);
        return;
    }

    PuzzleView::applyEvent(event);
}

void KeypadPuzzleView::activateKey(int number)
{
    if (number < 1 || number > 9 || !isVisible())
        return;
    m_keys[number - 1]->click();
}

void KeypadPuzzleView::keyPressEvent(QKeyEvent *evt)
{
    if (evt->key() >= Qt::Key_1 && evt->key() <= Qt::Key_9) {
        activateKey(evt->key() - Qt::Key_0);
        evt->accept();
        return;
    }
    PuzzleView::keyPressEvent(evt);
}

namespace {

class ShardPinkPanel : public ItemDropZone
{
public:
    explicit ShardPinkPanel(QWidget *parent = nullptr)
        : ItemDropZone(parent)
    {
        setFixedSize(356, 356);
    }

    QImage image;

protected:
    void paintEvent(QPaintEvent *evt) override
    {
        QPainter painter(this);
        if (!image.isNull())
            painter.drawImage(rect(), image);
        painter.end();
        // 交给 ItemDropZone 画拖拽高亮。
        ItemDropZone::paintEvent(evt);
    }
};

} // namespace

ShardPinkPuzzleView::ShardPinkPuzzleView(QWidget *parent)
    : PuzzleView(parent)
{
    setObjectName("shardPinkPuzzleView");
    setStyleSheet(
        "QLabel#shardPinkFeedback {"
        "  color: #e8c9ce;"
        "  font-size: 12px;"
        "  background: transparent;"
        "}"
        "QToolButton#shardPinkClose {"
        "  background: transparent;"
        "  border: none;"
        "  color: #f0ece8;"
        "  font-size: 26px;"
        "}"
        "QToolButton#shardPinkClose:hover { color: #ffd15c; }");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addStretch(1);

    auto *panelRow = new QHBoxLayout();
    panelRow->setContentsMargins(0, 0, 0, 0);
    panelRow->addStretch(1);

    auto *panel = new ShardPinkPanel(this);
    m_panel = panel;

    // 拖放只认三角槽（留一点容差），并只接受 shard_pink。
    panel->setHitTest([panel](const QPoint &pos) {
        const QRectF box = ShardArt::pinkPanelTriangleBox(panel->width());
        const QPainterPath path = ShardArt::pinkPath(box);
        return path.contains(pos)
            || path.boundingRect().adjusted(-8, -8, 8, 8).contains(pos);
    });
    connect(panel, &ItemDropZone::itemDropped, this, [this](const QString &id) {
        emit viewEvent(PuzzleViewEvent{
            QString::fromLatin1(ShardPinkViewEvents::Drop),
            {{QString::fromLatin1(ShardPinkViewEvents::ItemId), id}}
        });
    });
    connect(panel, &ItemDropZone::dropMissed, this, [this](const QString &) {
        showFeedback(QStringLiteral("要放到三角凹槽里。"));
    });
    connect(panel, &ItemDropZone::clicked, this, [this] {
        emit viewEvent(PuzzleViewEvent{
            QString::fromLatin1(ShardPinkViewEvents::Take), {}
        });
    });

    panelRow->addWidget(panel, 0, Qt::AlignCenter);
    panelRow->addStretch(1);
    root->addLayout(panelRow);
    root->addSpacing(2);

    m_feedback = new QLabel(this);
    m_feedback->setObjectName("shardPinkFeedback");
    m_feedback->setAlignment(Qt::AlignCenter);
    m_feedback->setFixedHeight(22);
    root->addWidget(m_feedback);
    root->addSpacing(4);

    auto *closeButton = new QToolButton(this);
    closeButton->setObjectName("shardPinkClose");
    closeButton->setText(QStringLiteral("▼"));
    closeButton->setToolTip(QStringLiteral("关闭 (Esc)"));
    closeButton->setFixedSize(48, 36);
    closeButton->setFocusPolicy(Qt::NoFocus);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setAutoRaise(true);
    connect(closeButton, &QToolButton::clicked, this, &OverlayContent::closeRequested);
    root->addWidget(closeButton, 0, Qt::AlignHCenter);
    root->addStretch(1);

    m_feedbackTimer = new QTimer(this);
    m_feedbackTimer->setSingleShot(true);
    connect(m_feedbackTimer, &QTimer::timeout, this, [this] {
        m_feedback->clear();
    });
}

void ShardPinkPuzzleView::applyEvent(const PuzzleViewEvent &event)
{
    if (event.type == QLatin1String(ShardPinkViewEvents::SetState)) {
        m_filled = event.payload.value(
            QString::fromLatin1(ShardPinkViewEvents::Filled)).toBool();
        m_itemId = event.payload.value(
            QString::fromLatin1(ShardPinkViewEvents::ItemId)).toString();
        refresh();
        return;
    }
    if (event.type == QLatin1String(ShardPinkViewEvents::Feedback)) {
        const QString text = event.payload.value(
            QString::fromLatin1(ShardPinkViewEvents::Text)).toString();
        const int duration = event.payload.value(
            QString::fromLatin1(ShardPinkViewEvents::DurationMs), 1200).toInt();
        showFeedback(text, duration);
        return;
    }

    PuzzleView::applyEvent(event);
}

void ShardPinkPuzzleView::refresh()
{
    auto *panel = static_cast<ShardPinkPanel *>(m_panel);
    panel->image = ShardArt::pinkPanelImage(m_filled, panel->width());

    // 槽里有碎片时不能继续拖入；空槽才接受当前 puzzle 的 item id。
    if (m_filled || m_itemId.isEmpty())
        panel->clearAcceptedIds();
    else
        panel->setAcceptedId(m_itemId);
    panel->setToolTip(m_filled ? QStringLiteral("点击取走粉色碎片")
                               : QStringLiteral("把粉色碎片拖回凹槽"));
    panel->update();

    // 2D 是外观的唯一来源：状态变化后同步世界里的 sprite 贴图。
    syncAppearance();
}

void ShardPinkPuzzleView::showFeedback(const QString &text, int durationMs)
{
    m_feedback->setText(text);
    m_feedbackTimer->stop();
    if (durationMs > 0)
        m_feedbackTimer->start(durationMs);
}
