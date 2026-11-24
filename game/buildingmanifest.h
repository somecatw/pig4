#ifndef BUILDINGMANIFEST_H
#define BUILDINGMANIFEST_H

#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QList>

// 房间在建筑 grid 中的单元：col 沿 +X，row 沿 +Z，floor 沿 +Y。
// 房间的本地 AABB 会在加载后自动归一化到 cell 的 min 角，
// 所以这里不需要手写 room position；异形房间以后再特判。
struct RoomDef
{
    QString id;
    QString label;
    QString scene;
    QString markers;
    int cell[3] = {0, 0, 0};
    float yaw = 0.0f; // degrees，当前默认 0
};

// 窗户两侧各有一个落点/看向点，坐标使用该房间模型自身的 OBJ 源坐标。
// 同一扇窗可以从任一侧翻到另一侧，动画路径为 当前相机 -> 本侧 end -> 对侧 end。
struct PortalEnd
{
    bool valid = false;
    float pos[3] = {0.0f, 0.0f, 0.0f};
    float lookAt[3] = {0.0f, 0.0f, 0.0f};
};

struct PortalDef
{
    QString id;
    QString fromRoom;   // marker 所在房间
    QString marker;
    QString toRoom;
    QString kind;
    QHash<QString, PortalEnd> ends; // room id -> 该侧落点

    bool hasEnd(const QString &room) const
    {
        const auto it = ends.constFind(room);
        return it != ends.constEnd() && it.value().valid;
    }
};

// 门/窗锁配置放在 building.json，而不是每次重新导出的 markers JSON。
struct LockDef
{
    QString room;
    QString marker;
    QString type;   // "key" / "event"
    QString id;     // key_8 / silver_key；event 锁可留空
    QString label;  // 提示里显示的名字
    // 可选：只有站在这个房间里时，才能从“这一侧”直接打开反锁。
    // 其他房间仍然只显示普通钥匙提示。
    QString directRoom;
};

// 门的剧情/交互状态。state 2 之后的交互优先使用这里配置的 prompt/notice，
// 而不是普通钥匙锁逻辑；action 支持 none/unlock/lock/open/close/toggle。
struct DoorStateDef
{
    QString room;
    QString marker;
    int state = 0;
    QString prompt;
    QString notice;
    QString action = QStringLiteral("none");
};

// 事件到门动作的连接表。加载时由 DoorSystem 注册到 GameEvents。
struct ConnectionDef
{
    QString event;   // 例如 puzzle_303.solved
    QString room;
    QString marker;
    QString action;  // unlock / lock / open / close / toggle
};

// 通用 3D 交互物：通常是房间模型里的 HOT/TRIG 热区（钟表、假门等）。
// prompt 是准星提示（代码自动加 "[F] " 前缀），notice 是按 F 后底部显示的文本。
struct InteractionDef
{
    QString room;
    QString marker;
    QString prompt;
    QString notice;
    // 可选：<plotId>[:<moduleName>]，按 F 时播放，可替代 notice。
    QString story;
};

// 事件到剧情的连接表。例如 pickup.key_3 -> _5_MeetShadow:module_1。
// once 为 true 时，同一事件只触发一次剧情。
struct StoryTriggerDef
{
    QString event;
    QString story;
    bool once = true;
};

// 程序化绘制的钟表：PH_SPR marker + 表盘参数。
// prompt/notice 留空时使用“查看 时钟”和“时钟显示 HH:MM。”
struct ClockDef
{
    QString room;
    QString marker;
    int hour = 12;
    int minute = 0;
    QString prompt;
    QString notice;
};

// 程序化文字贴图：PH_SPR marker 没有导出 PNG 时，按参数画一张文字卡。
struct TextSpriteDef
{
    QString room;
    QString marker;
    QString text;
    QString fontFamily = QStringLiteral("Consolas");
    int pixelSize = 44;
    QColor color = QColor(43, 38, 32);
    QColor background = QColor(239, 231, 214);
    int width = 1024;
    int height = 256;
    int rotate = 0;   // 文字卡整体旋转角度，用于适配横向/纵向桌面 marker
    bool interactive = false;
    QString prompt;
    QString notice;
};

// 通用拾取配置：把 marker 绑到 inventory item。
// icon 支持 "key:1" 这种钥匙编号，或直接给 images/ 下的文件名。
struct PickupDef
{
    QString room;
    QString marker;
    QString item;
    QString name;
    QString icon;
};

// Puzzle 配置。Puzzle 本体是 SPR，interactive=false 时只作为可高亮线索。
struct PuzzleDef
{
    QString room;
    QString marker;
    QString kind = QStringLiteral("keypad");
    bool interactive = true;
    QString title;
    QString prompt;
    QString solution;   // keypad 密码；暂时留空表示任意输入都完成
    QString event;      // 完成后发布的 GameEvents 事件
    QString solvedText;
    QString errorText;  // password_keypad 输错时的提示
    QStringList floors; // elevator_panel 的楼层列表，例如 1F..6F
    QString item;       // 槽位 puzzle 接受/容纳的 item id，例如 shard_pink
    bool filled = false; // 槽位 puzzle 的初始状态

    // 字母密码器（letter_keypad / password_keypad）：
    // keys 按行优先排列；columns 是每行按键数；solution 的长度就是密码位数。
    // 特殊键是可选的，标签必须出现在 keys 里：
    //   clearKey     清空当前输入
    //   backspaceKey 删除一位
    //   submitKey    提交；不配置时输满 solution 长度自动校验
    QStringList keys;
    int columns = 3;
    QString clearKey;
    QString backspaceKey;
    QString submitKey;
};

struct BuildingDef
{
    QString building;
    float scale = 28.0f;
    bool flipX = true;
    float floorOffsetY = -2.3328f;
    QString anchorRoom;
    QList<RoomDef> rooms;
    QList<PortalDef> portals;
    QList<LockDef> locks;
    QList<DoorStateDef> doorStates;
    QList<ConnectionDef> connections;
    QList<StoryTriggerDef> storyTriggers;
    QList<InteractionDef> interactions;
    QList<ClockDef> clocks;
    QList<TextSpriteDef> textSprites;
    QList<PickupDef> pickups;
    QList<PuzzleDef> puzzles;
};

class BuildingManifest
{
public:
    static BuildingDef load(const QString &path);
};

#endif // BUILDINGMANIFEST_H
