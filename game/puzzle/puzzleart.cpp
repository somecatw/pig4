#include "puzzleart.h"

#include <QFont>
#include <QPainter>
#include <QPen>

QImage keypadPuzzleImage()
{
    static const QImage image = [] {
        constexpr int size = 356;
        constexpr int margin = 18;
        constexpr int spacing = 10;
        constexpr int keySize = 100;

        QImage img(size, size, QImage::Format_ARGB32);
        img.fill(QColor(48, 30, 23));

        QPainter painter(&img);
        painter.setRenderHint(QPainter::Antialiasing, true);

        painter.setPen(QPen(QColor(0x7e, 0x5d, 0x4e), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(1, 1, size - 2, size - 2));

        QFont font(QStringLiteral("Times New Roman"));
        font.setPixelSize(46);
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);

        for (int i = 0; i < 9; ++i) {
            const int row = i / 3;
            const int col = i % 3;
            const QRectF rect(margin + col * (keySize + spacing),
                              margin + row * (keySize + spacing),
                              keySize, keySize);

            painter.setPen(QPen(QColor(0xd8, 0xc8, 0xbe), 3));
            painter.setBrush(QColor(0x5a, 0x40, 0x34));
            painter.drawRect(rect);
            painter.setPen(QColor(0xd8, 0xc7, 0xb6));
            painter.drawText(rect, Qt::AlignCenter, QString::number(i + 1));
        }

        return img;
    }();
    return image;
}
