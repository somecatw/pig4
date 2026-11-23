#include "gameevents.h"

#include <QDebug>

void GameEvents::on(const QString &event, Handler handler)
{
    if (event.isEmpty() || !handler)
        return;
    m_handlers[event].push_back(std::move(handler));
}

void GameEvents::publish(const QString &event) const
{
    const auto it = m_handlers.constFind(event);
    if (it == m_handlers.constEnd()) {
        qDebug() << "event:" << event << "(no handlers)";
        return;
    }

    // 复制一份，允许回调里注册新事件而不破坏当前遍历。
    const QList<Handler> handlers = it.value();
    qDebug() << "event:" << event << "handlers:" << handlers.size();
    for (const Handler &handler : handlers)
        handler();
}

void GameEvents::clear()
{
    m_handlers.clear();
}
