#include "dialoguepanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace {

// 原版文本里可能有 <font color=...>；普通文本先转义，避免 & / < 被当成标签。
QString richText(const QString &text)
{
    QString result = Qt::mightBeRichText(text) ? text : text.toHtmlEscaped();
    result.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return result;
}

} // namespace

DialoguePanel::DialoguePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("dialoguePanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    // Presentation only. Both buttons and clicks belong to the host.
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);

    setStyleSheet(
        "QWidget#dialoguePanel {"
        "  background-color: rgba(8, 8, 11, 218);"
        "  border: 1px solid rgba(255, 255, 255, 30);"
        "  border-radius: 8px;"
        "}"
        "QLabel { background: transparent; }"
        "QLabel#dialogueSpeaker {"
        "  color: #ffd15c; font-size: 18px; font-weight: 700;"
        "}"
        "QLabel#dialogueText {"
        "  color: #f1f1f3; font-size: 16px;"
        "}"
        "QLabel#dialogueHint {"
        "  color: #8c8c96; font-size: 11px;"
        "}");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(26, 16, 26, 12);
    layout->setSpacing(6);

    m_speaker = new QLabel(this);
    m_speaker->setObjectName(QStringLiteral("dialogueSpeaker"));
    m_speaker->setTextFormat(Qt::PlainText);
    m_speaker->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_speaker->hide();

    m_text = new QLabel(this);
    m_text->setObjectName(QStringLiteral("dialogueText"));
    m_text->setTextFormat(Qt::RichText);
    m_text->setWordWrap(true);
    m_text->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_text->setTextInteractionFlags(Qt::NoTextInteraction);
    m_text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_hint = new QLabel(this);
    m_hint->setObjectName(QStringLiteral("dialogueHint"));
    m_hint->setTextFormat(Qt::PlainText);
    m_hint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_hint->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_hint->hide();

    layout->addWidget(m_speaker);
    layout->addWidget(m_text, 1);

    auto *bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(0, 0, 0, 0);
    bottomRow->addStretch(1);
    bottomRow->addWidget(m_hint, 0, Qt::AlignRight);
    layout->addLayout(bottomRow);
}

void DialoguePanel::setLine(const QString &speaker, const QString &text,
                            const QString &hint)
{
    if (text.isEmpty()) {
        clearLine();
        return;
    }

    if (speaker.isEmpty()) {
        m_speaker->clear();
        m_speaker->hide();
    } else {
        m_speaker->setText(speaker + QStringLiteral("："));
        m_speaker->show();
    }

    m_text->setText(richText(text));

    if (hint.isEmpty()) {
        m_hint->clear();
        m_hint->hide();
    } else {
        m_hint->setText(hint);
        m_hint->show();
    }
    updateGeometry();
}

void DialoguePanel::clearLine()
{
    m_speaker->clear();
    m_speaker->hide();
    m_text->clear();
    m_hint->clear();
    m_hint->hide();
    updateGeometry();
}
