#include "roommanager.h"
#include "clockart.h"
#include "itemicons.h"
#include "roommanifest.h"
#include "textart.h"
#include "door.h"
#include "elevator/elevatorart.h"
#include "password/passwordart.h"
#include "puzzle.h"
#include "shardart.h"
#include "stage3d.h"
#include "assetmanager.h"

#include <QColor>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QTransform>
#include <algorithm>
#include <cmath>

using namespace std;

static Vec3 rotateY(const Vec3 &v, float rad)
{
    const float c = std::cos(rad), s = std::sin(rad);
    return {v.x * c - v.z * s, v.y, v.x * s + v.z * c};
}

static bool objectLocalBounds(const GameObject *obj, Vec3 &lo, Vec3 &hi)
{
    bool any = false;
    obj->forEach([&](const MeshActor *a) {
        for (const Vertex &v : a->mesh.vertices) {
            if (!any) { lo = hi = v.pos; any = true; }
            lo.x = std::min(lo.x, v.pos.x);
            lo.y = std::min(lo.y, v.pos.y);
            lo.z = std::min(lo.z, v.pos.z);
            hi.x = std::max(hi.x, v.pos.x);
            hi.y = std::max(hi.y, v.pos.y);
            hi.z = std::max(hi.z, v.pos.z);
        }
    });
    return any;
}

static const ClockDef *findClock(const BuildingDef &building, const QString &roomId,
                                 const QString &marker)
{
    for (const ClockDef &clock : building.clocks) {
        if (clock.room == roomId && clock.marker == marker)
            return &clock;
    }
    return nullptr;
}

static const TextSpriteDef *findTextSprite(const BuildingDef &building,
                                           const QString &roomId,
                                           const QString &marker)
{
    for (const TextSpriteDef &sprite : building.textSprites) {
        if (sprite.room == roomId && sprite.marker == marker)
            return &sprite;
    }
    return nullptr;
}

static const PuzzleDef *findPuzzle(const BuildingDef &building,
                                   const QString &roomId,
                                   const QString &marker)
{
    for (const PuzzleDef &puzzle : building.puzzles) {
        if (puzzle.room == roomId && puzzle.marker == marker)
            return &puzzle;
    }
    return nullptr;
}

static const PickupDef *findPickup(const BuildingDef &building,
                                   const QString &roomId,
                                   const QString &marker)
{
    for (const PickupDef &pickup : building.pickups) {
        if (pickup.room == roomId && pickup.marker == marker)
            return &pickup;
    }
    return nullptr;
}

// 沿 dir 向房间几何打一条短射线，返回最近交点距离（source 空间）。
// 用来判断 sprite 贴墙的一侧：房间 AABB 中心可能落在隔墙另一侧（L 形
// 走廊等），只看中心会把 sprite 翻进墙里。
static float castRoomRay(const Room *room, const Vec3 &origin, const Vec3 &dir,
                         float maxDist)
{
    float nearest = maxDist;
    if (room == nullptr || room->sceneRoot == nullptr)
        return nearest;

    room->sceneRoot->forEach([&](const MeshActor *actor) {
        const Mesh &mesh = actor->mesh;
        for (const Triangle &tri : mesh.triangles) {
            const Vec3 &v0 = mesh.vertices[tri.vid[0]].pos;
            const Vec3 &v1 = mesh.vertices[tri.vid[1]].pos;
            const Vec3 &v2 = mesh.vertices[tri.vid[2]].pos;
            const Vec3 e1 = v1 - v0;
            const Vec3 e2 = v2 - v0;
            const Vec3 p = dir.cross(e2);
            const float det = e1.dot(p);
            if (std::abs(det) < 1e-6f)
                continue;
            const float invDet = 1.0f / det;
            const Vec3 tvec = origin - v0;
            const float u = tvec.dot(p) * invDet;
            if (u < 0.0f || u > 1.0f)
                continue;
            const Vec3 q = tvec.cross(e1);
            const float v = dir.dot(q) * invDet;
            if (v < 0.0f || u + v > 1.0f)
                continue;
            const float t = e2.dot(q) * invDet;
            if (t > 1e-4f && t < nearest)
                nearest = t;
        }
    });
    return nearest;
}

