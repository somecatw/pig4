#ifndef GAMEUI_H
#define GAMEUI_H

#include <QImage>
#include <QList>
#include <QPoint>
#include <QString>
#include <QWidget>

class DialoguePanel;
class QEvent;
class QLabel;
class QTimer;
class QToolButton;

struct InventoryItem {
    QString id;
    QString name;
    QImage icon;
};

// 底部剧情/提示条。speaker 为空时就是旁白，不占说话人位置。
// durationMs <= 0 表示等待玩家按 F/Space 推进。
class DialogueBar : public QWidget
{
    Q_OBJECT
public:
    explicit DialogueBar(QWidget *parent = nullptr);

    void showLine(const QString &speaker, const QString &text, int durationMs = 0);
    void showNotice(const QString &text, int durationMs = 2200);
    void clearLine();

    bool awaitingAdvance() const { return m_waiting; }
    bool advance();

private:
    DialoguePanel *m_panel = nullptr;
    QTimer *m_hideTimer = nullptr;
    bool m_waiting = false;
};

// Minecraft F3 风调试叠层：两列等宽字的只读信息，不接收鼠标。
// 内容由 MainWindow 每帧/定时填充，这里只负责排版和样式。
class DebugOverlay : public QWidget
{
public:
    explicit DebugOverlay(QWidget *parent = nullptr);

    void setColumns(const QString &left, const QString &right);

private:
    QLabel *m_left = nullptr;
    QLabel *m_right = nullptr;
};

// 右侧常驻栏：小地图占位、背包页签、2×6 物品格。
// 原版的 1–5 是背包页，不是楼层；每页 12 格，总共 60 格。
class GameSidebar : public QWidget
{
    Q_OBJECT
public:
    static constexpr int SlotsPerPage = 12;
    static constexpr int PageCount = 5;

    explicit GameSidebar(QWidget *parent = nullptr);

    // items 是全部物品；显示当前页对应的 12 个。
    void setInventory(const QList<InventoryItem> &items);
    // 当前页内的槽位（0..11），不是全局物品下标。
    void setSelection(int localIndex);
    void setPage(int page);
    int page() const { return m_page; }

signals:
    void slotActivated(int localIndex);

private:
    static constexpr int SlotCount = SlotsPerPage;
    static constexpr int Columns = 2;

    QToolButton *m_slots[SlotCount] = {};
    QLabel *m_itemName = nullptr;
    QLabel *m_pageTabs[PageCount] = {};
    QList<InventoryItem> m_items;
    int m_page = 0;
    int m_selection = -1;
    QPoint m_dragStartPos;
    int m_dragSlot = -1;

    bool eventFilter(QObject *watched, QEvent *event) override;
    int itemIndexForSlot(int slot) const { return m_page * SlotCount + slot; }
    void refreshPage();
    void refreshSlot(int slot);
    void refreshSelectionStyle();
    void refreshSelectionName();
};

#endif // GAMEUI_H
