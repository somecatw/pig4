#include "itemicons.h"
#include "shardart.h"

#include <QDir>
#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>
#include <algorithm>
#include <cmath>

namespace
{
constexpr int IconSize = 256;
QString g_imageDir;

QImage transparentIcon()
{
    QImage img(IconSize, IconSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    return img;
}

void drawHighlight(QPainter &p, const QRectF &r, int alpha)
{
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, alpha));
    p.drawRoundedRect(r, r.height() * 0.5, r.height() * 0.5);
}

QImage loadImage(const QString &fileName)
{
    if (g_imageDir.isEmpty() || fileName.isEmpty())
        return {};
    return QImage(QDir(g_imageDir).filePath(fileName));
}

// 保留原图的明暗细节，只把灰度映射到金属色，比直接 SourceIn 上色更像实物。
QImage tintMetalImage(const QImage &base, const QColor &metal)
{
    const QImage src = base.convertToFormat(QImage::Format_ARGB32);
    QImage out(src.size(), QImage::Format_ARGB32);
    out.fill(Qt::transparent);

    const QColor dark = metal.darker(300);
    const QColor light = metal.lighter(145);
    for (int y = 0; y < src.height(); ++y) {
        for (int x = 0; x < src.width(); ++x) {
            const QRgb px = src.pixel(x, y);
            const int alpha = qAlpha(px);
            if (alpha == 0)
                continue;
            const float lum = std::clamp(qGray(px) / 255.0f, 0.0f, 1.0f);
            const float t = std::pow(lum, 1.05f);
            int r = int(dark.red()   + (light.red()   - dark.red())   * t);
            int g = int(dark.green() + (light.green() - dark.green()) * t);
            int b = int(dark.blue()  + (light.blue()  - dark.blue())  * t);
            // 只给最亮的一小段混一点白色，保留金属高光。
            if (lum > 0.78f) {
                const float spec = (lum - 0.78f) / 0.22f * 0.35f;
                r = int(r * (1.0f - spec) + 255 * spec);
                g = int(g * (1.0f - spec) + 255 * spec);
                b = int(b * (1.0f - spec) + 255 * spec);
            }
            out.setPixel(x, y, qRgba(r, g, b, alpha));
        }
    }
    return out;
}

