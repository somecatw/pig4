#include "storyui.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSizePolicy>
#include <QTextDocument>
#include <QVBoxLayout>

// 原版文本里可能有 <font color=...>，是富文本；普通文本要先转义，
// 否则人名/旁白里的 & 或 < 会被 QLabel 当成标签吃掉。
static QString storyRichText(const QString &text)
{
    QString result = Qt::mightBeRichText(text) ? text : text.toHtmlEscaped();
    result.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return result;
}

StoryView::StoryView(QWidget *parent)
    : OverlayContent(parent)
{
    setObjectName(QStringLiteral("storyView"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        "QWidget#storyView { background: transparent; }"
        "QWidget#storyPanel {"
        "  background-color: rgba(8, 8, 11, 218);"
        "  border: 1px solid rgba(255, 255, 255, 30);"
        "  border-radius: 8px;"
        "}"
        "QLabel#storySpeaker {"
        "  color: #ffd15c; font-size: 18px; font-weight: 700; background: transparent;"
        "}"
        "QLabel#storyText {"
        "  color: #f1f1f3; font-size: 16px; background: transparent;"
        "}"
        "QLabel#storyHint {"
        "  color: #8c8c96; font-size: 11px; background: transparent;"
        "}"
        "QPushButton#storySkip {"
        "  background-color: rgba(18, 18, 22, 200);"
        "  color: #e8e8ee;"
        "  border: 1px solid rgba(255, 255, 255, 70);"
        "  border-radius: 4px;"
        "  padding: 6px 16px;"
        "  font-size: 13px;"
        "}"
        "QPushButton#storySkip:hover { background-color: rgba(46, 46, 54, 230); }"
        "QPushButton#storySkip:pressed { background-color: rgba(70, 70, 80, 240); }");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(48, 24, 48, 30);
    root->setSpacing(0);

    auto *topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->addStretch(1);

    m_skipButton = new QPushButton(QStringLiteral("跳过"), this);
    m_skipButton->setObjectName(QStringLiteral("storySkip"));
    m_skipButton->setCursor(Qt::PointingHandCursor);
    // 不抢 StoryView 的键盘焦点，点击后仍然可以用空格继续（如果需要）。
    m_skipButton->setFocusPolicy(Qt::NoFocus);
    m_skipButton->setToolTip(QStringLiteral("跳过整段剧情（Esc）"));
    connect(m_skipButton, &QPushButton::clicked, this, &StoryView::skip);
    topRow->addWidget(m_skipButton, 0, Qt::AlignTop | Qt::AlignRight);
    root->addLayout(topRow);
    root->addStretch(1);

    auto *panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("storyPanel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    // 面板只显示文本；点击任意位置都交给 StoryView 推进。
    panel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    panel->setFixedHeight(180);
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(26, 16, 26, 12);
    panelLayout->setSpacing(6);

    m_speaker = new QLabel(panel);
    m_speaker->setObjectName(QStringLiteral("storySpeaker"));
    m_speaker->setTextFormat(Qt::PlainText);
    m_speaker->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_speaker->hide();

    m_text = new QLabel(panel);
    m_text->setObjectName(QStringLiteral("storyText"));
    m_text->setTextFormat(Qt::RichText);
    m_text->setWordWrap(true);
    m_text->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_text->setTextInteractionFlags(Qt::NoTextInteraction);
    m_text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_hint = new QLabel(panel);
    m_hint->setObjectName(QStringLiteral("storyHint"));
    m_hint->setTextFormat(Qt::PlainText);
    m_hint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_hint->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    panelLayout->addWidget(m_speaker);
    panelLayout->addWidget(m_text, 1);

    auto *bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(0, 0, 0, 0);
    bottomRow->addStretch(1);
    bottomRow->addWidget(m_hint, 0, Qt::AlignRight);
    panelLayout->addLayout(bottomRow);

    root->addWidget(panel);
}

void StoryView::setScript(const StoryScript &script)
{
    m_lines = script.lines;
    m_finished = false;
    m_index = m_lines.isEmpty() ? -1 : 0;

    if (m_index < 0) {
        m_speaker->clear();
        m_speaker->hide();
        m_text->clear();
        m_hint->clear();
        return;
    }
    showCurrentLine();
}

void StoryView::clearScript()
{
    m_lines.clear();
    m_index = -1;
    m_finished = false;
    m_speaker->clear();
    m_speaker->hide();
    m_text->clear();
    m_hint->clear();
}

void StoryView::showCurrentLine()
{
    if (m_index < 0 || m_index >= m_lines.size())
        return;

    const StoryLine &line = m_lines[m_index];
    if (line.speaker.isEmpty()) {
        m_speaker->clear();
        m_speaker->hide();
    } else {
        m_speaker->setText(line.speaker + QStringLiteral("："));
        m_speaker->show();
    }
    m_text->setText(storyRichText(line.text));

    const bool last = m_index + 1 >= m_lines.size();
    m_hint->setText(last ? QStringLiteral("点击 / 空格 / F 结束")
                         : QStringLiteral("点击 / 空格 / F 继续"));
}

void StoryView::advance()
{
    if (m_finished || m_index < 0)
        return;
    if (m_index + 1 >= m_lines.size()) {
        finishStory();
        return;
    }
    ++m_index;
    showCurrentLine();
}

void StoryView::skip()
{
    if (m_finished)
        return;
    m_finished = true;
    emit skipRequested();
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
            skip();
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
