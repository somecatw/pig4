#ifndef PASSWORDART_H
#define PASSWORDART_H

#include <QImage>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>

// PasswordKeypadView's programmatic panel art. Both the 2D overlay and the
// 3D sprite use the same image.
QImage passwordPanelImage(const QStringList &keys, int columns,
                          const QString &input,
                          const QString &clearKey = QString(),
                          const QString &backspaceKey = QString(),
                          const QString &submitKey = QString());
QSize passwordPanelSize();
QRectF passwordDisplayRect(const QSizeF &panelSize);
QRectF passwordKeyRect(int index, int columns, int keyCount,
                       const QSizeF &panelSize);

#endif // PASSWORDART_H
