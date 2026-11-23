#include "elevatorpanelview.h"
#include "elevatorart.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// 贴图从上到下的楼层顺序（与 assets/images/elevator_panel_3F.png 对齐）。
const QStringList kElevatorFloorLabels = {
    QStringLiteral("6F"), QStringLiteral("5F"), QStringLiteral("4F"),
    QStringLiteral("3F"), QStringLiteral("2F"), QStringLiteral("1F")
};

// 原图 198x376，放大 1.5 倍后给 2D 面板使用。
class ElevatorPanelCanvas : public QWidget
{
public:
    explicit ElevatorPanelCanvas(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(297, 564);
    }

    QImage image;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        if (!image.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(rect(), image);
        } else {
            painter.fillRect(rect(), QColor(36, 38, 40));
        }
    }
};

} // namespace

ElevatorPanelView::ElevatorPanelView(QWidget *parent)
    : PuzzleView(parent)
{
    setObjectName("elevatorPanelView");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        "QWidget#elevatorPanelView { background-color: rgba(0, 0, 0, 80); }"
        "QToolButton#elevatorFloor {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QToolButton#elevatorFloor:hover {"
        "  background: rgba(255, 255, 255, 24);"
        "  border: 2px solid #e6b862;"
        "}"
        "QToolButton#elevatorFloor:pressed { background: rgba(0, 0, 0, 80); }"
        "QToolButton#elevatorFloor:disabled { background: transparent; }"
        "QToolButton#elevatorOpen {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QToolButton#elevatorOpen:hover {"
        "  background: rgba(255, 255, 255, 24);"
        "  border: 2px solid #e6b862;"
        "}"
        "QToolButton#elevatorOpen:pressed { background: rgba(0, 0, 0, 80); }"
        "QLabel#elevatorFeedback {"
        "  color: #d8c7b6;"
        "  font-size: 12px;"
        "  background: transparent;"
        "}"
        "QToolButton#elevatorClose {"
        "  background: transparent;"
        "  border: none;"
        "  color: #f0ece8;"
        "  font-size: 26px;"
        "}"
        "QToolButton#elevatorClose:hover { color: #ffd15c; }");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addStretch(1);

    auto *panelRow = new QHBoxLayout();
    panelRow->setContentsMargins(0, 0, 0, 0);
    panelRow->addStretch(1);

    auto *panel = new ElevatorPanelCanvas(this);
    m_panel = panel;
    auto *grid = new QGridLayout(panel);
    // 原图按钮区域：x=49..152, y=38..331，行高 43、间隔约 7。
    // 放大 1.5 倍后：左边距 73，上边距 57，按钮 156x64，行距 11。
    // 最下面再放一个 156x42 的开门键（原图 y=338，高 28）。
    grid->setContentsMargins(73, 57, 68, 15);
    grid->setHorizontalSpacing(0);
    grid->setVerticalSpacing(11);
    for (int i = 0; i < 6; ++i) {
        auto *button = new QToolButton(panel);
        button->setObjectName("elevatorFloor");
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFixedSize(156, 64);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QToolButton::clicked, this, [this, i] {
            const QString floorId = kElevatorFloorLabels.at(i);
            if (m_floors.contains(floorId))
                emitFloor(floorId);
        });
        m_buttons[i] = button;
        grid->addWidget(button, i, 0);
    }

    auto *openButton = new QToolButton(panel);
    openButton->setObjectName("elevatorOpen");
    openButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    openButton->setText(QString());
    openButton->setAccessibleName(QStringLiteral("开门"));
    openButton->setToolTip(QStringLiteral("打开电梯门"));
    openButton->setFixedSize(156, 42);
    openButton->setFocusPolicy(Qt::NoFocus);
    openButton->setCursor(Qt::PointingHandCursor);
    connect(openButton, &QToolButton::clicked,
            this, &ElevatorPanelView::emitOpenDoor);
    m_openButton = openButton;
    grid->addWidget(openButton, 6, 0);

    panelRow->addWidget(panel, 0, Qt::AlignCenter);
    panelRow->addStretch(1);
    root->addLayout(panelRow);
    root->addSpacing(2);

    m_feedback = new QLabel(this);
    m_feedback->setObjectName("elevatorFeedback");
    m_feedback->setAlignment(Qt::AlignCenter);
    m_feedback->setFixedHeight(22);
    root->addWidget(m_feedback);
    root->addSpacing(4);

    auto *closeButton = new QToolButton(this);
    closeButton->setObjectName("elevatorClose");
    closeButton->setText(QStringLiteral("▼"));
    closeButton->setToolTip(QStringLiteral("关闭 (Esc)"));
    closeButton->setFixedSize(48, 36);
    closeButton->setFocusPolicy(Qt::NoFocus);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setAutoRaise(true);
    connect(closeButton, &QToolButton::clicked,
            this, &PuzzleView::closeRequested);
    root->addWidget(closeButton, 0, Qt::AlignHCenter);
    root->addStretch(1);

    m_feedbackTimer = new QTimer(this);
    m_feedbackTimer->setSingleShot(true);
    connect(m_feedbackTimer, &QTimer::timeout, this, [this] {
        m_feedback->clear();
    });
}

