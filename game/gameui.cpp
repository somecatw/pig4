#include "gameui.h"
#include "itemdrag.h"

#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

DialogueBar::DialogueBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("dialogueBar");
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setStyleSheet(
        "QWidget#dialogueBar {"
        "  background-color: rgba(8, 8, 11, 205);"
        "  border-top: 1px solid rgba(255, 255, 255, 28);"
        "}"
        "QLabel { background: transparent; }");

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(20, 10, 20, 10);
    layout->setSpacing(8);

    m_speaker = new QLabel(this);
    m_speaker->setTextFormat(Qt::RichText);
    m_speaker->setStyleSheet("color: #ffd15c; font-size: 14px; font-weight: 700;");
    m_speaker->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    m_speaker->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_text = new QLabel(this);
    // 原版文本里会有颜色标签（例如上锁提示里的绿色钥匙名）。
    m_text->setTextFormat(Qt::RichText);
    m_text->setStyleSheet("color: #f1f1f3; font-size: 14px;");
    m_text->setWordWrap(true);
    m_text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_advance = new QLabel(QStringLiteral("▼"), this);
    m_advance->setStyleSheet("color: #ffd15c; font-size: 12px;");
    m_advance->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    m_advance->hide();

    layout->addWidget(m_speaker, 0, Qt::AlignVCenter);
    layout->addWidget(m_text, 1);
    layout->addWidget(m_advance, 0, Qt::AlignVCenter);

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, &DialogueBar::clearLine);

    hide();
}

void DialogueBar::showLine(const QString &speaker, const QString &text, int durationMs)
{
    if (text.isEmpty()) {
        clearLine();
        return;
    }

    m_speaker->setText(speaker.isEmpty() ? QString() : speaker + QStringLiteral("："));
    m_speaker->setVisible(!speaker.isEmpty());
    m_text->setText(text);

    m_waiting = durationMs <= 0;
    m_advance->setVisible(m_waiting);

    m_hideTimer->stop();
    if (durationMs > 0)
        m_hideTimer->start(durationMs);

    show();
    raise();
    updateGeometry();
}

void DialogueBar::showNotice(const QString &text, int durationMs)
{
    showLine(QString(), text, durationMs);
}

void DialogueBar::clearLine()
{
    m_hideTimer->stop();
    m_waiting = false;
    m_text->clear();
    m_speaker->clear();
    m_speaker->hide();
    m_advance->hide();
    hide();
}

bool DialogueBar::advance()
{
    if (!isVisible())
        return false;
    if (!m_waiting)
        return false;
    clearLine();
    return true;
}

DebugOverlay::DebugOverlay(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("debugOverlay");
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);

    setStyleSheet(
        "QWidget#debugOverlay {"
        "  background-color: rgba(0, 0, 0, 165);"
        "  border: 1px solid rgba(255, 255, 255, 22);"
        "}"
        "QLabel {"
        "  color: #f4f4f4;"
        "  background: transparent;"
        "  font-family: 'Consolas', 'Courier New', monospace;"
        "  font-size: 10px;"
        "}");

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 7, 12, 7);
    layout->setSpacing(26);

    m_left = new QLabel(this);
    m_left->setTextFormat(Qt::PlainText);
    m_left->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_left->setTextInteractionFlags(Qt::NoTextInteraction);

    m_right = new QLabel(this);
    m_right->setTextFormat(Qt::PlainText);
    m_right->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_right->setTextInteractionFlags(Qt::NoTextInteraction);

    layout->addWidget(m_left, 0, Qt::AlignTop);
    layout->addWidget(m_right, 0, Qt::AlignTop);

    hide();
}

void DebugOverlay::setColumns(const QString &left, const QString &right)
{
    m_left->setText(left);
    m_right->setText(right);
    layout()->activate();
    adjustSize();
}

