#include "storyui.h"
#include "dialoguepanel.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

StoryView::StoryView(QWidget *parent)
    : OverlayContent(parent)
{
    setObjectName(QStringLiteral("storyView"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        "QWidget#storyView { background: transparent; }"
        "QPushButton#storyFastForward {"
        "  background-color: rgba(18, 18, 22, 200);"
        "  color: #e8e8ee;"
        "  border: 1px solid rgba(255, 255, 255, 70);"
        "  border-radius: 4px;"
        "  padding: 6px 16px;"
        "  font-size: 13px;"
        "}"
        "QPushButton#storyFastForward:hover { background-color: rgba(46, 46, 54, 230); }"
        "QPushButton#storyFastForward:pressed { background-color: rgba(70, 70, 80, 240); }");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(48, 24, 48, 30);
    root->setSpacing(0);

    auto *topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->addStretch(1);

    m_fastForwardButton = new QPushButton(QStringLiteral("快进"), this);
    m_fastForwardButton->setObjectName(QStringLiteral("storyFastForward"));
    m_fastForwardButton->setCursor(Qt::PointingHandCursor);
    // 不抢 StoryView 的键盘焦点，点击后仍然可以用空格继续（如果需要）。
    m_fastForwardButton->setFocusPolicy(Qt::NoFocus);
    m_fastForwardButton->setToolTip(QStringLiteral("执行剩余剧情并快进（Esc）"));
    connect(m_fastForwardButton, &QPushButton::clicked,
            this, &StoryView::fastForward);
    topRow->addWidget(m_fastForwardButton, 0, Qt::AlignBottom | Qt::AlignRight);
    root->addLayout(topRow);
    // root->addStretch(1);

    m_panel = new DialoguePanel(this);
    m_panel->setFixedHeight(150);
    m_panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    root->addWidget(m_panel);
}

void StoryView::setScript(const StoryScript &script)
{
    m_steps = script.steps;
    m_storyKey = script.key;
    m_index = -1;
    m_finished = false;
    m_panel->clearLine();

    // 第一行之前的命令也要执行；如果没有文本，直接结束。
    if (!advanceToNextLine()) {
        m_finished = true;
        return;
    }
    showCurrentLine();
}

void StoryView::clearScript()
{
    m_steps.clear();
    m_storyKey.clear();
    m_index = -1;
    m_finished = false;
    m_panel->clearLine();
}

bool StoryView::advanceToNextLine()
{
    while (++m_index < m_steps.size()) {
        const StoryStep &step = m_steps.at(m_index);
        if (step.kind == StoryStep::Line)
            return true;
        if (step.kind == StoryStep::Command && !step.command.name.isEmpty())
            emit commandRequested(step.command, m_storyKey);
    }
    return false;
}

bool StoryView::nextLineExists() const
{
    for (int i = m_index + 1; i < m_steps.size(); ++i) {
        if (m_steps.at(i).kind == StoryStep::Line)
            return true;
    }
    return false;
}

void StoryView::showCurrentLine()
{
    if (m_index < 0 || m_index >= m_steps.size())
        return;
    const StoryStep &step = m_steps.at(m_index);
    if (step.kind != StoryStep::Line)
        return;

    m_panel->setLine(
        step.line.speaker, step.line.text,
        nextLineExists() ? QStringLiteral("点击 / 空格 / F 继续")
                         : QStringLiteral("点击 / 空格 / F 结束"));
}

void StoryView::advance()
{
    if (m_finished || m_index < 0)
        return;
    if (!advanceToNextLine()) {
        finishStory();
        return;
    }
    showCurrentLine();
}

void StoryView::fastForward()
{
    if (m_finished)
        return;

    // 快进不是跳过：所有剩余命令仍然按原顺序执行，只是不再逐行显示文本。
    while (++m_index < m_steps.size()) {
        const StoryStep &step = m_steps.at(m_index);
        if (step.kind == StoryStep::Command && !step.command.name.isEmpty())
            emit commandRequested(step.command, m_storyKey);
    }
    finishStory();
}

void StoryView::finishStory()
{
    if (m_finished)
        return;
    m_finished = true;
    emit storyFinished();
}

bool StoryView::handleContentKey(QKeyEvent *evt)
{
    switch (evt->key()) {
    case Qt::Key_Escape:
        if (!evt->isAutoRepeat())
            fastForward();
        return true;
    case Qt::Key_Space:
    case Qt::Key_F:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!evt->isAutoRepeat())
            advance();
        return true;
    default:
        break;
    }
    return OverlayContent::handleContentKey(evt);
}

void StoryView::mousePressEvent(QMouseEvent *evt)
{
    if (evt->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        advance();
    }
    evt->accept();
}
