#ifndef STORYCATALOG_H
#define STORYCATALOG_H

#include <QList>
#include <QString>
#include <QStringList>

// 剧情界面用的一行：speaker 为空表示旁白。
struct StoryLine
{
    QString speaker;
    QString text;
};

// 一段可播放的剧情。第一版只取原文里的 line / narration，
// raw 命令（门、机关、选项等）暂时跳过，等我们按自己的配置重写。
struct StoryScript
{
    QString id;
    QString title;
    QList<StoryLine> lines;
};

// 读取 assets/story/opening.json 和 plots.json。
// 只保留“显示一条剧情文本”的步骤，其余 raw 步骤不进入 StoryLine。
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
        QList<StoryLine> lines;
    };
    struct PlotRecord {
        QString id;
        int plotIndex = -1;
        QList<ModuleRecord> modules;
    };

    QList<StoryLine> m_opening;
    QList<PlotRecord> m_plots;
    bool m_loaded = false;

    const PlotRecord *plot(const QString &plotId) const;
};

#endif // STORYCATALOG_H