void ElevatorPanelView::applyEvent(const PuzzleViewEvent &event)
{
    if (event.type == QLatin1String(ElevatorViewEvents::SetPanel)) {
        const QStringList floors = event.payload.value(
            QString::fromLatin1(ElevatorViewEvents::Floors)).toStringList();
        const QString currentFloor = event.payload.value(
            QString::fromLatin1(ElevatorViewEvents::CurrentFloor)).toString();

        if (auto *canvas = static_cast<ElevatorPanelCanvas *>(m_panel)) {
            canvas->image = elevatorPanelImage(floors, currentFloor);
            canvas->update();
        }
        setFloors(floors);
        return;
    }
    if (event.type == QLatin1String(ElevatorViewEvents::Feedback)) {
        const QString text = event.payload.value(
            QString::fromLatin1(ElevatorViewEvents::Text)).toString();
        const int duration = event.payload.value(
            QString::fromLatin1(ElevatorViewEvents::DurationMs), 1200).toInt();
        showFeedback(text, duration);
        return;
    }

    PuzzleView::applyEvent(event);
}

void ElevatorPanelView::emitFloor(const QString &floorId)
{
    emit viewEvent(PuzzleViewEvent{
        QString::fromLatin1(ElevatorViewEvents::Floor),
        {{QString::fromLatin1(ElevatorViewEvents::FloorId), floorId}}
    });
}

void ElevatorPanelView::emitOpenDoor()
{
    emit viewEvent(PuzzleViewEvent{
        QString::fromLatin1(ElevatorViewEvents::OpenDoor), {}
    });
}

void ElevatorPanelView::setFloors(const QStringList &floors)
{
    m_floors = floors;
    for (int i = 0; i < 6; ++i) {
        const QString floorId = kElevatorFloorLabels.at(i);
        const bool reachable = floors.contains(floorId);
        m_buttons[i]->setEnabled(reachable);
        m_buttons[i]->setCursor(reachable ? Qt::PointingHandCursor
                                          : Qt::ArrowCursor);
        m_buttons[i]->setAccessibleName(floorId);
        m_buttons[i]->setToolTip(reachable ? floorId
                                           : floorId + QStringLiteral("（不可达）"));
    }
}

void ElevatorPanelView::showFeedback(const QString &text, int durationMs)
{
    m_feedback->setText(text);
    m_feedbackTimer->stop();
    if (durationMs > 0)
        m_feedbackTimer->start(durationMs);
}

void ElevatorPanelView::activateKey(int number)
{
    if (!isVisible())
        return;
    if (number == 0) {
        emitOpenDoor();
        return;
    }
    if (number < 1 || number > 6)
        return;
    const QString floorId = QString::number(number) + QStringLiteral("F");
    const int index = kElevatorFloorLabels.indexOf(floorId);
    if (index >= 0)
        m_buttons[index]->click();
}

void ElevatorPanelView::keyPressEvent(QKeyEvent *evt)
{
    if (evt->key() == Qt::Key_0 || evt->key() == Qt::Key_O) {
        emitOpenDoor();
        evt->accept();
        return;
    }
    if (evt->key() >= Qt::Key_1 && evt->key() <= Qt::Key_6) {
        const int number = evt->key() - Qt::Key_0;
        const QString floorId = QString::number(number) + QStringLiteral("F");
        if (m_floors.contains(floorId)) {
            emitFloor(floorId);
            evt->accept();
            return;
        }
    }
    PuzzleView::keyPressEvent(evt);
}