GameSidebar::GameSidebar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("gameSidebar");
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(216);
    setStyleSheet(
        "QWidget#gameSidebar { background-color: #151518; }"
        "QWidget#miniMap { background-color: #0b0b0e; border: 1px solid #2e2e35; }"
        "QLabel#mapLabel { color: #4b4b55; font-size: 10px; letter-spacing: 2px; }"
        "QLabel#pageTab { background-color: #2b2b31; color: #a8a8b0;"
        "  font-size: 11px; border: 1px solid #202025; }"
        "QLabel#pageTab[pageCurrent=\"true\"] { background-color: #4a4a52; color: #ffffff; }"
        "QLabel#itemName { color: #e6e6ec; font-size: 11px; }"
        "QLabel#invHint { color: #6f6f78; font-size: 10px; }"
        "QToolButton[invSlot=\"true\"] {"
        "  background-color: #202025; border: 1px solid #34343c; border-radius: 3px;"
        "}"
        "QToolButton[invSlot=\"true\"][selected=\"true\"] {"
        "  background-color: #2b2920; border: 2px solid #ffd15c;"
        "}");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto *map = new QWidget(this);
    map->setObjectName("miniMap");
    map->setAttribute(Qt::WA_StyledBackground, true);
    map->setFixedHeight(96);
    auto *mapLayout = new QVBoxLayout(map);
    mapLayout->setContentsMargins(0, 0, 0, 0);
    auto *mapLabel = new QLabel(QStringLiteral("MAP"), map);
    mapLabel->setObjectName("mapLabel");
    mapLabel->setAlignment(Qt::AlignCenter);
    mapLayout->addWidget(mapLabel);
    root->addWidget(map);

    auto *pageRow = new QWidget(this);
    auto *pageLayout = new QHBoxLayout(pageRow);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(2);
    for (int i = 0; i < PageCount; ++i) {
        m_pageTabs[i] = new QLabel(QString::number(i + 1), pageRow);
        m_pageTabs[i]->setObjectName("pageTab");
        m_pageTabs[i]->setAlignment(Qt::AlignCenter);
        m_pageTabs[i]->setFixedHeight(22);
        pageLayout->addWidget(m_pageTabs[i], 1);
    }
    root->addWidget(pageRow);

    m_itemName = new QLabel(this);
    m_itemName->setObjectName("itemName");
    m_itemName->setAlignment(Qt::AlignCenter);
    m_itemName->setFixedHeight(18);
    root->addWidget(m_itemName);

    auto *gridWidget = new QWidget(this);
    auto *grid = new QGridLayout(gridWidget);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(6);
    grid->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    for (int i = 0; i < SlotCount; ++i) {
        auto *slot = new QToolButton(gridWidget);
        slot->setProperty("invSlot", true);
        slot->setProperty("selected", false);
        slot->setFixedSize(80, 80);
        slot->setIconSize(QSize(72, 72));
        slot->setFocusPolicy(Qt::NoFocus);
        slot->setAutoRaise(false);
        slot->setToolButtonStyle(Qt::ToolButtonIconOnly);
        connect(slot, &QToolButton::clicked, this, [this, i] {
            if (itemIndexForSlot(i) < m_items.size()) {
                setSelection(i);
                emit slotActivated(i);
            }
        });
        slot->installEventFilter(this);
        m_slots[i] = slot;
        grid->addWidget(slot, i / Columns, i % Columns);
    }
    root->addWidget(gridWidget);

    auto *hint = new QLabel(QStringLiteral("Q/E 切换 · 1–5 翻页\nR 查看"), this);
    hint->setObjectName("invHint");
    hint->setTextFormat(Qt::PlainText);
    hint->setWordWrap(true);
    hint->setAlignment(Qt::AlignCenter);
    hint->setFixedHeight(32);
    root->addWidget(hint);

    root->addStretch(1);
    setPage(0);
}

