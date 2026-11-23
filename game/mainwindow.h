#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QImage>
#include <QTimer>
#include "stage3d.h"
#include "roommanager.h"
#include "gameui.h"
#include "itemcatalog.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class Door;
class Hotspot;
class GameSidebar;
class DialogueBar;
class Puzzle;
class StageOverlay;
class OverlayContent;
class PuzzleView;
class ItemDetailView;
class StoryView;
class StoryCatalog;
struct StoryScript;
class DevHooks;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void updateFrame();

protected:
    void keyPressEvent(QKeyEvent *evt) override;
    void keyReleaseEvent(QKeyEvent *evt) override;
    void mousePressEvent(QMouseEvent *evt) override;
    void mouseReleaseEvent(QMouseEvent *evt) override;
    void mouseMoveEvent(QMouseEvent *evt) override;

private:
    friend class DevHooks;

    Ui::MainWindow *ui;
    int t;
    QLabel *fpsLabel;
    QTimer *timer;

    Stage3D *stage;
    Camera *camera;
    GameSidebar *sidebar = nullptr;
    DialogueBar *dialogueBar = nullptr;
    DebugOverlay *debugOverlay = nullptr;
    StageOverlay *stageOverlay = nullptr;
    PuzzleView *activePuzzleView = nullptr;
    Puzzle *activePuzzle = nullptr;
    QMetaObject::Connection activePuzzleSolvedConnection;
    QMetaObject::Connection activeElevatorConnection;
    QMetaObject::Connection activeElevatorOpenConnection;
    ItemDetailView *itemDetailView = nullptr;
    StoryCatalog *storyCatalog = nullptr;
    StoryView *storyView = nullptr;
    ItemCatalog itemCatalog;
    QString activeItemId;
    QLabel *roomLabel = nullptr;
    RoomManager *roomManager = nullptr;
    int hoveredTarget = -1;
    qint64 lastDebugUpdateMs = 0;
    QList<InventoryItem> inventory;
    int inventoryIndex = 0;
    int inventoryPage = 0;
    QString currentRoom;

    // 穿门/翻窗过场：相机沿折线平滑移动，期间暂停移动/碰撞。
    // 路径通常是 当前相机 -> 本侧 end -> 对侧 end。
    bool portalTransitionActive = false;
    qint64 portalTransitionStartMs = 0;
    qint64 portalTransitionDurationMs = 1400;
    QList<Vec3> portalPathPositions;
    QList<Vec3> portalPathLooks;
    QList<float> portalPathPrefix;
    float portalPathLength = 0.0f;
    QString portalDestinationRoom;
    QString portalTransitionId;

    void updateInteraction();
    void updateDebugOverlay();
    // 全屏内容统一入口：冻结/恢复玩家、准星、侧栏，再交给 StageOverlay。
    void showOverlay(OverlayContent *content);
    void hideOverlay(OverlayContent *content);
    void setHoveredTarget(int index);
    void setTargetHighlight(const InteractTarget &target, bool on);
    bool raycastTarget(const InteractTarget &target, const Ray &ray, float &dis) const;
    QString selectedItemId() const;
    void addItem(const QString &id, const QString &name, const QImage &icon);
    void refreshInventoryUi();
    void cycleInventory(int delta);
    void selectInventoryPage(int page);
    void interactHovered();
    void openPuzzle(Puzzle *puzzle);
    void closePuzzle();
    void onActivePuzzleSolved();
    void onElevatorFloorSelected(const QString &floorId);
    void onElevatorOpenDoor();
    void onElevatorCall(const PuzzleDef &def);
    void finishElevatorTravel();
    Door *elevatorDoor(const QString &roomId, const QString &floorId) const;

    // 选层后的 关门 -> 传送 -> 开门 序列。传送时用玩家当前相对当前面板的
    // 位置/朝向映射到目标面板，运行期间不冻结玩家。
    bool elevatorTravelActive = false;
    Transform elevatorTravelSourceWorld;
    Transform elevatorTravelDestinationWorld;
    QString elevatorTravelRoomId;
    QString elevatorTravelFloor;
    void openItemDetail();
    void closeItemDetail();
    // 剧情界面：第一版只播放文本，不运镜、不执行 raw 命令。
    void playStoryRequest(const QString &request);
    void startStory(const StoryScript &script);
    void closeStory(bool skipped);
    void combineItem(const QString &otherId);
    bool removeItem(const QString &id);
    void showGameNotice(const QString &text, int durationMs = 2200);
    void updateRoomUi();
    const PortalDef *usablePortal(const InteractTarget &target) const;
    void startPortalTransition(int targetIndex, const PortalDef &portal);
    bool updatePortalTransition();

    // 迫真 wasd 实现
    Transform camTrans;
    bool wasdFlags[4];
    bool dragFlag;
    bool jumpFlag;
    QPoint startPoint;
    int xacc, yacc;
};

namespace pig4_detail {
// 链接期布局哨兵：main.cpp 和 mainwindow.cpp 必须对 MainWindow 的 sizeof 达成一致。
// 如果某个 .obj 因为头文件依赖丢失而没有重编，会在这里直接链接失败，
// 而不是让 main 的栈帧和构造函数写入位置错位、静默破坏 security cookie。
template<unsigned Size>
struct MainWindowLayoutGuard {
    static void check();
};
}
#endif // MAINWINDOW_H
