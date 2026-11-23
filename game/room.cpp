#include "room.h"

Room::Room(const RoomDef &_def, QObject *parent)
    : GameObject(parent), def(_def)
{
}
