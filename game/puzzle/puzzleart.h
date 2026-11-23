#ifndef PUZZLEART_H
#define PUZZLEART_H

#include <QImage>

// 3×3 keypad 的 2D 外观。世界里的 sprite 和放大的操作界面共用同一张图。
// 尺寸和 KeypadPuzzleView 的布局严格对应：356 = 18*2 + 100*3 + 10*2。
QImage keypadPuzzleImage();

#endif // PUZZLEART_H
