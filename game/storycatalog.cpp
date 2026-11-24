#include "storycatalog.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

static StoryStep lineStep(const QString &speaker, const QString &text)
{
    StoryStep step;
    step.kind = StoryStep::Line;
    step.line.speaker = speaker;
    step.line.text = text;
    return step;
}

static QList<StoryStep> stepsFromOpening(const QJsonArray &array)
{
    QList<StoryStep> steps;
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const QString text = object.value(QStringLiteral("text")).toString();
        if (text.isEmpty())
            continue;
        steps.append(lineStep(
            object.value(QStringLiteral("speaker")).toString(), text));
    }
    return steps;
}

static QList<StoryStep> stepsFromSteps(const QJsonArray &steps,
                                       const QHash<int, QString> &speakers)
{
    QList<StoryStep> result;
    for (const QJsonValue &value : steps) {
        if (!value.isObject())
            continue;
        const QJsonObject step = value.toObject();
        const QString type = step.value(QStringLiteral("type")).toString();

        if (type == QStringLiteral("line")
            || type == QStringLiteral("narration")) {
            QString speaker;
            if (type == QStringLiteral("line")) {
                const QJsonValue speakerValue =
                    step.value(QStringLiteral("speaker"));
                if (speakerValue.isString()) {
                    speaker = speakerValue.toString();
                } else {
                    const int speakerId =
                        step.value(QStringLiteral("speakerId")).toInt(-1);
                    speaker = speakers.value(speakerId);
                }
            }
            const QString text = step.value(QStringLiteral("text")).toString();
            if (!text.isEmpty())
                result.append(lineStep(speaker, text));
            continue;
        }

        // 只执行我们手写脚本里的 command；plots.json 里的 raw 是 export
        // 原始指令，保留作迁移参考，不直接交给运行时。
        if (type == QStringLiteral("command")) {
            StoryStep command;
            command.kind = StoryStep::Command;
            command.command.name =
                step.value(QStringLiteral("name")).toString();
            command.command.args =
                step.value(QStringLiteral("args")).toArray();
            if (!command.command.name.isEmpty())
                result.append(command);
        }
    }
    return result;
}

static bool stepsHaveLines(const QList<StoryStep> &steps)
{
    for (const StoryStep &step : steps) {
        if (step.kind == StoryStep::Line && !step.line.text.isEmpty())
            return true;
    }
    return false;
}

bool StoryScript::hasLines() const
{
    return stepsHaveLines(steps);
}

int StoryScript::lineCount() const
{
    int count = 0;
    for (const StoryStep &step : steps) {
        if (step.kind == StoryStep::Line && !step.line.text.isEmpty())
            ++count;
    }
    return count;
}

