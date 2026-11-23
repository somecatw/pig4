#ifndef SHARDART_H
#define SHARDART_H

#include <QImage>
#include <QPainterPath>
#include <QRectF>

// 月亮碎片的统一外观：原版里碎片是同一套均一颜色，靠形状区分。
// 目前只有粉色碎片（直角三角形，顶角约 20°，竖直放置，斜边在右）。
// 后续九碎片机关可以在这里注册更多形状，并复用同一套图标/面板绘制。
namespace ShardArt
{
inline constexpr qreal PinkAngleDegrees = 20.0;

// 面板坐标里的三角包围盒；pinkPath() 会在盒子里按 20° 计算实际宽高。
QRectF pinkPanelTriangleBox(qreal panelSize);
QPainterPath pinkPath(const QRectF &box);

// 背包图标：透明底、均一粉色、深色描边，无渐变/高光。
QImage pinkIcon(int size = 256);

// 粉色槽的 2D 面板；2D 操作界面和 3D sprite 共用同一张图。
QImage pinkPanelImage(bool filled, int size = 356);
}

#endif // SHARDART_H
