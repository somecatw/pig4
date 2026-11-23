#ifndef ELEVATORPANELVIEW_H
#define ELEVATORPANELVIEW_H

#include "puzzleui.h"

#include <QStringList>

class QKeyEvent;
class QLabel;
class QTimer;
class QToolButton;

// 电梯选层 view 的事件表：
//   out: floor { floorId }, openDoor { }
//   in:  setPanel { floors, currentFloor }, feedback { text, durationMs }
namespace ElevatorViewEvents
{
inline constexpr auto Floor = "floor";
inline constexpr auto OpenDoor = "openDoor";
inline constexpr auto SetPanel = "setPanel";
inline constexpr auto Feedback = "feedback";
inline constexpr auto FloorId = "floorId";
inline constexpr auto Floors = "floors";
inline constexpr auto CurrentFloor = "currentFloor";
inline constexpr auto Text = "text";
inline constexpr auto DurationMs = "durationMs";
} // namespace ElevatorViewEvents

// 电梯选层面板。楼层按钮和世界里的电梯 panel 共用 elevatorPanelImage()。
class ElevatorPanelView : public PuzzleView
{
    Q_OBJECT
public:
    explicit ElevatorPanelView(QWidget *parent = nullptr);

    void activateKey(int number) override;
    QWidget *appearanceWidget() const override { return m_panel; }

public slots:
    void applyEvent(const PuzzleViewEvent &event) override;

protected:
    void keyPressEvent(QKeyEvent *evt) override;

private:
    QWidget *m_panel = nullptr;
    QLabel *m_feedback = nullptr;
    QToolButton *m_buttons[6] = {};
    QToolButton *m_openButton = nullptr;
    QStringList m_floors; // 可到达楼层；六个按钮始终显示
    QTimer *m_feedbackTimer = nullptr;

    void setFloors(const QStringList &floors);
    void showFeedback(const QString &text, int durationMs = 1200);
    void emitFloor(const QString &floorId);
    void emitOpenDoor();
};

#endif // ELEVATORPANELVIEW_H
