#include "itemcatalog.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

bool ItemCatalog::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "item catalog not found:" << path;
        return false;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "item catalog parse error:" << path << err.errorString();
        return false;
    }

    const QDir imageDir(QFileInfo(path).absolutePath() + QStringLiteral("/images"));
    const QJsonObject items = doc.object().value("items").toObject();
    for (auto it = items.constBegin(); it != items.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        ItemDef def;
        def.id = it.key();
        def.name = o.value("name").toString();
        def.content = o.value("content").toString();

        const QJsonArray views = o.value("views").toArray();
        for (const QJsonValue &viewValue : views) {
            const QJsonObject viewObject = viewValue.toObject();
            const QString imageName = viewObject.value("image").toString();
            if (imageName.isEmpty())
                continue;
            const QImage image(imageDir.filePath(imageName));
            if (image.isNull()) {
                qWarning() << "item view image missing:" << def.id << imageName;
                continue;
            }
            ItemViewDef view;
            view.image = image;
            view.caption = viewObject.value("caption").toString();
            def.views.push_back(view);
        }

        const QJsonArray combines = o.value("combines").toArray();
        for (const QJsonValue &combineValue : combines) {
            const QJsonObject combineObject = combineValue.toObject();
            ItemCombineDef combine;
            combine.with = combineObject.value("with").toString();
            combine.result = combineObject.value("result").toString();
            combine.notice = combineObject.value("notice").toString();
            if (!combine.with.isEmpty() && !combine.result.isEmpty())
                def.combines.push_back(combine);
        }

        m_items.insert(def.id, def);
    }

    qDebug() << "item catalog loaded:" << m_items.size() << "definitions from" << path;
    return true;
}

const ItemDef *ItemCatalog::find(const QString &id) const
{
    auto it = m_items.constFind(id);
    return it == m_items.constEnd() ? nullptr : &it.value();
}

ItemDef ItemCatalog::definitionFor(const QString &id, const QString &fallbackName,
                                   const QImage &fallbackIcon) const
{
    if (const ItemDef *existing = find(id)) {
        ItemDef def = *existing;
        if (def.name.isEmpty())
            def.name = fallbackName;
        if (def.views.isEmpty() && !fallbackIcon.isNull()) {
            ItemViewDef fallback;
            fallback.image = fallbackIcon;
            fallback.caption = def.name;
            def.views.push_back(fallback);
        }
        return def;
    }

    ItemDef def;
    def.id = id;
    def.name = fallbackName;
    if (!fallbackIcon.isNull()) {
        ItemViewDef fallback;
        fallback.image = fallbackIcon;
        fallback.caption = fallbackName;
        def.views.push_back(fallback);
    }
    return def;
}
