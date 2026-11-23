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

static QList<StoryLine> linesFromOpening(const QJsonArray &array)
{
    QList<StoryLine> lines;
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        StoryLine line;
        const QJsonValue speaker = object.value(QStringLiteral("speaker"));
        if (speaker.isString())
            line.speaker = speaker.toString();
        line.text = object.value(QStringLiteral("text")).toString();
        if (!line.text.isEmpty())
            lines.append(line);
    }
    return lines;
}

static QList<StoryLine> linesFromSteps(const QJsonArray &steps,
                                       const QHash<int, QString> &speakers)
{
    QList<StoryLine> lines;
    for (const QJsonValue &value : steps) {
        if (!value.isObject())
            continue;
        const QJsonObject step = value.toObject();
        const QString type = step.value(QStringLiteral("type")).toString();

        StoryLine line;
        if (type == QStringLiteral("line")) {
            const QJsonValue speaker = step.value(QStringLiteral("speaker"));
            if (speaker.isString()) {
                line.speaker = speaker.toString();
            } else {
                const int speakerId = step.value(QStringLiteral("speakerId")).toInt(-1);
                line.speaker = speakers.value(speakerId);
            }
            line.text = step.value(QStringLiteral("text")).toString();
        } else if (type == QStringLiteral("narration")) {
            line.text = step.value(QStringLiteral("text")).toString();
        } else {
            // raw：原版的门/机关/选项/立绘等，第一版不执行。
            continue;
        }

        if (!line.text.isEmpty())
            lines.append(line);
    }
    return lines;
}

bool StoryCatalog::load(const QString &directory)
{
    m_opening.clear();
    m_plots.clear();
    m_loaded = false;

    const QDir dir(directory);

    QFile openingFile(dir.filePath(QStringLiteral("opening.json")));
    if (openingFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QJsonParseError error{};
        const QJsonDocument document =
            QJsonDocument::fromJson(openingFile.readAll(), &error);
        if (error.error == QJsonParseError::NoError && document.isObject()) {
            m_opening = linesFromOpening(
                document.object().value(QStringLiteral("lines")).toArray());
        } else {
            qWarning() << "opening.json parse failed:" << error.errorString();
        }
    } else {
        qWarning() << "opening.json not found in" << directory;
    }

    QFile plotsFile(dir.filePath(QStringLiteral("plots.json")));
    if (!plotsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "plots.json not found in" << directory;
        m_loaded = !m_opening.isEmpty();
        return m_loaded;
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(plotsFile.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << "plots.json parse failed:" << error.errorString();
        m_loaded = !m_opening.isEmpty();
        return m_loaded;
    }

    const QJsonObject root = document.object();

    QHash<int, QString> speakers;
    const QJsonObject speakerObject = root.value(QStringLiteral("speakers")).toObject();
    for (auto it = speakerObject.begin(); it != speakerObject.end(); ++it) {
        bool ok = false;
        const int id = it.key().toInt(&ok);
        if (ok && it.value().isString())
            speakers.insert(id, it.value().toString());
    }

    for (const QJsonValue &plotValue : root.value(QStringLiteral("plots")).toArray()) {
        if (!plotValue.isObject())
            continue;
        const QJsonObject plotObject = plotValue.toObject();

        PlotRecord plotRecord;
        plotRecord.id = plotObject.value(QStringLiteral("id")).toString();
        plotRecord.plotIndex = plotObject.value(QStringLiteral("plotIndex")).toInt(-1);
        if (plotRecord.id.isEmpty())
            continue;

        for (const QJsonValue &moduleValue :
             plotObject.value(QStringLiteral("modules")).toArray()) {
            if (!moduleValue.isObject())
                continue;
            const QJsonObject moduleObject = moduleValue.toObject();
            ModuleRecord module;
            module.name = moduleObject.value(QStringLiteral("name")).toString();
            if (module.name.isEmpty())
                continue;
            module.lines = linesFromSteps(
                moduleObject.value(QStringLiteral("steps")).toArray(), speakers);
            plotRecord.modules.append(module);
        }

        m_plots.append(plotRecord);
    }

    m_loaded = !m_opening.isEmpty() || !m_plots.isEmpty();
    qInfo() << "story catalog:" << m_plots.size() << "plots,"
            << m_opening.size() << "opening lines";
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
    script.title = QStringLiteral("开场白");
    script.lines = m_opening;
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
    script.title = plotId + QStringLiteral(" / ") + found->name;
    script.lines = found->lines;
    return script;
}

StoryScript StoryCatalog::firstScript() const
{
    if (!m_opening.isEmpty())
        return openingScript();
    for (const PlotRecord &record : m_plots) {
        for (const ModuleRecord &module : record.modules) {
            if (module.lines.isEmpty())
                continue;
            StoryScript script;
            script.id = record.id;
            script.title = record.id + QStringLiteral(" / ") + module.name;
            script.lines = module.lines;
            return script;
        }
    }
    return {};
}
