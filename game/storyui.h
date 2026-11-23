#ifndef STORYUI_H
#define STORYUI_H

#include "overlayui.h"
#include "storycatalog.h"

class QLabel;
class QPushButton;

// 第一版剧情界面：不运镜，只把 3D 场景留在原地，底部显示说话人和文本。
// 点击/空格/F/回车推进；右上角“跳过”直接结束整段剧情。
class StoryView : public OverlayContent
{
    Q_OBJECT
public:
    explicit StoryView(QWidget *parent = nullptr);

    void setScript(const StoryScript &script);
    void clearScript();

    bool isPlaying() const { return m_index >= 0 && !m_finished; }
    int lineIndex() const { return m_index; }
    int lineCount() const { return int(m_lines.size()); }

public slots:
    void advance();
    void skip();

signals:
    void storyFinished();
    void skipRequested();

protected:
    bool handleContentKey(QKeyEvent *evt) override;
    bool blocksSidebar() const override { return true; }
    void mousePressEvent(QMouseEvent *evt) override;

private:
    QLabel *m_speaker = nullptr;
    QLabel *m_text = nullptr;
    QLabel *m_hint = nullptr;
    QPushButton *m_skipButton = nullptr;

    QList<StoryLine> m_lines;
    int m_index = -1;
    bool m_finished = false;

    void showCurrentLine();
    void finishStory();
};

#endif // STORYUI_H
