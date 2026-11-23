#ifndef PASSWORDKEYPADVIEW_H
#define PASSWORDKEYPADVIEW_H

#include "puzzleui.h"

class QKeyEvent;
class QLabel;
class QTimer;

// 密码器 view 的事件表：
//   out: key { label }
//   in:  setInput { text }, feedback { text, durationMs }
// view 只处理显示和输入；密码、清空键语义由 PasswordKeypadPuzzle 负责。
namespace PasswordKeypadViewEvents
{
inline constexpr auto Key = "key";
inline constexpr auto SetInput = "setInput";
inline constexpr auto Feedback = "feedback";
inline constexpr auto Label = "label";
inline constexpr auto Text = "text";
inline constexpr auto DurationMs = "durationMs";
} // namespace PasswordKeypadViewEvents

class PasswordKeypadView : public PuzzleView
{
    Q_OBJECT
public:
    explicit PasswordKeypadView(const QStringList &keys, int columns,
                                const QString &clearKey,
                                const QString &backspaceKey,
                                const QString &submitKey,
                                QWidget *parent = nullptr);

    void setInput(const QString &input);
    void setFeedback(const QString &text, int durationMs = 900);
    // 调试/键盘输入用；大小写不敏感，只接受 keys 中存在的标签。
    void activateKeyLabel(const QString &label);
    QWidget *appearanceWidget() const override { return m_panel; }

public slots:
    void applyEvent(const PuzzleViewEvent &event) override;

protected:
    bool handleContentKey(QKeyEvent *evt) override;

private:
    QWidget *m_panel = nullptr;
    QLabel *m_feedback = nullptr;
    QTimer *m_feedbackTimer = nullptr;
    QStringList m_keys;
    QString m_clearKey;
    QString m_backspaceKey;
    QString m_submitKey;

    void emitKey(const QString &label);
};

#endif // PASSWORDKEYPADVIEW_H
