#ifndef TEXTART_H
#define TEXTART_H

#include <QColor>
#include <QImage>
#include <QString>

// 程序化生成的文字贴图：桌面提示、纸条等没有导出 PNG 的 SPR。
namespace TextArt {
QImage textCard(const QString &text,
                const QString &fontFamily = QStringLiteral("Consolas"),
                int pixelSize = 44,
                const QColor &color = QColor(43, 38, 32),
                const QColor &background = QColor(239, 231, 214),
                int width = 1024,
                int height = 256);
}

#endif // TEXTART_H