QImage numberedKeyImage(int number)
{
    QImage base = loadImage(QStringLiteral("key_template.png"));
    if (base.isNull())
        return {};

    QImage img = transparentIcon();
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QImage scaled = base.scaledToHeight(IconSize - 18, Qt::SmoothTransformation);
    const int x = (IconSize - scaled.width()) / 2;
    const int y = (IconSize - scaled.height()) / 2;
    p.drawImage(x, y, scaled);

    // key_template.png 的钥匙头在顶部；数字叠在钥匙头中心。
    const qreal bowX = x + scaled.width() * 0.5;
    const qreal bowY = y + scaled.height() * (29.0 / 156.0);
    const qreal plateR = scaled.width() * 0.43;
    p.setPen(QPen(QColor(255, 240, 200, 190), 2));
    p.setBrush(QColor(45, 32, 12, 190));
    p.drawEllipse(QPointF(bowX, bowY), plateR, plateR);

    const QString text = QString::number(number);
    QFont font(QStringLiteral("Arial"), 42, QFont::Black);
    const QFontMetricsF fm(font);
    const QRectF textRect = fm.boundingRect(text);
    const QPointF baseline(bowX - textRect.width() / 2.0 - textRect.left(),
                           bowY + textRect.height() / 2.0 - textRect.bottom());

    QPainterPath textPath;
    textPath.addText(baseline, font, text);
    p.setPen(QPen(QColor(40, 28, 10), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(QColor(255, 244, 210));
    p.drawPath(textPath);

    return img;
}
}

namespace ItemIcons
{

void setImageDirectory(const QString &dir)
{
    g_imageDir = dir;
}

QImage imageIcon(const QString &fileName)
{
    return loadImage(fileName);
}

QColor silver() { return QColor(0xc9, 0xce, 0xd6); }
QColor bronze() { return QColor(0xc0, 0x7a, 0x45); }
QColor gold()   { return QColor(0xe8, 0xc1, 0x4a); }

QImage keyIcon(const QColor &metal, int number)
{
    if (number > 0) {
        const QImage numbered = numberedKeyImage(number);
        if (!numbered.isNull())
            return numbered;
    }

    const QImage base = loadImage(QStringLiteral("silver_key.png"));
    if (!base.isNull()) {
        if (metal == silver())
            return base;
        return tintMetalImage(base, metal);
    }

    // assets/images 缺失时回退到代码绘制的占位钥匙。
    QImage img = transparentIcon();
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 钥匙横放：左边是钥匙环，右边是齿。
    QPainterPath key;
    key.addEllipse(QRectF(28, 86, 90, 90));
    QPainterPath shaft;
    shaft.addRoundedRect(QRectF(110, 119, 124, 24), 12, 12);
    key = key.united(shaft);

    QPainterPath tooth1;
    tooth1.addRoundedRect(QRectF(191, 136, 18, 37), 5, 5);
    QPainterPath tooth2;
    tooth2.addRoundedRect(QRectF(217, 136, 15, 26), 4, 4);
    key = key.united(tooth1).united(tooth2);

    // 没有数字的小钥匙保留环孔；带数字的钥匙把数字压在实心钥匙头上。
    if (number <= 0) {
        QPainterPath hole;
        hole.addEllipse(QRectF(50, 108, 46, 46));
        key = key.subtracted(hole);
    }

    QLinearGradient fill(0, 86, 0, 178);
    fill.setColorAt(0.00, metal.lighter(170));
    fill.setColorAt(0.42, metal);
    fill.setColorAt(1.00, metal.darker(165));
    p.setPen(QPen(metal.darker(270), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(fill);
    p.drawPath(key);

    drawHighlight(p, QRectF(121, 122, 96, 7), 105);
    p.setPen(QPen(QColor(255, 255, 255, 100), 3));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(36, 94, 74, 74));

    if (number > 0) {
        QFont font(QStringLiteral("Arial"), 50, QFont::Black);
        const QString text = QString::number(number);
        const QFontMetricsF fm(font);
        const QRectF textRect = fm.boundingRect(text);
        const QPointF center(73, 131);
        const QPointF baseline(center.x() - textRect.width() / 2.0 - textRect.left(),
                               center.y() + textRect.height() / 2.0 - textRect.bottom());

        QPainterPath textPath;
        textPath.addText(baseline, font, text);
        p.setPen(QPen(QColor(35, 30, 20), 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(255, 246, 214));
        p.drawPath(textPath);
    }

    return img;
}

QImage moonBlockIcon()
{
    // 世界 sprite 和背包图标共用 assets/images/moon.png。
    const QImage shared = loadImage(QStringLiteral("moon.png"));
    if (!shared.isNull())
        return shared;

    QImage img = transparentIcon();
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath outer;
    outer.addEllipse(QRectF(32, 36, 184, 184));
    QPainterPath cut;
    cut.addEllipse(QRectF(104, 14, 184, 184));
    const QPainterPath moon = outer.subtracted(cut);

    QLinearGradient fill(50, 40, 200, 220);
    fill.setColorAt(0.00, QColor(0xff, 0xf4, 0xae));
    fill.setColorAt(0.45, QColor(0xff, 0xd7, 0x4f));
    fill.setColorAt(1.00, QColor(0xc9, 0x91, 0x12));
    p.setPen(QPen(QColor(91, 61, 8), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(fill);
    p.drawPath(moon);

    // 内缘高光，让它看起来像一块有厚度的积木。
    QPainterPath inner;
    inner.addEllipse(QRectF(48, 52, 152, 152));
    QPainterPath innerCut;
    innerCut.addEllipse(QRectF(114, 34, 152, 152));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 55));
    p.drawPath(inner.subtracted(innerCut));

    return img;
}

QImage pinkShardIcon()
{
    return ShardArt::pinkIcon(IconSize);
}

QImage triangleShardIcon(const QColor &color, int variant)
{
    QImage img = transparentIcon();
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(IconSize / 2.0, IconSize / 2.0);
    p.rotate((variant % 8) * 45.0);
    p.translate(-IconSize / 2.0, -IconSize / 2.0);

    QPainterPath shard;
    shard.moveTo(128, 26);
    shard.lineTo(218, 196);
    shard.lineTo(38, 196);
    shard.closeSubpath();

    QLinearGradient fill(70, 40, 190, 210);
    fill.setColorAt(0.00, color.lighter(165));
    fill.setColorAt(0.40, color);
    fill.setColorAt(1.00, color.darker(145));
    p.setPen(QPen(color.darker(300), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(fill);
    p.drawPath(shard);

    QPainterPath highlight;
    highlight.moveTo(128, 53);
    highlight.lineTo(184, 163);
    highlight.lineTo(96, 163);
    highlight.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 58));
    p.drawPath(highlight);

    return img;
}

QImage colorfulShardIcon()
{
    QImage img = transparentIcon();
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath shard;
    shard.moveTo(128, 26);
    shard.lineTo(218, 196);
    shard.lineTo(38, 196);
    shard.closeSubpath();

    QLinearGradient fill(38, 196, 218, 26);
    fill.setColorAt(0.00, QColor(0xe5, 0x6b, 0x9f));
    fill.setColorAt(0.22, QColor(0xf0, 0x8a, 0x3c));
    fill.setColorAt(0.42, QColor(0xf2, 0xd3, 0x3c));
    fill.setColorAt(0.62, QColor(0x4c, 0xc3, 0x6a));
    fill.setColorAt(0.80, QColor(0x55, 0xb7, 0xe8));
    fill.setColorAt(1.00, QColor(0x9b, 0x5d, 0xe5));
    p.setPen(QPen(QColor(28, 28, 34), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(fill);
    p.drawPath(shard);

    QPainterPath highlight;
    highlight.moveTo(128, 53);
    highlight.lineTo(184, 163);
    highlight.lineTo(96, 163);
    highlight.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 62));
    p.drawPath(highlight);

    return img;
}

} // namespace ItemIcons
