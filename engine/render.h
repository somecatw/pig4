#ifndef RENDER_H
#define RENDER_H

#include "structures.h"
#include <QString>

const bool showStatistics = true;

void clearRenderBuffer();

void submitMesh(const Mesh &mesh);

void drawFrame(const CameraInfo &camera, uint *buffer);

// 调试：在正常渲染后额外光栅化一遍 alpha-test 面片，把第一阶段实际使用的
// 透视校正 UV、alpha 贴图采样结果 dump 成 PNG。只有 PIG4_DUMP_UV 设置时才开销。
void setDebugUVCapture(bool enabled);
void dumpDebugUV(const QString &prefix);


#endif // RENDER_H
