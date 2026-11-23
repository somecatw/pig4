#ifndef GAMEEVENTS_H
#define GAMEEVENTS_H

#include <functional>
#include <QHash>
#include <QList>
#include <QString>

// 极简事件总线：JSON 里的连接表只存事件名，不依赖 Qt meta-object。
// 事件是瞬时的；需要持久状态（例如“机关已解开”）时由触发方自己维护。
class GameEvents
{
public:
    using Handler = std::function<void()>;

    void on(const QString &event, Handler handler);
    // 不能叫 emit：Qt 把 emit 定义成宏了。
    void publish(const QString &event) const;
    void clear();

private:
    QHash<QString, QList<Handler>> m_handlers;
};

#endif // GAMEEVENTS_H
