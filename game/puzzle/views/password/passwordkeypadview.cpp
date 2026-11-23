#include "passwordkeypadview.h"
#include "passwordart.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace {

// 字母密码器的可点击面板。绘制和命中检测共用 passwordKeyRect()，
// 因此换行数/键数时不会出现“画在这里、点在别处”。
class PasswordPanel : public QWidget
{
public:
    PasswordPanel(const QStringList &keys, int columns,
                  const QString &clearKey, const QString &backspaceKey,
                  const QString &submitKey, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_keys(keys)
        , m_columns(qMax(1, columns))
        , m_clearKey(clearKey)
        , m_backspaceKey(backspaceKey)
        , m_submitKey(submitKey)
    {
        setFixedSize(passwordPanelSize());
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }

    std::function<void(const QString &)> keyActivated;

    void setInput(const QString &input)
    {
        if (m_input == input)
            return;
        m_input = input;
        update();
    }

    QString input() const { return m_input; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.drawImage(rect(), passwordPanelImage(m_keys, m_columns, m_input,
                                                     m_clearKey, m_backspaceKey,
                                                     m_submitKey));
    }

    void mousePressEvent(QMouseEvent *evt) override
    {
        if (evt->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(evt);
            return;
        }

        const QSizeF panelSize(width(), height());
        for (int i = 0; i < m_keys.size(); ++i) {
            if (passwordKeyRect(i, m_columns, m_keys.size(), panelSize)
                    .contains(evt->position())) {
                if (keyActivated)
                    keyActivated(m_keys.at(i));
                evt->accept();
                return;
            }
        }
        evt->ignore();
    }

private:
    QStringList m_keys;
    int m_columns = 3;
    QString m_clearKey;
    QString m_backspaceKey;
    QString m_submitKey;
    QString m_input;
};

} // namespace

PasswordKeypadView::PasswordKeypadView(const QStringList &keys, int columns,
                                       const QString &clearKey,
                                       const QString &backspaceKey,
                                       const QString &submitKey,
                                       QWidget *parent)
    : PuzzleView(parent)
    , m_keys(keys)
    , m_clearKey(clearKey)
    , m_backspaceKey(backspaceKey)
    , m_submitKey(submitKey)
{
    setObjectName("passwordKeypadView");
    setStyleSheet(
        "QLabel#passwordFeedback {"
        "  color: #c9b5a5;"
        "  font-size: 12px;"
        "  background: transparent;"
        "}"
        "QToolButton#passwordClose {"
        "  background: transparent;"
        "  border: none;"
        "  color: #f0ece8;"
        "  font-size: 26px;"
        "}"
        "QToolButton#passwordClose:hover { color: #ffd15c; }");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addStretch(1);

    auto *panelRow = new QHBoxLayout();
    panelRow->setContentsMargins(0, 0, 0, 0);
    panelRow->addStretch(1);

    auto *panel = new PasswordPanel(keys, columns, clearKey, backspaceKey,
                                    submitKey, this);
    panel->keyActivated = [this](const QString &label) {
        activateKeyLabel(label);
    };
    m_panel = panel;

    panelRow->addWidget(panel, 0, Qt::AlignCenter);
    panelRow->addStretch(1);
    root->addLayout(panelRow);
    root->addSpacing(2);

    m_feedback = new QLabel(this);
    m_feedback->setObjectName("passwordFeedback");
    m_feedback->setAlignment(Qt::AlignCenter);
    m_feedback->setFixedHeight(22);
    root->addWidget(m_feedback);
    root->addSpacing(4);

    auto *closeButton = new QToolButton(this);
    closeButton->setObjectName("passwordClose");
    closeButton->setText(QStringLiteral("▼"));
    closeButton->setToolTip(QStringLiteral("关闭 (Esc)"));
    closeButton->setFixedSize(48, 36);
    closeButton->setFocusPolicy(Qt::NoFocus);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setAutoRaise(true);
    connect(closeButton, &QToolButton::clicked,
            this, &OverlayContent::closeRequested);
    root->addWidget(closeButton, 0, Qt::AlignHCenter);
    root->addStretch(1);

    m_feedbackTimer = new QTimer(this);
    m_feedbackTimer->setSingleShot(true);
    connect(m_feedbackTimer, &QTimer::timeout, this, [this] {
        m_feedback->clear();
    });
}

void PasswordKeypadView::setInput(const QString &input)
{
    if (auto *panel = static_cast<PasswordPanel *>(m_panel))
        panel->setInput(input);
}

void PasswordKeypadView::setFeedback(const QString &text, int durationMs)
{
    m_feedback->setText(text);
    m_feedbackTimer->stop();
    if (durationMs > 0)
        m_feedbackTimer->start(durationMs);
}

void PasswordKeypadView::applyEvent(const PuzzleViewEvent &event)
{
    if (event.type == QLatin1String(PasswordKeypadViewEvents::SetInput)) {
        setInput(event.payload.value(
            QString::fromLatin1(PasswordKeypadViewEvents::Text)).toString());
        return;
    }
    if (event.type == QLatin1String(PasswordKeypadViewEvents::Feedback)) {
        const QString text = event.payload.value(
            QString::fromLatin1(PasswordKeypadViewEvents::Text)).toString();
        const int duration = event.payload.value(
            QString::fromLatin1(PasswordKeypadViewEvents::DurationMs), 900).toInt();
        setFeedback(text, duration);
        return;
    }

    PuzzleView::applyEvent(event);
}

void PasswordKeypadView::emitKey(const QString &label)
{
    emit viewEvent(PuzzleViewEvent{
        QString::fromLatin1(PasswordKeypadViewEvents::Key),
        {{QString::fromLatin1(PasswordKeypadViewEvents::Label), label}}
    });
}

void PasswordKeypadView::activateKeyLabel(const QString &label)
{
    if (!isVisible() || label.isEmpty())
        return;

    // 先精确匹配（区分 c 和清除键 C），键盘输入找不到时再忽略大小写。
    for (const QString &key : m_keys) {
        if (key == label) {
            emitKey(key);
            return;
        }
    }
    for (const QString &key : m_keys) {
        if (key.compare(label, Qt::CaseInsensitive) == 0) {
            emitKey(key);
            return;
        }
    }
}

bool PasswordKeypadView::handleContentKey(QKeyEvent *evt)
{
    if (evt->key() == Qt::Key_Escape)
        return OverlayContent::handleContentKey(evt);

    if (evt->key() == Qt::Key_Backspace && !m_backspaceKey.isEmpty()) {
        emitKey(m_backspaceKey);
        return true;
    }
    if (evt->key() == Qt::Key_Delete && !m_clearKey.isEmpty()) {
        emitKey(m_clearKey);
        return true;
    }
    if ((evt->key() == Qt::Key_Return || evt->key() == Qt::Key_Enter)
        && !m_submitKey.isEmpty()) {
        emitKey(m_submitKey);
        return true;
    }

    // 有键盘时可以直接敲键面字母，省得用鼠标点。
    const QString text = evt->text();
    if (!text.isEmpty()) {
        for (const QString &key : m_keys) {
            if (key == text) {
                emitKey(key);
                return true;
            }
        }
        for (const QString &key : m_keys) {
            if (key.compare(text, Qt::CaseInsensitive) == 0) {
                emitKey(key);
                return true;
            }
        }
    }

    return OverlayContent::handleContentKey(evt);
}