// SPR：生成 alpha-test 贴图四边形，位置/朝向来自 marker。
static MeshActor *createSpriteActor(Room *room, const RoomMarker &m,
                                    const QString &roomDir, float scale,
                                    Stage3D *stage, const Vec3 &roomCenter,
                                    const BuildingDef &building)
{
    QString texturePath = m.texture.isEmpty() ? QString()
                                              : QDir(roomDir).absoluteFilePath(m.texture);
    QImage img(texturePath);
    if (img.isNull() && !m.id.isEmpty()) {
        texturePath = QDir(roomDir).absoluteFilePath("sprites/" + m.id + ".png");
        img = QImage(texturePath);
    }
    if (img.isNull() && m.id.startsWith(QStringLiteral("elevator_panel"))) {
        // 电梯面板由代码绘制，不需要导出贴图；这里只用来创建 sprite 网格，
        // 具体到达/当前楼层颜色会在 Puzzle 初始化时再覆盖。
        img = elevatorPanelImage({}, QString());
    }
    if (img.isNull() && m.id.startsWith(QStringLiteral("call_elevator"))) {
        // 召梯按钮同样由代码绘制。
        img = elevatorCallButtonImage();
    }
    if (img.isNull() && m.id == QStringLiteral("puzzle_3W")) {
        // 粉色碎片槽也没有导出贴图；先用程序绘制的初始外观保证 sprite 存在，
        // 之后 ShardPinkPuzzle::applyWorldAppearance 会按实际的 filled 状态覆盖。
        img = ShardArt::pinkPanelImage(true);
    }
    if (img.isNull()) {
        // 字母密码器通常也没有单独导出贴图，按键面配置现场绘制。
        if (const PuzzleDef *puzzle = findPuzzle(building, room->def.id, m.id);
            puzzle != nullptr && isPasswordKeypadKind(puzzle->kind)) {
            img = passwordPanelImage(puzzle->keys, puzzle->columns, QString(),
                                     puzzle->clearKey, puzzle->backspaceKey,
                                     puzzle->submitKey);
        }
    }
    if (img.isNull()) {
        // 大堂钟表按 building.json 的 clocks 参数现场绘制。
        if (const ClockDef *clock = findClock(building, room->def.id, m.id))
            img = ClockArt::clockFace(clock->hour, clock->minute);
    }
    if (img.isNull()) {
        // 桌面文字提示等没有导出 PNG 的 SPR。
        if (const TextSpriteDef *sprite = findTextSprite(building, room->def.id, m.id)) {
            img = TextArt::textCard(sprite->text, sprite->fontFamily,
                                    sprite->pixelSize, sprite->color,
                                    sprite->background, sprite->width, sprite->height);
            if (sprite->rotate != 0)
                img = img.transformed(QTransform().rotate(sprite->rotate));
        }
    }
    if (img.isNull() && m.id == QStringLiteral("item_key1")) {
        // 1号钥匙先复用钥匙图标工厂，保证桌面 SPR 和背包图标一致。
        img = ItemIcons::keyIcon(ItemIcons::silver(), 1);
    }
    if (img.isNull()) {
        // Generic generated pickup sprite: lets a marker like PH_SPR_key3 use
        // the same icon factory as the inventory without exporting a PNG.
        if (const PickupDef *pickup = findPickup(building, room->def.id, m.id)) {
            if (pickup->icon.startsWith(QStringLiteral("key:"))) {
                bool ok = false;
                const int number = pickup->icon.mid(4).toInt(&ok);
                img = ItemIcons::keyIcon(ItemIcons::silver(), ok ? number : 0);
            } else if (!pickup->icon.isEmpty()) {
                img = ItemIcons::imageIcon(pickup->icon);
            } else if (!pickup->item.isEmpty()) {
                img = ItemIcons::imageIcon(pickup->item + QStringLiteral(".png"));
            }
        }
    }
    if (img.isNull()) {
        // 贴图还没导出的 SPR 先隐藏：不生成网格、不创建交互目标。
        // 已实现的系统（电梯面板、召梯键、粉色槽、字母密码器）在上面都有
        // 程序化 fallback。
        qInfo() << "hidden sprite (texture missing):" << m.name << m.texture;
        return nullptr;
    }

    const int materialID = assetManager.addMaterial(img);
    if (materialID < 0) return nullptr;

    float w = m.width > 1e-5f ? m.width : m.height;
    float h = m.height > 1e-5f ? m.height : m.width;
    if (w <= 1e-5f || h <= 1e-5f) {
        qWarning() << "sprite marker has no size:" << m.name;
        return nullptr;
    }
    // 保持道具图原始宽高比，避免原版竖版钥匙被标记框拉扁。
    if (img.height() > 0) {
        const float aspect = float(img.width()) / float(img.height());
        if (w / h > aspect)
            w = h * aspect;
        else
            h = w / aspect;
    }

    Mesh mesh;
    mesh.materialID = ushort(materialID);
    mesh.vertices = {
        {{-0.5f * w, -0.5f * h, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{ 0.5f * w, -0.5f * h, 0.0f}, {1.0f, 1.0f, 0.0f}},
        {{ 0.5f * w,  0.5f * h, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{-0.5f * w,  0.5f * h, 0.0f}, {0.0f, 0.0f, 0.0f}},
    };
    auto addTriangle = [&](uint a, uint b, uint c) {
        Triangle t;
        t.vid[0] = a;
        t.vid[1] = b;
        t.vid[2] = c;
        t.materialID = ushort(materialID);
        mesh.triangles.push_back(t);
    };
    addTriangle(0, 1, 2);
    addTriangle(0, 2, 3);

    const int meshID = stage->addRuntimeMesh(mesh);

    Vec3 basisX = m.basisX;
    Vec3 basisY = m.basisY;
    Vec3 basisZ = m.basisZ;
    // 朝向必须朝开阔的一侧。marker 贴墙时，沿 ±basisZ 各打一条短射线：
    // 近的一侧是墙，远的一侧才是房间。
    constexpr float probeDist = 2.5f;
    const float openPlus = castRoomRay(room, m.position, basisZ, probeDist);
    const float openMinus = castRoomRay(room, m.position, -basisZ, probeDist);
    const bool hasWallSide = std::abs(openPlus - openMinus) > 1e-3f;
    bool flip = openPlus < openMinus - 1e-3f;
    if (!hasWallSide) {
        // 两侧都开阔（或都顶死）时退回房间 AABB 中心的旧启发式。
        flip = (roomCenter - m.position).dot(basisZ) < 0.0f;
    }
    if (flip) {
        basisX = -basisX;
        basisZ = -basisZ;
    }

    // Puzzle view 是贴在墙上的操作面板：不按 marker 的 thickness 推出来，
    // 而是沿朝墙方向打射线，落到墙面后只往外抬一个很小的距离。这样面板
    // 不会悬在墙前，也不会和墙共面导致 z-fighting。其他 sprite（道具、纸张、
    // 时钟、文字卡等）继续沿用原来的 thickness 偏移。
    const bool puzzleView =
        findPuzzle(building, room->def.id, m.id) != nullptr;
    float puzzleWallDist = probeDist;
    if (puzzleView)
        puzzleWallDist = castRoomRay(room, m.position, -basisZ, probeDist);
    const bool snapPuzzleToWall =
        puzzleView && puzzleWallDist < probeDist - 1e-3f;

    MeshActor *actor = new MeshActor(meshID, false, room);

    actor->setScale(scale);
    Transform t;
    if (snapPuzzleToWall) {
        // 0.05 个模型源单位 ≈ scale=28 时的 1.4 世界单位，足够脱离墙面
        // 的深度精度，又不会在视觉上离开墙面。
        constexpr float puzzleWallOffset = 0.05f;
        const Vec3 wallPoint = m.position - basisZ * puzzleWallDist;
        t.translation = (wallPoint + basisZ * puzzleWallOffset) * scale;
    } else {
        // Box marker 的 position 在体积中心；道具卡要贴在朝向房间的那一面，
        // 否则会有一半埋在墙/洗手台里，被深度测试切掉。
        const float surfaceOffset = 0.5f * std::max(m.thickness, 0.0f) * scale;
        t.translation = m.position * scale + basisZ * surfaceOffset;
    }
    t.rotation = Mat3(basisX, basisY, basisZ);
    actor->setTransform(t);
    actor->shaderConfigOverride =  ShaderConfig::UseAlphaBitmap
                                   | ShaderConfig::DisableBackCulling;
                                // | ShaderConfig::DisableMipmap

    return actor;
}

struct DoorAssetCacheEntry {
    QList<uint> meshIDs;
};
static QHash<QString, DoorAssetCacheEntry> g_doorAssetCache;

static QList<uint> doorAssetMeshIDs(const QString &assetPath, Stage3D *stage)
{
    QString key = QFileInfo(assetPath).canonicalFilePath();
    if (key.isEmpty()) key = QFileInfo(assetPath).absoluteFilePath();
    auto it = g_doorAssetCache.constFind(key);
    if (it != g_doorAssetCache.constEnd()) return it.value().meshIDs;

    GameObject *tmpl = stage->loadObj(assetPath, false);
    if (tmpl == nullptr) return {};
    QList<uint> ids = tmpl->forEach([](const MeshActor *a) { return a->meshID; });
    delete tmpl;

    DoorAssetCacheEntry entry;
    entry.meshIDs = ids;
    g_doorAssetCache.insert(key, entry);
    return ids;
}

// 电梯门占位：灰色长方体，沿 marker 的 basisX 方向平移打开。
static Mesh grayDoorBox(float width, float height, float thickness, ushort materialID)
{
    Mesh mesh;
    mesh.materialID = materialID;

    const float x = width * 0.5f;
    const float y = height * 0.5f;
    const float z = thickness * 0.5f;
    mesh.vertices = {
        {{-x, -y, -z}, {0, 0, 0}}, {{ x, -y, -z}, {0, 0, 0}},
        {{ x,  y, -z}, {0, 0, 0}}, {{-x,  y, -z}, {0, 0, 0}},
        {{-x, -y,  z}, {0, 0, 0}}, {{ x, -y,  z}, {0, 0, 0}},
        {{ x,  y,  z}, {0, 0, 0}}, {{-x,  y,  z}, {0, 0, 0}},
    };
    const uint faces[6][4] = {
        {0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
        {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7},
    };
    for (const auto &f : faces) {
        Triangle a;
        a.vid[0] = f[0]; a.vid[1] = f[1]; a.vid[2] = f[2];
        a.materialID = materialID;
        Triangle b;
        b.vid[0] = f[0]; b.vid[1] = f[2]; b.vid[2] = f[3];
        b.materialID = materialID;
        mesh.triangles.push_back(a);
        mesh.triangles.push_back(b);
    }
    return mesh;
}

static Door *createSlidingDoorFromMarker(Room *room, const RoomMarker &m,
                                         float scale, Stage3D *stage)
{
    const float width = m.width > 1e-5f ? m.width : 15.0f;
    const float height = m.height > 1e-5f ? m.height : 30.0f;
    const float thickness = m.thickness > 1e-5f ? m.thickness : 2.0f;

    QImage gray(4, 4, QImage::Format_ARGB32);
    gray.fill(QColor(0x7d, 0x80, 0x84));
    const int materialID = assetManager.addMaterial(gray);
    if (materialID < 0)
        return nullptr;

    const int meshID = stage->addRuntimeMesh(
        grayDoorBox(width, height, thickness, ushort(materialID)));

    Door *door = new Door(room);
    door->setKind(DoorKind::Door);
    door->setIdentity(room->def.id, m.id);
    // 电梯门：任何开门动作（召梯键、面板、玩家按 F）都停 5 秒后自动关。
    door->setOpenDuration(5.0f);

    const Mat3 basis(m.basisX, m.basisY, m.basisZ);
    const Vec3 closed = m.position * scale;
    // 西侧滑开：沿门宽方向（basisX）平移一个门宽。
    const Vec3 open = closed + m.basisX * (width * scale);
    door->setSlide(closed, open, basis);

    auto *actor = new MeshActor(meshID, false, door);
    actor->setScale(scale);
    actor->setTransform(Transform());
    actor->shaderConfigOverride = ShaderConfig::DisableLightModel
                                | ShaderConfig::DisableBackCulling;

    const Transform roomWorld = room->getGlobalTransform();
    Transform colliderWorld;
    colliderWorld.translation = (m.position * scale) * roomWorld.rotation
                              + roomWorld.translation;
    colliderWorld.rotation = basis * roomWorld.rotation;
    door->setColliderVolume(colliderWorld, {
        0.5f * width * scale,
        0.5f * height * scale,
        0.5f * std::max(thickness, 0.2f) * scale,
    });
    return door;
}

// DOOR/WIN：全局样式资产按规范本地坐标系建模（X 宽 / Y 高 / Z 厚）。
static Door *createDoorFromMarker(Room *room, const RoomMarker &m,
                                  const QString &roomDir, float scale,
                                  Stage3D *stage, const Vec3 &roomCenter)
{
    if (m.asset.isEmpty()) {
        qWarning() << "door marker has no asset:" << m.name;
        return nullptr;
    }
    const QString assetPath = QDir::cleanPath(QDir(roomDir).absoluteFilePath(m.asset));
    const QList<uint> meshIDs = doorAssetMeshIDs(assetPath, stage);
    if (meshIDs.isEmpty()) {
        qWarning() << "door load failed:" << assetPath;
        return nullptr;
    }

    GameObject *doorMesh = new GameObject();
    for (uint meshID : meshIDs)
        new MeshActor(meshID, false, doorMesh);

    bool any = false;
    float minX = 1e9f, maxX = -1e9f;
    float minY = 1e9f, maxY = -1e9f;
    float minZ = 1e9f, maxZ = -1e9f;
    double sumX = 0.0;
    int vertexCount = 0;
    for (uint meshID : meshIDs) {
        const Mesh &mesh = assetManager.getMeshes().at(meshID);
        for (const Vertex &v : mesh.vertices) {
            const float px = v.pos.x;
            const float py = v.pos.y;
            const float pz = v.pos.z;
            if (!any) {
                minX = maxX = px;
                minY = maxY = py;
                minZ = maxZ = pz;
                any = true;
            }
            minX = std::min(minX, px); maxX = std::max(maxX, px);
            minY = std::min(minY, py); maxY = std::max(maxY, py);
            minZ = std::min(minZ, pz); maxZ = std::max(maxZ, pz);
            sumX += px;
            ++vertexCount;
        }
    }
    if (!any) {
        qWarning() << "door asset has no vertices:" << assetPath;
        delete doorMesh;
        return nullptr;
    }

    // 不再依赖 marker 的 L/R：门板左右对称，只有把手会多出一坨几何。
    // 顶点均值偏向把手一侧，因此另一边就是门轴。
    const float centerX = 0.5f * (minX + maxX);
    const float meanX = vertexCount > 0
        ? float(sumX / double(vertexCount)) : centerX;
    const bool rightHinge = meanX < centerX;
    qDebug() << "door hinge from centroid:" << QFileInfo(assetPath).fileName()
             << "meanX" << meanX << "centerX" << centerX
             << "hinge" << (rightHinge ? "right" : "left");

    const Vec3 assetHinge = {rightHinge ? maxX : minX,
                             minY,
                             0.5f * (minZ + maxZ)};
    const float leafWidth = m.width > 1e-5f ? m.width : (maxX - minX);
    const float leafHeight = m.height > 1e-5f ? m.height : (maxY - minY);
    const Vec3 markerHinge = m.position
                           + m.basisX * ((rightHinge ? 0.5f : -0.5f) * leafWidth)
                           + m.basisY * (-0.5f * leafHeight);

    const Vec3 inward = roomCenter - markerHinge;
    const Vec3 leafArm = m.position - markerHinge;
    const float openSign = rotateY(leafArm, 0.2f).dot(inward) >=
                           rotateY(leafArm, -0.2f).dot(inward) ? 1.0f : -1.0f;
    const float openAngle = std::max(std::abs(m.openAngle), 0.2f) * openSign;

    Door *door = new Door(room);
    door->setKind(m.type == "WIN" ? DoorKind::Window : DoorKind::Door);
    door->setIdentity(room->def.id, m.id);
    door->setHinge(markerHinge * scale, 0.0f, openAngle, 0.0f);

    const Mat3 basis(m.basisX, m.basisY, m.basisZ);
    const Transform toHinge = Transform::translate(-assetHinge * scale)
                            * Transform{{0.0f, 0.0f, 0.0f}, basis};
    doorMesh->setParent(door);
    doorMesh->forEach([&](MeshActor *a) {
        a->setScale(scale);
        a->setTransform(toHinge);
        a->shaderConfigOverride = ShaderConfig::DisableLightModel;
    });

    // 碰撞体固定在墙上的开口位置：门打开时禁用，窗户始终启用。
    const Transform roomWorld = room->getGlobalTransform();
    Transform colliderWorld;
    colliderWorld.translation = (m.position * scale) * roomWorld.rotation
                              + roomWorld.translation;
    colliderWorld.rotation = basis * roomWorld.rotation;
    const Vec3 half = {
        0.5f * leafWidth * scale,
        0.5f * leafHeight * scale,
        0.5f * std::max(m.thickness, 0.2f) * scale,
    };
    door->setColliderVolume(colliderWorld, half);
    return door;
}

RoomManager::RoomManager(Stage3D *stage)
    : m_stage(stage)
    , m_doorSystem(stage)
{
}

RoomManager::~RoomManager()
{
    qDeleteAll(m_puzzles);
    m_puzzles.clear();
    if (m_buildingRoot != nullptr) {
        delete m_buildingRoot;
        m_buildingRoot = nullptr;
    }
}

Room *RoomManager::room(const QString &id) const
{
    return m_rooms.value(id, nullptr);
}

const PortalDef *RoomManager::portalFor(const Room *room, const QString &marker) const
{
    if (room == nullptr) return nullptr;
    for (const PortalDef &portal : m_def.portals) {
        if (portal.fromRoom == room->def.id && portal.marker == marker)
            return &portal;
    }
    return nullptr;
}

Vec3 RoomManager::roomSourceToWorld(const Room *room, const Vec3 &sourcePos) const
{
    if (room == nullptr) return sourcePos;
    const Transform world = room->getGlobalTransform();
    return (sourcePos * m_def.scale) * world.rotation + world.translation;
}

Room *RoomManager::roomAt(const Vec3 &worldPos) const
{
    for (Room *room : m_roomOrder) {
        const Transform inv = room->getGlobalTransform().inverseTransform();
        const Vec3 local = (worldPos * inv.rotation + inv.translation) / m_def.scale;
        if (local.x >= room->localMin.x && local.x <= room->localMax.x &&
            local.y >= room->localMin.y && local.y <= room->localMax.y &&
            local.z >= room->localMin.z && local.z <= room->localMax.z)
            return room;
    }
    return nullptr;
}

void RoomManager::updateRenderRooms(const QString &currentRoomId)
{
    if (m_renderRoomId == currentRoomId)
        return;
    m_renderRoomId = currentRoomId;

    Room *current = room(currentRoomId);
    if (current == nullptr) {
        // 不在任何房间时不裁掉房间，避免相机越界/调试时黑屏。
        m_renderRoomIds.clear();
        for (Room *room : m_roomOrder) {
            room->setRenderVisible(true);
            m_renderRoomIds.push_back(room->def.id);
        }
        qWarning() << "render room fallback: current room not found:"
                   << currentRoomId;
        return;
    }

    QSet<QString> visible;
    visible.insert(current->def.id);
    const RoomDef &currentDef = current->def;
    for (Room *room : m_roomOrder) {
        if (room == current)
            continue;
        if (room->def.cell[2] != currentDef.cell[2])
            continue;
        if (room->def.cell[0] == currentDef.cell[0]
            || room->def.cell[1] == currentDef.cell[1]) {
            visible.insert(room->def.id);
        }
    }

    for (const VerticalLinkDef &link : m_def.verticalLinks) {
        if (link.lower == current->def.id)
            visible.insert(link.upper);
        else if (link.upper == current->def.id)
            visible.insert(link.lower);
    }

    m_renderRoomIds.clear();
    for (Room *room : m_roomOrder) {
        const bool enabled = visible.contains(room->def.id);
        room->setRenderVisible(enabled);
        if (enabled)
            m_renderRoomIds.push_back(room->def.id);
    }
    qDebug() << "render rooms for" << currentRoomId << ":" << m_renderRoomIds;
}

Room *RoomManager::loadRoomScene(const RoomDef &def)
{
    const QString scenePath = QDir(m_dir).absoluteFilePath(def.scene);
    GameObject *scene = m_stage->loadObj(scenePath, true);
    if (scene == nullptr) {
        qWarning() << "room load failed:" << scenePath;
        return nullptr;
    }

    Room *room = new Room(def, m_buildingRoot);
    scene->setParent(room);
    room->sceneRoot = scene;

    const float scale = m_def.scale;
    scene->forEach([scale](MeshActor *actor) {
        actor->setScale(scale);
        actor->shaderConfigOverride = ShaderConfig::DisableLightModel;
    });

    if (!computeBounds(room)) {
        qWarning() << "room has no mesh bounds:" << def.id;
    }

    // INTERACT_<id>：房间 OBJ 里的真实几何，既参与渲染，也作为可交互目标。
    // 同一 id 的多个材质 submesh 会合并进同一个 target，瞄准时一起高亮。
    const QString interactPrefix = QStringLiteral("INTERACT_");
    scene->forEach([&](MeshActor *actor) {
        const QString name = actor->objectName();
        if (!name.startsWith(interactPrefix, Qt::CaseInsensitive))
            return;
        const QString id = name.mid(interactPrefix.size());
        if (id.isEmpty())
            return;
        ensureInteractTarget(room, id).interactMeshes.push_back(actor);
        qDebug() << "interact object:" << def.id << id
                 << "mesh" << actor->meshID;
    });
    return room;
}

bool RoomManager::computeBounds(Room *room)
{
    if (room == nullptr || room->sceneRoot == nullptr) return false;
    const bool ok = objectLocalBounds(room->sceneRoot, room->localMin, room->localMax);
    if (ok) room->localCenter = (room->localMin + room->localMax) * 0.5f;
    return ok;
}

InteractTarget *RoomManager::findInteractableById(const QString &id)
{
    for (InteractTarget &target : m_interactables) {
        if (target.id == id)
            return &target;
    }
    return nullptr;
}

InteractTarget &RoomManager::ensureInteractTarget(Room *room, const QString &id)
{
    for (InteractTarget &target : m_interactables) {
        if (target.room == room && target.id == id)
            return target;
    }
    m_interactables.push_back(InteractTarget{});
    InteractTarget &target = m_interactables.last();
    target.room = room;
    target.id = id;
    return target;
}

void RoomManager::createRoomObjects(Room *room)
{
    if (room == nullptr) return;
    const QString roomDir = QFileInfo(QDir(m_dir).absoluteFilePath(room->def.scene)).absolutePath();
    const QString manifestPath = room->def.markers.isEmpty()
        ? QString()
        : QDir(m_dir).absoluteFilePath(room->def.markers);
    const QList<RoomMarker> markers = RoomManifest::load(manifestPath);
    const float scale = m_def.scale;
    const Vec3 roomCenter = room->localCenter;

    auto addHotspot = [&](const RoomMarker &m) {
        Hotspot *h = new Hotspot(room);
        h->id = m.id;
        h->center = m.position * scale;
        Vec3 half = m.halfSize;
        if (half.len() < 1e-5f)
            half = {0.5f * m.width, 0.5f * m.height, 0.5f * std::max(m.thickness, 0.2f)};
        h->halfSize = half * scale;
        h->setObjectName(m.name);
        Transform t;
        t.translation = h->center;
        t.rotation = Mat3(m.basisX, m.basisY, m.basisZ);
        h->setTransform(t);
        return h;
    };

    for (const RoomMarker &m : markers) {
        if (m.type == "SPR") {
            MeshActor *sprite = createSpriteActor(room, m, roomDir, scale, m_stage,
                                                  roomCenter, m_def);
            // 缺贴图的 SPR 按约定隐藏：createSpriteActor 返回 nullptr，
            // 这里不建 InteractTarget，玩家不会瞄到隐形交互物。
            if (sprite != nullptr) {
                const TextSpriteDef *textSprite = findTextSprite(m_def, room->def.id, m.id);
                // 纯文字提示默认只显示，不创建交互目标；需要交互时在
                // textSprites 里显式写 interactive: true。
                if (textSprite == nullptr || textSprite->interactive) {
                    InteractTarget &target = ensureInteractTarget(room, m.id);
                    target.sprite = sprite;
                    if (const ClockDef *clock = findClock(m_def, room->def.id, m.id)) {
                        int displayHour = clock->hour % 12;
                        if (displayHour <= 0)
                            displayHour += 12;
                        const int displayMinute = ((clock->minute % 60) + 60) % 60;
                        target.prompt = clock->prompt.isEmpty()
                            ? QStringLiteral("查看 时钟") : clock->prompt;
                        target.notice = clock->notice.isEmpty()
                            ? QStringLiteral("时钟显示 %1:%2。")
                                  .arg(displayHour)
                                  .arg(displayMinute, 2, 10, QLatin1Char('0'))
                            : clock->notice;
                        qDebug() << "clock:" << m.id
                                 << "source" << m.position.to_string().c_str()
                                 << "world" << roomSourceToWorld(room, m.position).to_string().c_str()
                                 << "time" << clock->hour << clock->minute;
                    } else if (textSprite != nullptr) {
                        target.prompt = textSprite->prompt.isEmpty()
                            ? QStringLiteral("查看 提示") : textSprite->prompt;
                        target.notice = textSprite->notice.isEmpty()
                            ? textSprite->text : textSprite->notice;
                    }
                }
            }
        } else if (m.type == "HOT" || m.type == "TRIG") {
            Hotspot *hotspot = addHotspot(m);
            ensureInteractTarget(room, m.id).hotspot = hotspot;
        } else if (m.type == "DOOR" || m.type == "WIN") {
            const bool elevatorDoor = m.type == "DOOR"
                && (m.id.contains(QStringLiteral("elevator"), Qt::CaseInsensitive)
                    || m.name.contains(QStringLiteral("elevator"), Qt::CaseInsensitive));
            Door *door = elevatorDoor
                ? createSlidingDoorFromMarker(room, m, scale, m_stage)
                : createDoorFromMarker(room, m, roomDir, scale, m_stage, roomCenter);
            if (door != nullptr) {
                m_doorSystem.addDoor(door);
                // 电梯门完全由召梯键/面板控制，不提供玩家直接开关的 F 交互。
                if (!elevatorDoor) {
                    InteractTarget &target = ensureInteractTarget(room, m.id);
                    target.door = door;
                    // 门板打开后会移开，门口的固定 hotspot 继续负责关窗/翻窗。
                    target.hotspot = addHotspot(m);
                }
            } else if (!elevatorDoor) {
                qWarning() << "door marker degraded to hotspot:" << m.name;
                Hotspot *hotspot = addHotspot(m);
                ensureInteractTarget(room, m.id).hotspot = hotspot;
            }
        } else {
            qWarning() << "unknown marker type:" << m.type << m.name;
        }
    }
}

bool RoomManager::loadBuilding(const QString &path)
{
    m_def = BuildingManifest::load(path);
    m_dir = QFileInfo(path).absolutePath();
    if (m_def.rooms.isEmpty()) {
        qWarning() << "building has no rooms:" << path;
        return false;
    }

    m_buildingRoot = new GameObject(m_stage->root);
    Transform buildingT;
    if (m_def.flipX)
        buildingT = Transform::rotateAroundAxis({1, 0, 0}, 3.1415926f);
    buildingT = buildingT * Transform::translate({0.0f, m_def.floorOffsetY, 0.0f});
    m_buildingRoot->setTransform(buildingT);

    for (const RoomDef &def : m_def.rooms) {
        Room *room = loadRoomScene(def);
        if (room == nullptr) continue;
        m_rooms.insert(def.id, room);
        m_roomOrder.push_back(room);
    }
    if (m_roomOrder.isEmpty()) return false;

    // grid cell 尺寸取所有房间非 PH AABB 的各轴最大值。
    Vec3 cellSize;
    for (Room *room : m_roomOrder) {
        const Vec3 size = room->localMax - room->localMin;
        cellSize.x = std::max(cellSize.x, size.x);
        cellSize.y = std::max(cellSize.y, size.y);
        cellSize.z = std::max(cellSize.z, size.z);
    }

    Room *anchor = room(m_def.anchorRoom);
    if (anchor == nullptr) anchor = m_roomOrder.first();
    const Vec3 anchorMin = anchor->localMin;
    const int *anchorCell = anchor->def.cell;

    // 房间本地 AABB min 对齐到 cell 的 min 角；anchor 房间保持原来的世界位置。
    for (Room *room : m_roomOrder) {
        const RoomDef &def = room->def;
        const Vec3 cellOrigin = {
            (def.cell[0] - anchorCell[0]) * cellSize.x,
            (def.cell[2] - anchorCell[2]) * cellSize.y,
            (def.cell[1] - anchorCell[1]) * cellSize.z,
        };
        const Vec3 localDelta = room->localMin - anchorMin;
        const Vec3 placement = (cellOrigin - localDelta) * m_def.scale;
        const float yaw = def.yaw * 3.1415926f / 180.0f;
        room->setTransform(Transform::rotateAroundAxis({0, 1, 0}, yaw)
                         * Transform::translate(placement));
    }

    for (Room *room : m_roomOrder) {
        const Vec3 wmin = roomSourceToWorld(room, room->localMin);
        const Vec3 wmax = roomSourceToWorld(room, room->localMax);
        qDebug() << "room world bounds:" << room->def.id
                 << wmin.to_string().c_str() << wmax.to_string().c_str();
    }

    for (Room *room : m_roomOrder)
        createRoomObjects(room);

    // 通用 3D 交互物：把配置里的提示/文本挂到已有的 HOT/TRIG/SPR target。
    for (const InteractionDef &def : m_def.interactions) {
        Room *interactionRoom = room(def.room);
        InteractTarget *target = nullptr;
        if (interactionRoom != nullptr) {
            for (InteractTarget &candidate : m_interactables) {
                if (candidate.room == interactionRoom && candidate.id == def.marker) {
                    target = &candidate;
                    break;
                }
            }
        }
        if (target == nullptr) {
            qWarning() << "interaction target not found:" << def.room << def.marker;
            continue;
        }
        target->prompt = def.prompt;
        target->notice = def.notice;
        target->story = def.story;
        qDebug() << "interaction:" << def.room << def.marker
                 << "prompt" << def.prompt
                 << "notice" << def.notice
                 << "story" << def.story;
    }

    // 通用拾取配置：把 marker 绑定到 inventory item。
    for (const PickupDef &def : m_def.pickups) {
        Room *pickupRoom = room(def.room);
        InteractTarget *target = nullptr;
        if (pickupRoom != nullptr) {
            for (InteractTarget &candidate : m_interactables) {
                if (candidate.room == pickupRoom && candidate.id == def.marker) {
                    target = &candidate;
                    break;
                }
            }
        }
        if (target == nullptr) {
            qWarning() << "pickup target not found:" << def.room << def.marker;
            continue;
        }
        target->pickupItem = def.item;
        target->pickupName = def.name;
        target->pickupIcon = def.icon;
        qDebug() << "pickup:" << def.room << def.marker << "->" << def.item;
    }

    // Puzzle 只是给现有 SPR target 附加配置和状态，不额外生成场景对象。
    for (const PuzzleDef &def : m_def.puzzles) {
        Room *puzzleRoom = room(def.room);
        InteractTarget *target = nullptr;
        if (puzzleRoom != nullptr) {
            for (InteractTarget &candidate : m_interactables) {
                if (candidate.room == puzzleRoom && candidate.id == def.marker) {
                    target = &candidate;
                    break;
                }
            }
        }
        if (target == nullptr) {
            qWarning() << "puzzle target not found:" << def.room << def.marker;
            continue;
        }

        Puzzle *puzzle = createPuzzle(def, target->sprite);
        if (puzzle == nullptr)
            continue;

        // 具体 puzzle 自己决定 3D 世界里长什么样。
        puzzle->applyWorldAppearance(target->sprite);

        target->puzzle = puzzle;
        m_puzzles.push_back(puzzle);
        qDebug() << "puzzle:" << def.room << def.marker
                 << "kind" << def.kind
                 << "interactive" << def.interactive;
    }

    // portal 挂到门/窗实体上，交互时由 DoorSystem 决定是否翻窗/进入。
    for (const PortalDef &portal : m_def.portals) {
        Door *door = m_doorSystem.findDoor(portal.fromRoom, portal.marker);
        if (door == nullptr) {
            qWarning() << "portal marker not found:" << portal.fromRoom << portal.marker;
            continue;
        }
        door->setPortalId(portal.id);
    }

    for (const LockDef &lock : m_def.locks)
        m_doorSystem.applyLock(lock);
    m_doorSystem.bindStates(m_def.doorStates);
    m_doorSystem.bindConnections(m_def.connections, m_events);

    qDebug() << "room grid cell size:" << cellSize.x << cellSize.y << cellSize.z
             << "rooms:" << m_roomOrder.size();
    return true;
}
