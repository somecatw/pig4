#ifndef STORYCATALOG_H
#define STORYCATALOG_H

#include <QJsonArray>
#include <QList>
#include <QString>
#include <QStringList>

// 剧情界面用的一行：speaker 为空表示旁白。
struct StoryLine
{
    QString speaker;
    QString text;
};

// 我们自己的剧情指令。args 是手写脚本里的参数数组，不含命令名。
// 未注册的命令由运行时直接 pass，剧情继续往下走。
struct StoryCommand
{
    QString name;
    QJsonArray args;
};

// 一段剧情按原始顺序保存文本和命令。命令在播放到它时执行，快进时也会执行。
struct StoryStep
{
    enum Kind { Line, Command };
    Kind kind = Line;
    StoryLine line;
    StoryCommand command;
};

// 一段可播放的剧情。key 是 <plotId>:<module>，用于剧情生命周期事件。
struct StoryScript
{
    QString id;
    QString key;
    QString title;
    QList<StoryStep> steps;

    bool hasLines() const;
    int lineCount() const;
};

// 读取 assets/story/movie_text.json 的 KaiChangBai 作为开场白，
// 缺失时回退到 opening.json；plots.json 提供从 export 提取的文本。
// plots_custom.json 是手写脚本，用来覆盖同名 plot/module，并携带我们自己的
// command 步骤；原始的 raw 步骤只留在 plots.json 里作为迁移参考，不执行。
class StoryCatalog
{
public:
    bool load(const QString &directory);
    bool isLoaded() const { return m_loaded; }

    QStringList plotIds() const;
    QStringList moduleNames(const QString &plotId) const;
    StoryScript openingScript() const;
    StoryScript script(const QString &plotId, const QString &moduleName = QString()) const;
    // 优先开场白；没有开场白时返回第一个含文本的 plot module。
    StoryScript firstScript() const;

private:
    struct ModuleRecord {
        QString name;
        QList<StoryStep> steps;
    };
    struct PlotRecord {
        QString id;
        int plotIndex = -1;
        QList<ModuleRecord> modules;
    };

    QList<StoryStep> m_opening;
    QList<PlotRecord> m_plots;
    bool m_loaded = false;

    const PlotRecord *plot(const QString &plotId) const;
};

#endif // STORYCATALOG_H
