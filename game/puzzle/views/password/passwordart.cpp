#include "passwordart.h"

#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <cmath>

namespace {

// 显示屏里每个输入字符统一画成这个绿色小人/符号，和原版一样不泄露字母。
void drawPasswordInputGlyph(QPainter &painter, const QRectF &box)
{
    const qreal s = qMin(box.width(), box.height());
    const qreal cx = box.center().x();
    const qreal top = box.center().y() - s * 0.5;
    const QColor green(0x18, 0xff, 0x2a);

    auto drawShape = [&](qreal width, const QColor &color) {
        painter.setPen(QPen(color, s * width, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);

        // 头顶的一条粗竖线。
        painter.drawLine(QPointF(cx, top + s * 0.10),
                         QPointF(cx, top + s * 0.38));

        // 两条外张的腿/手臂，形成原版那个绿色小符号。
        QPainterPath path;
        path.moveTo(cx - s * 0.30, top + s * 0.68);
        path.lineTo(cx, top + s * 0.48);
        path.lineTo(cx + s * 0.30, top + s * 0.68);
        painter.drawPath(path);
    };

    // 先画一层模糊的辉光，再画实心线条。
    drawShape(0.24, QColor(0x10, 0xff, 0x28, 55));
    drawShape(0.14, green);
}

// 清除键上的绿色 C。原版按键只有这个字母，直接用矢量画，避免字体差异。
void drawPasswordClearGlyph(QPainter &painter, const QRectF &keyRect)
{
    const qreal s = qMin(keyRect.width(), keyRect.height()) * 0.62;
    const QRectF box(keyRect.center().x() - s * 0.5,
                     keyRect.center().y() - s * 0.5, s, s);
    const QColor green(0x18, 0xff, 0x2a);

    auto drawC = [&](qreal width, const QColor &color) {
        painter.setPen(QPen(color, s * width, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        QPainterPath path;
        path.moveTo(box.left() + s * 0.78, box.top() + s * 0.22);
        path.lineTo(box.left() + s * 0.30, box.top() + s * 0.22);
        path.lineTo(box.left() + s * 0.30, box.top() + s * 0.78);
        path.lineTo(box.left() + s * 0.78, box.top() + s * 0.78);
        painter.drawPath(path);
    };

    drawC(0.22, QColor(0x10, 0xff, 0x28, 55));
    drawC(0.13, green);
}

} // namespace

QSize passwordPanelSize()
{
    return {360, 500};
}

QRectF passwordDisplayRect(const QSizeF &panelSize)
{
    const qreal w = panelSize.width();
    const qreal h = panelSize.height();
    return QRectF(w * 0.13, h * 0.065, w * 0.74, h * 0.145);
}

QRectF passwordKeyRect(int index, int columns, int keyCount,
                       const QSizeF &panelSize)
{
    if (columns < 1 || keyCount < 1 || index < 0 || index >= keyCount)
        return {};

    const int rows = (keyCount + columns - 1) / columns;
    const qreal w = panelSize.width();
    const qreal h = panelSize.height();
    const qreal left = w * 0.12;
    const qreal right = w * 0.12;
    const qreal top = h * 0.255;
    const qreal bottom = h * 0.07;
    const qreal gapX = w * 0.04;
    const qreal gapY = h * 0.028;
    const qreal keyW = (w - left - right - (columns - 1) * gapX) / columns;
    const qreal keyH = (h - top - bottom - (rows - 1) * gapY) / rows;

    const int row = index / columns;
    const int col = index % columns;
    return QRectF(left + col * (keyW + gapX),
                  top + row * (keyH + gapY), keyW, keyH);
}

QImage passwordPanelImage(const QStringList &keys, int columns,
                          const QString &input,
                          const QString &clearKey,
                          const QString &backspaceKey,
                          const QString &submitKey)
{
    const QSize size = passwordPanelSize();
    const QSizeF panelSize(size);
    const int keyCount = keys.size();
    const int cols = qMax(1, columns);

    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF panel(0.5, 0.5, size.width() - 1.0, size.height() - 1.0);

    // 参考原版的暖色木纹底板。
    QLinearGradient wood(0.0, 0.0, qreal(size.width()), 0.0);
    wood.setColorAt(0.00, QColor(0x7d, 0x45, 0x20));
    wood.setColorAt(0.22, QColor(0xb9, 0x74, 0x3b));
    wood.setColorAt(0.48, QColor(0xd0, 0x8b, 0x4c));
    wood.setColorAt(0.72, QColor(0xbc, 0x75, 0x38));
    wood.setColorAt(1.00, QColor(0x7a, 0x42, 0x1f));
    painter.fillRect(panel, wood);

    // 细木纹。使用固定公式而不是随机数，保证世界里和打开界面后的外观一致。
    painter.save();
    painter.setClipRect(panel);
    for (int i = 0; i < 28; ++i) {
        const qreal x = std::fmod(i * 47.0 + 9.0, qreal(size.width()));
        QPainterPath grain;
        grain.moveTo(x, 0.0);
        grain.cubicTo(x + 7.0, size.height() * 0.32,
                      x - 5.0, size.height() * 0.68,
                      x + 2.0, qreal(size.height()));
        painter.setPen(QPen(QColor(62, 31, 12, 20), 1.0));
        painter.drawPath(grain);
    }
    painter.restore();

    // 外框。
    painter.setPen(QPen(QColor(0x3b, 0x1d, 0x0c), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(panel.adjusted(1, 1, -1, -1));
    painter.setPen(QPen(QColor(0xf0, 0xbc, 0x7d, 70), 1));
    painter.drawLine(panel.topLeft() + QPointF(2, 2),
                     panel.topRight() + QPointF(-2, 2));

    // 顶部黑色显示屏。
    const QRectF display = passwordDisplayRect(panelSize);
    painter.setPen(QPen(QColor(0x1e, 0x0f, 0x07), 3));
    painter.setBrush(QColor(0x01, 0x02, 0x01));
    painter.drawRect(display);
    painter.setPen(QPen(QColor(0x00, 0x30, 0x10, 80), 1));
    painter.drawRect(display.adjusted(4, 4, -4, -4));

    // 每个输入字符画一个绿色符号；最多绘制能放进屏幕的数量。
    if (!input.isEmpty()) {
        const int count = input.size();
        qreal glyph = display.height() * 0.46;
        const qreal maxWidth = display.width() * 0.82;
        qreal total = count * glyph + (count - 1) * glyph * 0.35;
        if (total > maxWidth && total > 0.0) {
            glyph *= maxWidth / total;
            total = maxWidth;
        }
        const qreal spacing = glyph * 0.35;
        qreal x = display.center().x() - total * 0.5;
        const qreal y = display.center().y() - glyph * 0.5;
        for (int i = 0; i < count; ++i) {
            drawPasswordInputGlyph(painter, QRectF(x, y, glyph, glyph));
            x += glyph + spacing;
        }
    }

    // 按键。原版按键是纯黑方块、浅灰字母，动作键用绿色。
    QFont letterFont(QStringLiteral("Arial"));
    for (int i = 0; i < keyCount; ++i) {
        const QRectF rect = passwordKeyRect(i, cols, keyCount, panelSize);
        const QString &label = keys.at(i);
        // 动作键按标签精确匹配：密码器的 c 和清除键 C 在同一个面板上，
        // 不能忽略大小写。
        const bool clear = !clearKey.isEmpty() && label == clearKey;
        const bool backspace = !backspaceKey.isEmpty() && label == backspaceKey;
        const bool submit = !submitKey.isEmpty() && label == submitKey;
        const bool action = clear || backspace || submit;

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0x00, 0x00, 0x00, 90));
        painter.drawRect(rect.translated(2, 3));

        painter.setPen(QPen(QColor(0x1e, 0x12, 0x0a), 2));
        painter.setBrush(QColor(0x07, 0x08, 0x06));
        painter.drawRect(rect);

        painter.setPen(QPen(QColor(0x51, 0x30, 0x1c, 100), 1));
        painter.drawLine(rect.topLeft() + QPointF(2, 2),
                         rect.topRight() + QPointF(-2, 2));

        if (clear && label == QStringLiteral("C")) {
            drawPasswordClearGlyph(painter, rect);
        } else {
            QFont font = letterFont;
            font.setPixelSize(qMax(18, qRound(rect.height() * 0.48)));
            font.setWeight(action ? QFont::DemiBold : QFont::Normal);
            painter.setFont(font);
            painter.setPen(action ? QColor(0x18, 0xff, 0x2a)
                                  : QColor(0xdc, 0xdc, 0xd8));
            painter.drawText(rect, Qt::AlignCenter, label);
        }
    }

    return img;
}
