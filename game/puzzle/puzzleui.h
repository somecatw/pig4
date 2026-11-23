#ifndef PUZZLEUI_H
#define PUZZLEUI_H

#include <QImage>
#include <QMetaType>
#include <QVariant>
#include <QWidget>
#include <QString>

#include "overlayui.h"

class QKeyEvent;
class QLabel;
class QTimer;
class QToolButton;

// puzzle view 与 puzzle 之间的通用事件信封。
// type 由具体 view 定义；payload 放该事件需要的字段。
struct PuzzleViewEvent
{
    QString type;
    QVariantMap payload;
};
Q_DECLARE_METATYPE(PuzzleViewEvent)

// 每种 puzzle 的操作界面继承这个类，作为内容挂进 StageOverlay。
// 模态输入拦截和背景由 StageOverlay 统一处理。
class PuzzleView : public OverlayContent
{
    Q_OBJECT
public:
    explicit PuzzleView(QWidget *parent = nullptr);
    ~PuzzleView() override = default;

    virtual void activateKey(int) {}
    // 数字交给 activateKey，Esc 由 OverlayContent 关闭。
    bool handleContentKey(QKeyEvent *evt) override;

    // 2D 操作界面中真正代表 puzzle 外观的那块区域。
    virtual QWidget *appearanceWidget() const;
    QImage renderAppearance() const;
    // 2D 外观变化时调用；MainWindow 会把它同步到 3D sprite 的贴图。
    void syncAppearance();

public slots:
    // puzzle 通过这个入口把状态/提示推给 view；具体 view 自行解释。
    // view 不需要知道发送者，也不需要 include puzzle.h。
    virtual void applyEvent(const PuzzleViewEvent &event) { Q_UNUSED(event); }

signals:
    void appearanceChanged(const QImage &image);
    // view 只发“用户做了什么”的语义事件，不直接调用 puzzle 逻辑。
    void viewEvent(const PuzzleViewEvent &event);
};

// 数字键盘 view 的事件表：
//   out: digit { value }
//   in:  feedback { text, durationMs }
namespace KeypadViewEvents
{
inline constexpr auto Digit = "digit";
inline constexpr auto Feedback = "feedback";
inline constexpr auto Value = "value";
inline constexpr auto Text = "text";
inline constexpr auto DurationMs = "durationMs";
} // namespace KeypadViewEvents

// 当前 303 机关的 3×3 数字键盘视图。
class KeypadPuzzleView : public PuzzleView
{
    Q_OBJECT
public:
    explicit KeypadPuzzleView(QWidget *parent = nullptr);

    void setFeedback(const QString &text, int durationMs = 900);
    void activateKey(int number) override;
    QWidget *appearanceWidget() const override { return m_panel; }

public slots:
    void applyEvent(const PuzzleViewEvent &event) override;

protected:
    void keyPressEvent(QKeyEvent *evt) override;

private:
    QWidget *m_panel = nullptr;
    QLabel *m_feedback = nullptr;
    QToolButton *m_keys[9] = {};
    QTimer *m_feedbackTimer = nullptr;
};

// 粉色碎片槽 view 的事件表：
//   out: take {}, drop { itemId }
//   in:  setState { filled, itemId }, feedback { text, durationMs }
namespace ShardPinkViewEvents
{
inline constexpr auto Take = "take";
inline constexpr auto Drop = "drop";
inline constexpr auto SetState = "setState";
inline constexpr auto Feedback = "feedback";
inline constexpr auto Filled = "filled";
inline constexpr auto ItemId = "itemId";
inline constexpr auto Text = "text";
inline constexpr auto DurationMs = "durationMs";
} // namespace ShardPinkViewEvents

// 粉色碎片槽：一个可点击取走 / 拖入放回的三角凹槽。
class ShardPinkPuzzleView : public PuzzleView
{
    Q_OBJECT
public:
    explicit ShardPinkPuzzleView(QWidget *parent = nullptr);

    QWidget *appearanceWidget() const override { return m_panel; }

public slots:
    void applyEvent(const PuzzleViewEvent &event) override;

private:
    QWidget *m_panel = nullptr;
    QLabel *m_feedback = nullptr;
    QTimer *m_feedbackTimer = nullptr;
    bool m_filled = false;
    QString m_itemId;

    void refresh();
    void showFeedback(const QString &text, int durationMs = 1200);
};

#endif // PUZZLEUI_H
