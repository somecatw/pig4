#ifndef ELEVATORART_H
#define ELEVATORART_H

#include <QImage>
#include <QString>
#include <QStringList>

// Elevator panel and call-button art. Both the world sprites and the 2D
// overlay use these images.
QImage elevatorPanelImage(const QStringList &reachableFloors,
                          const QString &currentFloor);
QImage elevatorCallButtonImage();

#endif // ELEVATORART_H
