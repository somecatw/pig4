#ifndef OVERLAYUI_H
#define OVERLAYUI_H

#include <QWidget>

class QKeyEvent;
class QMouseEvent;
class QStackedLayout;

// 所有全屏内容 widget 的公共契约：puzzle、道具、剧情都继承它。
// 外壳 StageOverlay 只负责模态输入拦截和背景，不关心内容类型。
class OverlayContent : public QWidget
{
    Q_OBJECT
public:
    explicit OverlayContent(QWidget *parent = nullptr);
    ~OverlayContent() override = default;

    // 返回 true 表示按键已处理。默认 Esc 请求关闭。
    virtual bool handleContentKey(QKeyEvent *evt);
    // 0 表示不画背景；道具查看用半透明黑压暗场景。
    virtual int backdropOpacity() const { return 0; }
    // true 时外壳会暂时拦掉右侧背包的鼠标事件（剧情这类不需要拖道具的界面）。
    // puzzle/道具查看需要从背包拖入，默认 false。
    virtual bool blocksSidebar() const { return false; }

    // StageOverlay 挂上/摘下内容时调用，供内容做焦点、状态同步。
    virtual void overlayActivated() {}
    virtual void overlayDeactivated() {}

signals:
    void closeRequested();
    void finishedRequested();

protected:
    void keyPressEvent(QKeyEvent *evt) override;
};

// 唯一的全屏外壳：覆盖 Stage3D，吞掉鼠标，托管一个内容 widget。
// 内容 widget 的生命周期由 MainWindow 决定：临时 puzzle 关闭后 delete，
// 道具/剧情是常驻 widget，只从外壳里摘下来。
class StageOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit StageOverlay(QWidget *parent = nullptr);

    void setContent(QWidget *content);
    QWidget *takeContent();
    QWidget *content() const { return m_content; }
    bool hasContent() const { return m_content != nullptr; }

signals:
    void closeRequested();

protected:
    void paintEvent(QPaintEvent *evt) override;
    void keyPressEvent(QKeyEvent *evt) override;
    void mousePressEvent(QMouseEvent *evt) override;
    void mouseReleaseEvent(QMouseEvent *evt) override;
    void mouseMoveEvent(QMouseEvent *evt) override;

private:
    QStackedLayout *m_stack = nullptr;
    QWidget *m_content = nullptr;
    int m_backdropOpacity = 0;

    void updateBackdrop();
};

#endif // OVERLAYUI_H
