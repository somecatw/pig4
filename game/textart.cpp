#include "textart.h"

#include <QFont>
#include <QPainter>
#include <QPen>
#include <QPainterPath>
#include <QRectF>

QImage TextArt::textCard(const QString &text,
                         const QString &fontFamily,
                         int pixelSize,
                         const QColor &color,
                         const QColor &background,
                         int width,
                         int height)
{
    width = qMax(64, width);
    height = qMax(32, height);

    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF card(6.0, 6.0, width - 12.0, height - 12.0);
    if (background.alpha() > 0) {
        // 轻微的纸张投影，贴在桌面上更有实体感。
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 70));
        painter.drawRoundedRect(card.translated(3.0, 5.0), 10.0, 10.0);

        painter.setBrush(background);
        painter.setPen(QPen(QColor(126, 112, 88, 190), 2.0));
        painter.drawRoundedRect(card, 10.0, 10.0);
    }

    QFont font(fontFamily);
    font.setPixelSize(qMax(8, pixelSize));
    font.setStyleHint(QFont::Monospace);
    painter.setFont(font);
    painter.setPen(color);

    const QRectF textRect = card.adjusted(32.0, 18.0, -32.0, -18.0);
    painter.drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap
                              | Qt::TextWrapAnywhere, text);

    return image;
}
