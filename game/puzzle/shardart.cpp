#include "shardart.h"

#include <QPainter>
#include <QPen>
#include <algorithm>
#include <cmath>

namespace ShardArt
{
namespace {

// 原版碎片是均一的粉色；不要渐变和高光。
const QColor kShardPink(0xf0, 0x6e, 0xa9);
const QColor kPanelPink(0xd9, 0x8c, 0x99);
const QColor kOutline(0x2c, 0x24, 0x26);

} // namespace

QRectF pinkPanelTriangleBox(qreal panelSize)
{
    // 竖放的窄长三角形：高度约面板的 72%，宽度由 20° 顶角决定。
    const qreal height = panelSize * 0.72;
    const qreal width = height * std::tan(PinkAngleDegrees * 3.14159265358979 / 180.0);
    return QRectF((panelSize - width) * 0.5,
                  (panelSize - height) * 0.5,
                  width, height);
}

QPainterPath pinkPath(const QRectF &box)
{
    const qreal width = std::min(box.width(),
                                 box.height() * std::tan(PinkAngleDegrees * 3.14159265358979 / 180.0));
    const qreal height = width / std::tan(PinkAngleDegrees * 3.14159265358979 / 180.0);
    const qreal x = box.left() + (box.width() - width) * 0.5;
    const qreal y = box.top() + (box.height() - height) * 0.5;

    // 左边竖直、底边水平、斜边在右，小角在顶部。
    QPainterPath path;
    path.moveTo(x, y);
    path.lineTo(x, y + height);
    path.lineTo(x + width, y + height);
    path.closeSubpath();
    return path;
}

QImage pinkIcon(int size)
{
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const qreal margin = size * 0.14;
    const QRectF box(margin, margin, size - margin * 2, size - margin * 2);
    const QPainterPath path = pinkPath(box);

    painter.setPen(QPen(kOutline, std::max(2.0, size * 0.014),
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(kShardPink);
    painter.drawPath(path);
    return img;
}

QImage pinkPanelImage(bool filled, int size)
{
    QImage img(size, size, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF panelRect(1, 1, size - 2, size - 2);
    painter.setPen(QPen(QColor(0xf2, 0xe7, 0xe9), 2));
    painter.setBrush(kPanelPink);
    painter.drawRect(panelRect);

    const QPainterPath path = pinkPath(pinkPanelTriangleBox(size));
    if (filled) {
        painter.setPen(QPen(kOutline, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(kShardPink);
        painter.drawPath(path);
    } else {
        painter.setPen(QPen(kOutline, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(QColor("#000000"));
        painter.drawPath(path);
    }

    return img;
}

} // namespace ShardArt
