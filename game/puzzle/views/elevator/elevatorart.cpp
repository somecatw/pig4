#include "elevatorart.h"

#include <QFont>
#include <QPainter>
#include <QPen>

QImage elevatorPanelImage(const QStringList &reachableFloors,
                          const QString &currentFloor)
{
    // 参考原版面板的比例：198x376，按钮 104x43，竖排 6F..1F。
    constexpr int width = 198;
    constexpr int height = 376;
    constexpr int buttonX = 49;
    constexpr int buttonW = 104;
    constexpr int buttonH = 43;
    const int rowY[6] = {38, 88, 139, 188, 238, 289};
    const QString labels[6] = {
        QStringLiteral("6F"), QStringLiteral("5F"), QStringLiteral("4F"),
        QStringLiteral("3F"), QStringLiteral("2F"), QStringLiteral("1F")
    };

    QImage img(width, height, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(30);
    font.setWeight(QFont::Normal);
    painter.setFont(font);

    for (int i = 0; i < 6; ++i) {
        const QString &floor = labels[i];
        const bool reachable = reachableFloors.contains(floor);
        const bool current = !currentFloor.isEmpty() && floor == currentFloor;
        const QColor fill = current ? QColor(0x00, 0xb0, 0xb0)
                           : reachable ? QColor(0x00, 0x69, 0x69)
                                       : QColor(0x33, 0x33, 0x33);

        const QRectF rect(buttonX, rowY[i], buttonW, buttonH);
        painter.setPen(QPen(QColor(0x1d, 0x1d, 0x1d), 1));
        painter.setBrush(fill);
        painter.drawRoundedRect(rect, 2, 2);

        painter.setPen(QColor(0x0b, 0x0b, 0x0b));
        painter.drawText(rect, Qt::AlignCenter, floor);
    }

    // 最下面额外一个“开门”键，供电梯里面使用。
    const QRectF openRect(buttonX, 338, buttonW, 28);
    painter.setPen(QPen(QColor(0x1d, 0x1d, 0x1d), 1));
    painter.setBrush(QColor(0x00, 0x69, 0x69));
    painter.drawRoundedRect(openRect, 2, 2);
    QFont openFont(QStringLiteral("Arial"));
    openFont.setPixelSize(16);
    painter.setFont(openFont);
    painter.setPen(QColor(0x0b, 0x0b, 0x0b));
    painter.drawText(openRect, Qt::AlignCenter, QStringLiteral("开门"));

    return img;
}

QImage elevatorCallButtonImage()
{
    // 电梯外面的小面板：深色底座 + 一个召梯按钮。
    constexpr int width = 128;
    constexpr int height = 160;
    QImage img(width, height, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF plate(4, 4, width - 8, height - 8);
    painter.setPen(QPen(QColor(0x1d, 0x1d, 0x1d), 2));
    painter.setBrush(QColor(0x33, 0x35, 0x38));
    painter.drawRoundedRect(plate, 6, 6);

    const QRectF button(28, 46, 72, 72);
    painter.setPen(QPen(QColor(0x14, 0x14, 0x14), 2));
    painter.setBrush(QColor(0x00, 0x69, 0x69));
    painter.drawEllipse(button);

    // 上下箭头；原版召梯键的常见样式。
    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(30);
    font.setWeight(QFont::Bold);
    painter.setFont(font);
    painter.setPen(QColor(0xe8, 0xf4, 0xf4));
    painter.drawText(button.adjusted(0, -12, 0, 0), Qt::AlignCenter, QStringLiteral("▲"));
    painter.drawText(button.adjusted(0, 12, 0, 0), Qt::AlignCenter, QStringLiteral("▼"));

    // 顶部的小指示灯。
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xb0, 0x6a, 0x2a));
    painter.drawEllipse(QPointF(width / 2.0, 26), 5, 5);

    return img;
}
