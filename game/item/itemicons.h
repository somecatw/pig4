#ifndef ITEMICONS_H
#define ITEMICONS_H

#include <QColor>
#include <QImage>
#include <QString>

// 原版 Flash 用库里的矢量/位图元件拼道具图标；这里用 QPainter 生成同样的变体。
// 目前只做占位，等有正式美术资源后可以替换绘制实现，不影响背包数据。
namespace ItemIcons
{
// 指向 assets/images：silver_key.png / key_template.png / remote.png / door_card.png。
// 找不到时回退到纯代码绘制的占位图。
void setImageDirectory(const QString &dir);
QImage imageIcon(const QString &fileName);

QColor silver();
QColor bronze();
QColor gold();

// number > 0 时用 key_template.png 并在钥匙头叠数字，用于 1~9 号钥匙。
QImage keyIcon(const QColor &metal, int number = 0);
QImage moonBlockIcon();
QImage triangleShardIcon(const QColor &color, int variant);
// 原版粉色碎片：均一粉色、直角三角形（20°），无渐变/高光。
QImage pinkShardIcon();
QImage colorfulShardIcon();
}

#endif // ITEMICONS_H
