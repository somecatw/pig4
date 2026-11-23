#ifndef CLOCKART_H
#define CLOCKART_H

#include <QImage>

// 大堂墙上的四个钟表是 PH_SPR，没有导出贴图；按谜题参数直接画表盘。
// hour 使用 0–23 或 1–12 均可，内部按 12 小时取模。
namespace ClockArt {
QImage clockFace(int hour, int minute, int size = 256);
}

#endif // CLOCKART_H
