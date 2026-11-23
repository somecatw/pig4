#ifndef ITEMCATALOG_H
#define ITEMCATALOG_H

#include <QHash>
#include <QImage>
#include <QList>
#include <QString>

struct ItemViewDef
{
    QImage image;
    QString caption;
};

struct ItemCombineDef
{
    QString with;
    QString result;
    QString notice;
};

struct ItemDef
{
    QString id;
    QString name;
    // 自定义内容 widget 的 kind，例如 "paper" / "moon_block"；
    // 为空时用默认的静态图片内容。
    QString content;
    QList<ItemViewDef> views;
    QList<ItemCombineDef> combines;
};

// 道具的查看/组合配置。图标仍由 InventoryItem 保存；这里负责放大查看的
// 多面视图和组合配方。
class ItemCatalog
{
public:
    bool load(const QString &path);

    const ItemDef *find(const QString &id) const;
    ItemDef definitionFor(const QString &id, const QString &fallbackName,
                          const QImage &fallbackIcon) const;

private:
    QHash<QString, ItemDef> m_items;
};

#endif // ITEMCATALOG_H
