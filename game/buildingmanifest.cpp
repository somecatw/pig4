#include "buildingmanifest.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDebug>

BuildingDef BuildingManifest::load(const QString &path)
{
    BuildingDef def;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "building manifest not found:" << path;
        return def;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "building manifest parse error:" << path << err.errorString();
        return def;
    }

    const QJsonObject root = doc.object();
    def.building = root.value("building").toString();
    def.scale = float(root.value("scale").toDouble(28.0));
    def.flipX = root.value("flipX").toBool(true);
    def.floorOffsetY = float(root.value("floorOffsetY").toDouble(-2.3328));
    def.anchorRoom = root.value("anchorRoom").toString();

    const QJsonArray rooms = root.value("rooms").toArray();
    for (const QJsonValue &v : rooms) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        RoomDef room;
        room.id = o.value("id").toString();
        room.label = o.value("label").toString();
        room.scene = o.value("scene").toString();
        room.markers = o.value("markers").toString();
        const QJsonArray cell = o.value("cell").toArray();
        if (cell.size() == 3) {
            room.cell[0] = cell[0].toInt();
            room.cell[1] = cell[1].toInt();
            room.cell[2] = cell[2].toInt();
        }
        room.yaw = float(o.value("yaw").toDouble(0.0));
        if (room.id.isEmpty() || room.scene.isEmpty()) {
            qWarning() << "building manifest: room missing id/scene, skipped";
            continue;
        }
        def.rooms.push_back(room);
    }

    const QJsonArray portals = root.value("portals").toArray();
    for (const QJsonValue &v : portals) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        PortalDef portal;
        portal.id = o.value("id").toString();
        portal.fromRoom = o.value("from").toString();
        portal.marker = o.value("marker").toString();
        portal.toRoom = o.value("to").toString();
        portal.kind = o.value("kind").toString();

        auto readVec3 = [](const QJsonValue &value, float out[3]) -> bool {
            const QJsonArray a = value.toArray();
            if (a.size() != 3) return false;
            for (int i = 0; i < 3; ++i) out[i] = float(a[i].toDouble());
            return true;
        };
        auto readEnd = [&](const QJsonValue &value, PortalEnd &end) -> bool {
            const QJsonObject o = value.toObject();
            end.valid = readVec3(o.value("pos"), end.pos)
                     && readVec3(o.value("lookAt"), end.lookAt);
            return end.valid;
        };

        const QJsonObject ends = o.value("ends").toObject();
        for (auto it = ends.constBegin(); it != ends.constEnd(); ++it) {
            PortalEnd end;
            if (readEnd(it.value(), end))
                portal.ends.insert(it.key(), end);
            else
                qWarning() << "building manifest: portal end needs pos/lookAt"
                           << portal.id << it.key();
        }

        // 兼容旧的单侧 enter：只给 to 侧一个落点。
        const QJsonObject enter = o.value("enter").toObject();
        if (portal.ends.isEmpty() && !enter.isEmpty()) {
            PortalEnd end;
            if (readEnd(enter, end))
                portal.ends.insert(portal.toRoom, end);
            else
                qWarning() << "building manifest: portal enter needs pos/lookAt" << portal.id;
        }

        if (!portal.fromRoom.isEmpty() && !portal.toRoom.isEmpty())
            def.portals.push_back(portal);
    }

    const QJsonArray locks = root.value("locks").toArray();
    for (const QJsonValue &v : locks) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        LockDef lock;
        lock.room = o.value("room").toString();
        lock.marker = o.value("marker").toString();
        lock.type = o.value("type").toString();
        lock.id = o.value("id").toString();
        lock.label = o.value("label").toString();
        lock.directRoom = o.value("directRoom").toString();
        if (!lock.room.isEmpty() && !lock.marker.isEmpty() && !lock.type.isEmpty())
            def.locks.push_back(lock);
        else
            qWarning() << "building manifest: lock missing room/marker/type, skipped";
    }

    const QJsonArray connections = root.value("connections").toArray();
    for (const QJsonValue &v : connections) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        ConnectionDef connection;
        connection.event = o.value("event").toString();
        connection.room = o.value("room").toString();
        connection.marker = o.value("marker").toString();
        connection.action = o.value("action").toString();
        if (!connection.event.isEmpty() && !connection.room.isEmpty()
            && !connection.marker.isEmpty() && !connection.action.isEmpty())
            def.connections.push_back(connection);
        else
            qWarning() << "building manifest: connection missing field, skipped";
    }

    const QJsonArray interactions = root.value("interactions").toArray();
    for (const QJsonValue &v : interactions) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        InteractionDef interaction;
        interaction.room = o.value("room").toString();
        interaction.marker = o.value("marker").toString();
        interaction.prompt = o.value("prompt").toString();
        interaction.notice = o.value("notice").toString();
        if (!interaction.room.isEmpty() && !interaction.marker.isEmpty())
            def.interactions.push_back(interaction);
        else
            qWarning() << "building manifest: interaction missing room/marker, skipped";
    }

    const QJsonArray clocks = root.value("clocks").toArray();
    for (const QJsonValue &v : clocks) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        ClockDef clock;
        clock.room = o.value("room").toString();
        clock.marker = o.value("marker").toString();
        clock.hour = o.value("hour").toInt(12);
        clock.minute = o.value("minute").toInt(0);
        clock.prompt = o.value("prompt").toString();
        clock.notice = o.value("notice").toString();
        if (!clock.room.isEmpty() && !clock.marker.isEmpty())
            def.clocks.push_back(clock);
        else
            qWarning() << "building manifest: clock missing room/marker, skipped";
    }

    const QJsonArray textSprites = root.value("textSprites").toArray();
    for (const QJsonValue &v : textSprites) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        TextSpriteDef sprite;
        sprite.room = o.value("room").toString();
        sprite.marker = o.value("marker").toString();
        sprite.text = o.value("text").toString();
        sprite.fontFamily = o.value("font").toString(QStringLiteral("Consolas"));
        sprite.pixelSize = o.value("pixelSize").toInt(44);
        if (o.value("color").isString())
            sprite.color = QColor(o.value("color").toString());
        if (o.value("background").isString())
            sprite.background = QColor(o.value("background").toString());
        sprite.width = o.value("width").toInt(1024);
        sprite.height = o.value("height").toInt(256);
        sprite.rotate = o.value("rotate").toInt(0);
        sprite.interactive = o.value("interactive").toBool(false);
        sprite.prompt = o.value("prompt").toString();
        sprite.notice = o.value("notice").toString();
        if (!sprite.room.isEmpty() && !sprite.marker.isEmpty() && !sprite.text.isEmpty())
            def.textSprites.push_back(sprite);
        else
            qWarning() << "building manifest: textSprite missing room/marker/text, skipped";
    }

    const QJsonArray pickups = root.value("pickups").toArray();
    for (const QJsonValue &v : pickups) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        PickupDef pickup;
        pickup.room = o.value("room").toString();
        pickup.marker = o.value("marker").toString();
        pickup.item = o.value("item").toString();
        pickup.name = o.value("name").toString();
        pickup.icon = o.value("icon").toString();
        if (!pickup.room.isEmpty() && !pickup.marker.isEmpty() && !pickup.item.isEmpty())
            def.pickups.push_back(pickup);
        else
            qWarning() << "building manifest: pickup missing room/marker/item, skipped";
    }

    const QJsonArray puzzles = root.value("puzzles").toArray();
    for (const QJsonValue &v : puzzles) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        PuzzleDef puzzle;
        puzzle.room = o.value("room").toString();
        puzzle.marker = o.value("marker").toString();
        puzzle.kind = o.value("kind").toString(QStringLiteral("keypad"));
        puzzle.interactive = o.value("interactive").toBool(true);
        puzzle.title = o.value("title").toString();
        puzzle.prompt = o.value("prompt").toString();
        puzzle.solution = o.value("solution").toString();
        puzzle.event = o.value("event").toString();
        puzzle.solvedText = o.value("solvedText").toString();
        puzzle.errorText = o.value("errorText").toString();
        puzzle.item = o.value("item").toString();
        puzzle.filled = o.value("filled").toBool(false);
        puzzle.columns = o.value("columns").toInt(3);
        if (puzzle.columns < 1)
            puzzle.columns = 1;
        puzzle.clearKey = o.value("clearKey").toString();
        puzzle.backspaceKey = o.value("backspaceKey").toString();
        puzzle.submitKey = o.value("submitKey").toString();
        const QJsonArray floors = o.value("floors").toArray();
        for (const QJsonValue &floorValue : floors) {
            const QString floor = floorValue.toString();
            if (!floor.isEmpty())
                puzzle.floors.push_back(floor);
        }
        const QJsonArray keys = o.value("keys").toArray();
        for (const QJsonValue &keyValue : keys) {
            const QString key = keyValue.toString();
            if (!key.isEmpty())
                puzzle.keys.push_back(key);
        }
        if (!puzzle.room.isEmpty() && !puzzle.marker.isEmpty())
            def.puzzles.push_back(puzzle);
        else
            qWarning() << "building manifest: puzzle missing room/marker, skipped";
    }

    return def;
}
