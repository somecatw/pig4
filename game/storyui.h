#ifndef STORYUI_H
#define STORYUI_H

#include "overlayui.h"
#include "storycatalog.h"

class DialoguePanel;
class QPushButton;

// 第一版剧情界面：不运镜，只把 3D 场景留在原地，底部显示说话人和文本。
// 点击/空格/F/回车推进；右上角“快进”会执行剩余的剧情命令后结束。
class StoryView : public OverlayContent
{
    Q_OBJECT
public:
    explicit StoryView(QWidget *parent = nullptr);

    void setScript(const StoryScript &script);
    void clearScript();

    bool isPlaying() const { return m_index >= 0 && !m_finished; }

public slots:
    void advance();
    void fastForward();

signals:
    void storyFinished();
    // 命令在正常推进或快进经过该步骤时触发；未注册的命令由接收方 pass。
    void commandRequested(const StoryCommand &command, const QString &storyKey);

protected:
    bool handleContentKey(QKeyEvent *evt) override;
    bool blocksSidebar() const override { return true; }
    void mousePressEvent(QMouseEvent *evt) override;

private:
    DialoguePanel *m_panel = nullptr;
    QPushButton *m_fastForwardButton = nullptr;

    QList<StoryStep> m_steps;
    QString m_storyKey;
    int m_index = -1;
    bool m_finished = false;

    bool advanceToNextLine();
    bool nextLineExists() const;
    void showCurrentLine();
    void finishStory();
};

#endif // STORYUI_H
