#ifndef DIALOGUEPANEL_H
#define DIALOGUEPANEL_H

#include <QWidget>

class QLabel;

// Shared bottom text presentation for story lines and transient notices.
// This widget never handles input: the host decides whether the text is modal.
// DialogueBar keeps it mouse-transparent; StoryView wraps it in an input layer.
class DialoguePanel : public QWidget
{
public:
    explicit DialoguePanel(QWidget *parent = nullptr);

    // Empty text clears the panel. hint is the optional bottom-right prompt.
    void setLine(const QString &speaker, const QString &text,
                 const QString &hint = QString());
    void clearLine();

private:
    QLabel *m_speaker = nullptr;
    QLabel *m_text = nullptr;
    QLabel *m_hint = nullptr;
};

#endif // DIALOGUEPANEL_H