bool GameSidebar::eventFilter(QObject *watched, QEvent *event)
{
    int slot = -1;
    for (int i = 0; i < SlotCount; ++i) {
        if (m_slots[i] == watched) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            const int itemIndex = itemIndexForSlot(slot);
            if (itemIndex >= 0 && itemIndex < m_items.size()) {
                m_dragSlot = slot;
                m_dragStartPos = mouse->position().toPoint();
            } else {
                m_dragSlot = -1;
            }
        }
    } else if (event->type() == QEvent::MouseMove) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (m_dragSlot == slot && (mouse->buttons() & Qt::LeftButton)) {
            const QPoint delta = mouse->position().toPoint() - m_dragStartPos;
            if (delta.manhattanLength() >= QApplication::startDragDistance()) {
                const int itemIndex = itemIndexForSlot(slot);
                if (itemIndex >= 0 && itemIndex < m_items.size()) {
                    const InventoryItem item = m_items[itemIndex];
                    m_dragSlot = -1;
                    if (auto *button = qobject_cast<QToolButton *>(watched))
                        button->setDown(false);
                    // exec 会进入 Qt 的拖放事件循环；游戏渲染由定时器继续驱动。
                    ItemDrag::exec(qobject_cast<QWidget *>(watched), item.id, item.icon);
                    return true;
                }
            }
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        if (m_dragSlot == slot)
            m_dragSlot = -1;
    }

    return QWidget::eventFilter(watched, event);
}

void GameSidebar::refreshSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= SlotCount)
        return;
    QToolButton *slot = m_slots[slotIndex];
    const int itemIndex = itemIndexForSlot(slotIndex);
    if (itemIndex >= 0 && itemIndex < m_items.size()) {
        const InventoryItem &item = m_items[itemIndex];
        slot->setIcon(QIcon(QPixmap::fromImage(item.icon)));
        slot->setToolTip(item.name);
        slot->setAccessibleName(item.name);
    } else {
        slot->setIcon(QIcon());
        slot->setToolTip(QString());
        slot->setAccessibleName(QString());
    }
}

void GameSidebar::refreshPage()
{
    for (int i = 0; i < SlotCount; ++i)
        refreshSlot(i);

    for (int i = 0; i < PageCount; ++i) {
        QLabel *tab = m_pageTabs[i];
        tab->setProperty("pageCurrent", i == m_page);
        tab->style()->unpolish(tab);
        tab->style()->polish(tab);
        tab->update();
    }
}

void GameSidebar::refreshSelectionStyle()
{
    for (int i = 0; i < SlotCount; ++i) {
        m_slots[i]->setProperty("selected", i == m_selection);
        m_slots[i]->style()->unpolish(m_slots[i]);
        m_slots[i]->style()->polish(m_slots[i]);
        m_slots[i]->update();
    }
}

void GameSidebar::refreshSelectionName()
{
    const int itemIndex = itemIndexForSlot(m_selection);
    if (m_selection >= 0 && itemIndex >= 0 && itemIndex < m_items.size())
        m_itemName->setText(m_items[itemIndex].name);
    else
        m_itemName->clear();
}

void GameSidebar::setInventory(const QList<InventoryItem> &items)
{
    m_items = items;
    if (m_selection >= 0 && itemIndexForSlot(m_selection) >= m_items.size())
        m_selection = -1;
    refreshPage();
    refreshSelectionStyle();
    refreshSelectionName();
}

void GameSidebar::setSelection(int localIndex)
{
    if (localIndex < 0 || localIndex >= SlotCount
        || itemIndexForSlot(localIndex) >= m_items.size())
        localIndex = -1;
    m_selection = localIndex;
    refreshSelectionStyle();
    refreshSelectionName();
}

void GameSidebar::setPage(int page)
{
    page = std::clamp(page, 0, PageCount - 1);
    m_page = page;
    m_dragSlot = -1;
    if (m_selection >= 0 && itemIndexForSlot(m_selection) >= m_items.size())
        m_selection = -1;
    refreshPage();
    refreshSelectionStyle();
    refreshSelectionName();
}
