#include "clockart.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRadialGradient>
#include <QtMath>

QImage ClockArt::clockFace(int hour, int minute, int size)
{
    if (size < 64)
        size = 64;

    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QPointF center(size * 0.5, size * 0.5);
    const qreal radius = size * 0.46;

    // 墙面上的投影。alpha-test 可能会裁掉太淡的像素，所以用较实的黑色。
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 90));
    painter.drawEllipse(center + QPointF(size * 0.012, size * 0.018), radius, radius);

    // 金属外圈。
    QRadialGradient bezel(center - QPointF(radius * 0.30, radius * 0.35),
                          radius * 1.45);
    bezel.setColorAt(0.0, QColor(126, 104, 68));
    bezel.setColorAt(0.55, QColor(76, 60, 38));
    bezel.setColorAt(1.0, QColor(34, 27, 19));
    painter.setBrush(bezel);
    painter.drawEllipse(center, radius, radius);

    // 内外两道细圈，让表盘有层次。
    painter.setBrush(QColor(188, 160, 112));
    painter.drawEllipse(center, radius * 0.93, radius * 0.93);

    QRadialGradient face(center - QPointF(radius * 0.18, radius * 0.24),
                         radius * 1.35);
    face.setColorAt(0.0, QColor(252, 247, 234));
    face.setColorAt(0.68, QColor(237, 227, 207));
    face.setColorAt(1.0, QColor(210, 197, 169));
    painter.setBrush(face);
    painter.drawEllipse(center, radius * 0.885, radius * 0.885);

    // 60 格刻度；整点刻度更粗更长。
    for (int i = 0; i < 60; ++i) {
        const bool major = (i % 5 == 0);
        const qreal angle = qDegreesToRadians(qreal(i) * 6.0);
        const QPointF direction(qSin(angle), -qCos(angle));
        const qreal outer = radius * 0.825;
        const qreal inner = major ? radius * 0.725 : radius * 0.775;

        QPen pen(major ? QColor(45, 39, 31) : QColor(96, 87, 71));
        pen.setWidthF(major ? radius * 0.034 : radius * 0.016);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(center + direction * inner, center + direction * outer);
    }

    // 12 个阿拉伯数字。
    QFont font(QStringLiteral("Times New Roman"));
    font.setPixelSize(qMax(10, int(radius * 0.185)));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor(43, 38, 30));
    const QFontMetricsF metrics(font);
    for (int h = 1; h <= 12; ++h) {
        const qreal angle = qDegreesToRadians(qreal(h) * 30.0);
        const QPointF direction(qSin(angle), -qCos(angle));
        const QPointF position = center + direction * (radius * 0.60);
        const QRectF textRect(position.x() - radius * 0.15,
                              position.y() - metrics.height() * 0.5,
                              radius * 0.30, metrics.height());
        painter.drawText(textRect, Qt::AlignCenter, QString::number(h));
    }

    auto drawHand = [&](qreal angleDegrees, qreal length, qreal width,
                        const QColor &color) {
        painter.save();
        painter.translate(center);
        painter.rotate(angleDegrees);
        QPen pen(color);
        pen.setWidthF(width);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(QPointF(0, 0), QPointF(0, -length));
        painter.restore();
    };

    const qreal minuteAngle = qreal(((minute % 60) + 60) % 60) * 6.0;
    const qreal hourAngle = qreal(((hour % 12) + 12) % 12) * 30.0
                          + qreal(((minute % 60) + 60) % 60) * 0.5;
    drawHand(hourAngle, radius * 0.46, radius * 0.065, QColor(38, 33, 26));
    drawHand(minuteAngle, radius * 0.70, radius * 0.038, QColor(38, 33, 26));

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(58, 48, 34));
    painter.drawEllipse(center, radius * 0.062, radius * 0.062);
    painter.setBrush(QColor(199, 168, 112));
    painter.drawEllipse(center, radius * 0.032, radius * 0.032);

    return image;
}
