#include "overlayui.h"

#include <QColor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStackedLayout>

OverlayContent::OverlayContent(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
}

bool OverlayContent::handleContentKey(QKeyEvent *evt)
{
    if (evt->key() == Qt::Key_Escape) {
        emit closeRequested();
        return true;
    }
    return false;
}

void OverlayContent::keyPressEvent(QKeyEvent *evt)
{
    if (handleContentKey(evt)) {
        evt->accept();
        return;
    }
    QWidget::keyPressEvent(evt);
}

StageOverlay::StageOverlay(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("stageOverlay");
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet("QWidget#stageOverlay { background-color: transparent; }");

    m_stack = new QStackedLayout(this);
    m_stack->setContentsMargins(0, 0, 0, 0);
    hide();
}

void StageOverlay::updateBackdrop()
{
    m_backdropOpacity = 0;
    if (auto *overlay = qobject_cast<OverlayContent *>(m_content))
        m_backdropOpacity = qBound(0, overlay->backdropOpacity(), 255);
    update();
}

void StageOverlay::setContent(QWidget *content)
{
    if (m_content == content) {
        if (content != nullptr) {
            updateBackdrop();
            content->show();
            show();
            raise();
            setFocus(Qt::OtherFocusReason);
            content->setFocus(Qt::OtherFocusReason);
        }
        return;
    }

    if (m_content != nullptr) {
        if (auto *overlay = qobject_cast<OverlayContent *>(m_content))
            overlay->overlayDeactivated();
        m_stack->removeWidget(m_content);
        m_content->hide();
        m_content = nullptr;
    }

    if (content == nullptr) {
        m_backdropOpacity = 0;
        hide();
        update();
        return;
    }

    m_content = content;
    m_stack->addWidget(content);
    m_stack->setCurrentWidget(content);
    content->show();
    show();
    raise();

    // 内容通常自己接焦点；如果内容是 NoFocus，外壳兜住键盘。
    setFocus(Qt::OtherFocusReason);
    content->setFocus(Qt::OtherFocusReason);
    if (auto *overlay = qobject_cast<OverlayContent *>(content))
        overlay->overlayActivated();
    updateBackdrop();
}

QWidget *StageOverlay::takeContent()
{
    QWidget *content = m_content;
    if (content != nullptr) {
        if (auto *overlay = qobject_cast<OverlayContent *>(content))
            overlay->overlayDeactivated();
        m_stack->removeWidget(content);
        content->hide();
        m_content = nullptr;
    }
    m_backdropOpacity = 0;
    hide();
    update();
    return content;
}

void StageOverlay::paintEvent(QPaintEvent *evt)
{
    if (m_backdropOpacity > 0) {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0, 0, 0, m_backdropOpacity));
    }
    QWidget::paintEvent(evt);
}

void StageOverlay::keyPressEvent(QKeyEvent *evt)
{
    if (auto *overlay = qobject_cast<OverlayContent *>(m_content)) {
        if (overlay->handleContentKey(evt)) {
            evt->accept();
            return;
        }
    }
    if (evt->key() == Qt::Key_Escape) {
        emit closeRequested();
        evt->accept();
        return;
    }
    QWidget::keyPressEvent(evt);
}

void StageOverlay::mousePressEvent(QMouseEvent *evt)
{
    evt->accept();
}

void StageOverlay::mouseReleaseEvent(QMouseEvent *evt)
{
    evt->accept();
}

void StageOverlay::mouseMoveEvent(QMouseEvent *evt)
{
    evt->accept();
}