bool StoryCatalog::load(const QString &directory)
{
    m_opening.clear();
    m_plots.clear();
    m_loaded = false;

    const QDir dir(directory);

    // Opening is authored in movie_text.json together with the other movie
    // timelines. opening.json is only a compatibility fallback.
    QFile movieFile(dir.filePath(QStringLiteral("movie_text.json")));
    if (movieFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QJsonParseError error{};
        const QJsonDocument document =
            QJsonDocument::fromJson(movieFile.readAll(), &error);
        if (error.error == QJsonParseError::NoError && document.isObject()) {
            m_opening = stepsFromOpening(
                document.object().value(QStringLiteral("KaiChangBai")).toArray());
        } else {
            qWarning() << "movie_text.json parse failed:" << error.errorString();
        }
    }

    if (m_opening.isEmpty()) {
        QFile openingFile(dir.filePath(QStringLiteral("opening.json")));
        if (openingFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonParseError error{};
            const QJsonDocument document =
                QJsonDocument::fromJson(openingFile.readAll(), &error);
            if (error.error == QJsonParseError::NoError && document.isObject()) {
                m_opening = stepsFromOpening(
                    document.object().value(QStringLiteral("lines")).toArray());
            } else {
                qWarning() << "opening.json parse failed:" << error.errorString();
            }
        } else {
            qWarning() << "opening.json not found in" << directory;
        }
    }

    QJsonObject root;
    QFile plotsFile(dir.filePath(QStringLiteral("plots.json")));
    if (plotsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QJsonParseError error{};
        const QJsonDocument document =
            QJsonDocument::fromJson(plotsFile.readAll(), &error);
        if (error.error == QJsonParseError::NoError && document.isObject())
            root = document.object();
        else
            qWarning() << "plots.json parse failed:" << error.errorString();
    } else {
        qWarning() << "plots.json not found in" << directory;
    }

    QHash<int, QString> speakers;
    const QJsonObject speakerObject =
        root.value(QStringLiteral("speakers")).toObject();
    for (auto it = speakerObject.begin(); it != speakerObject.end(); ++it) {
        bool ok = false;
        const int id = it.key().toInt(&ok);
        if (ok && it.value().isString())
            speakers.insert(id, it.value().toString());
    }

    auto applyPlots = [this, &speakers](const QJsonArray &plotArray,
                                        bool overrideModules) {
        for (const QJsonValue &plotValue : plotArray) {
            if (!plotValue.isObject())
                continue;
            const QJsonObject plotObject = plotValue.toObject();
            const QString plotId =
                plotObject.value(QStringLiteral("id")).toString();
            if (plotId.isEmpty())
                continue;

            PlotRecord *plotRecord = nullptr;
            for (PlotRecord &candidate : m_plots) {
                if (candidate.id == plotId) {
                    plotRecord = &candidate;
                    break;
                }
            }
            if (plotRecord == nullptr) {
                PlotRecord created;
                created.id = plotId;
                created.plotIndex =
                    plotObject.value(QStringLiteral("plotIndex")).toInt(-1);
                m_plots.append(created);
                plotRecord = &m_plots.last();
            }

            for (const QJsonValue &moduleValue :
                 plotObject.value(QStringLiteral("modules")).toArray()) {
                if (!moduleValue.isObject())
                    continue;
                const QJsonObject moduleObject = moduleValue.toObject();
                ModuleRecord module;
                module.name =
                    moduleObject.value(QStringLiteral("name")).toString();
                if (module.name.isEmpty())
                    continue;
                module.steps = stepsFromSteps(
                    moduleObject.value(QStringLiteral("steps")).toArray(),
                    speakers);

                if (overrideModules) {
                    ModuleRecord *existing = nullptr;
                    for (ModuleRecord &candidate : plotRecord->modules) {
                        if (candidate.name == module.name) {
                            existing = &candidate;
                            break;
                        }
                    }
                    if (existing != nullptr) {
                        existing->steps = module.steps;
                        continue;
                    }
                }
                plotRecord->modules.append(module);
            }
        }
    };

    applyPlots(root.value(QStringLiteral("plots")).toArray(), false);

    // 手写脚本覆盖同名 plot/module；extract_flash_story.py 不会写这个文件。
    QFile customFile(dir.filePath(QStringLiteral("plots_custom.json")));
    if (customFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QJsonParseError error{};
        const QJsonDocument document =
            QJsonDocument::fromJson(customFile.readAll(), &error);
        if (error.error == QJsonParseError::NoError && document.isObject()) {
            const QJsonObject customRoot = document.object();
            applyPlots(customRoot.value(QStringLiteral("plots")).toArray(), true);
            qInfo() << "story custom scripts:" << customFile.fileName();
        } else {
            qWarning() << "plots_custom.json parse failed:"
                       << error.errorString();
        }
    }

    m_loaded = !m_opening.isEmpty() || !m_plots.isEmpty();
    qInfo() << "story catalog:" << m_plots.size() << "plots,"
            << m_opening.size() << "opening steps";
    return m_loaded;
}

const StoryCatalog::PlotRecord *StoryCatalog::plot(const QString &plotId) const
{
    for (const PlotRecord &record : m_plots) {
        if (record.id == plotId)
            return &record;
    }
    return nullptr;
}

QStringList StoryCatalog::plotIds() const
{
    QStringList ids;
    ids.reserve(m_plots.size());
    for (const PlotRecord &record : m_plots)
        ids.append(record.id);
    return ids;
}

QStringList StoryCatalog::moduleNames(const QString &plotId) const
{
    QStringList names;
    const PlotRecord *record = plot(plotId);
    if (record == nullptr)
        return names;
    names.reserve(record->modules.size());
    for (const ModuleRecord &module : record->modules)
        names.append(module.name);
    return names;
}

StoryScript StoryCatalog::openingScript() const
{
    StoryScript script;
    script.id = QStringLiteral("opening");
    script.key = QStringLiteral("opening");
    script.title = QStringLiteral("开场白");
    script.steps = m_opening;
    return script;
}

StoryScript StoryCatalog::script(const QString &plotId,
                                 const QString &moduleName) const
{
    const PlotRecord *record = plot(plotId);
    if (record == nullptr || record->modules.isEmpty())
        return {};

    const ModuleRecord *found = nullptr;
    if (moduleName.isEmpty()) {
        found = &record->modules.first();
    } else {
        for (const ModuleRecord &module : record->modules) {
            if (module.name == moduleName) {
                found = &module;
                break;
            }
        }
    }
    if (found == nullptr)
        return {};

    StoryScript script;
    script.id = plotId;
    script.key = plotId + QLatin1Char(':') + found->name;
    script.title = plotId + QStringLiteral(" / ") + found->name;
    script.steps = found->steps;
    return script;
}

StoryScript StoryCatalog::firstScript() const
{
    if (!m_opening.isEmpty())
        return openingScript();
    for (const PlotRecord &record : m_plots) {
        for (const ModuleRecord &module : record.modules) {
            if (!stepsHaveLines(module.steps))
                continue;
            StoryScript script;
            script.id = record.id;
            script.key = record.id + QLatin1Char(':') + module.name;
            script.title = record.id + QStringLiteral(" / ") + module.name;
            script.steps = module.steps;
            return script;
        }
    }
    return {};
}
