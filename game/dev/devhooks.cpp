#include "devhooks.h"

#include "door.h"
#include "doorsystem.h"
#include "gameobject.h"
#include "itemcatalog.h"
#include "itemicons.h"
#include "itemui.h"
#include "mainwindow.h"
#include "mathbase.h"
#include "password/passwordkeypadview.h"
#include "puzzle.h"
#include "puzzleui.h"
#include "room.h"
#include "roommanager.h"
#include "stage3d.h"
#include "storyui.h"
#include "transform.h"

#include <QColor>
#include <QCoreApplication>
#include <QDebug>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>

namespace {

bool parseVec3Env(const char *name, Vec3 &out)
{
    const QString s = qEnvironmentVariable(name);
    if (s.isEmpty()) return false;
    const QStringList parts = s.split(',');
    if (parts.size() != 3) return false;
    bool okX = false, okY = false, okZ = false;
    const float x = parts[0].toFloat(&okX);
    const float y = parts[1].toFloat(&okY);
    const float z = parts[2].toFloat(&okZ);
    if (!okX || !okY || !okZ) return false;
    out = {x, y, z};
    return true;
}

Transform lookAt(const Vec3 &pos, const Vec3 &target)
{
    Vec3 axisZ = (target - pos).normalized();
    Vec3 up = {0, -1, 0}; // 世界 Y 向下
    Vec3 axisX = axisZ.cross(up);
    if (axisX.len() < 1e-5f) axisX = {1, 0, 0};
    axisX.normalize();
    Vec3 axisY = axisZ.cross(axisX).normalized();
    return {pos, Mat3(axisX, axisY, axisZ)};
}

} // namespace

bool DevHooks::loadDebugScene(MainWindow &window)
{
    if (!qEnvironmentVariableIsSet("PIG4_DUST2"))
        return false;

    GameObject *ttfa = window.stage->loadObj("..\\..\\assets\\dust2\\de_dust2.obj", true);
    if (ttfa == nullptr) {
        qWarning() << "PIG4_DUST2 set but de_dust2.obj could not be loaded";
        return false;
    }

    ttfa->forEach([](MeshActor *curr) { curr->setScale(8.0f); });
    ttfa->setParent(window.stage->root);
    window.camera->translate({-2000, -1250, -7500});

    if (GameObject *ak = window.stage->loadObj("..\\..\\assets\\ak.obj")) {
        ak->forEach([](MeshActor *curr) { curr->setScale(0.25f); });
        ak->setParent(window.camera);
        ak->setTransform(Transform::rotateAroundAxis({0, 1, 0}, 1.57)
                         * Transform::rotateAroundAxis({0, 0, 1}, -1.57));
        ak->translate({75, 37.5, 175});
    }

    const Transform rot = Transform::rotateAroundAxis({1, 0, 0}, -1.57)
                        * Transform::translate({0, 0, -400});
    ttfa->setTransform(rot);
    return true;
}

void DevHooks::startup(MainWindow &window)
{
    if (window.dialogueBar != nullptr) {
        const QString dialogueTest = qEnvironmentVariable("PIG4_DIALOGUE_TEST");
        if (dialogueTest == QStringLiteral("rich")) {
            window.dialogueBar->showNotice(
                QStringLiteral("锁住了，需要 <span style=\"color:#38c838\">“8号钥匙”</span>"),
                3000);
        } else if (!dialogueTest.isEmpty()) {
            window.dialogueBar->showLine(
                QStringLiteral("丹"),
                QStringLiteral("太好了，电梯能用，去别的楼层看看吧。"), 0);
        }
    }

    if (window.roomManager != nullptr
        && !qEnvironmentVariableIsEmpty("PIG4_DOOR_OPEN")) {
        window.roomManager->doorSystem()->forceOpenAll();
    }

    // PIG4_GIVE=silver_key 直接给物品，方便不跑拾取流程测试上锁的门窗。
    const QString giveItem = qEnvironmentVariable("PIG4_GIVE");
    if (window.roomManager != nullptr && giveItem == "silver_key") {
        window.addItem(QStringLiteral("silver_key"), QStringLiteral("银质小钥匙"),
                       ItemIcons::keyIcon(ItemIcons::silver()));
    } else if (window.roomManager != nullptr && giveItem == "combine") {
        window.addItem(QStringLiteral("remote"), QStringLiteral("遥控器"),
                       ItemIcons::imageIcon(QStringLiteral("remote.png")));
        window.addItem(QStringLiteral("battery"), QStringLiteral("电池"),
                       ItemIcons::imageIcon(QStringLiteral("battery.png")));
    } else if (window.roomManager != nullptr && giveItem == "icons") {
        // 图标工厂预览：12 格刚好塞满右侧背包。
        window.addItem(QStringLiteral("bronze_key"), QStringLiteral("铜质小钥匙"),
                       ItemIcons::keyIcon(ItemIcons::bronze()));
        window.addItem(QStringLiteral("gold_key"), QStringLiteral("金质小钥匙"),
                       ItemIcons::keyIcon(ItemIcons::gold()));
        window.addItem(QStringLiteral("key_5"), QStringLiteral("5号钥匙"),
                       ItemIcons::keyIcon(ItemIcons::silver(), 5));
        window.addItem(QStringLiteral("moon_block"), QStringLiteral("月亮积木"),
                       ItemIcons::moonBlockIcon());

        struct ShardPreview { const char *id; const char *name; QColor color; };
        const ShardPreview shards[] = {
            {"shard_pink",   "粉色碎片", QColor(0xf0, 0x6e, 0xa9)},
            {"shard_orange", "橙色碎片", QColor(0xf0, 0x8a, 0x3c)},
            {"shard_cyan",   "青色碎片", QColor(0x35, 0xc6, 0xc6)},
            {"shard_blue",   "蓝色碎片", QColor(0x3b, 0x6e, 0xe0)},
            {"shard_red",    "红色碎片", QColor(0xd9, 0x4a, 0x4a)},
            {"shard_yellow", "黄色碎片", QColor(0xf2, 0xd3, 0x3c)},
            {"shard_purple", "紫色碎片", QColor(0x9b, 0x5d, 0xe5)},
            {"shard_green",  "绿色碎片", QColor(0x4c, 0xc3, 0x6a)},
        };
        for (int i = 0; i < int(sizeof(shards) / sizeof(shards[0])); ++i) {
            const bool pink = QString::fromLatin1(shards[i].id)
                == QStringLiteral("shard_pink");
            window.addItem(QString::fromLatin1(shards[i].id),
                           QString::fromUtf8(shards[i].name),
                           pink ? ItemIcons::pinkShardIcon()
                                : ItemIcons::triangleShardIcon(shards[i].color, i));
        }
        window.inventoryIndex = 0;
        window.refreshInventoryUi();
    }

    // PIG4_ADD_ITEM=key_8 / silver_key
    const QString extraItem = qEnvironmentVariable("PIG4_ADD_ITEM");
    if (window.roomManager != nullptr && extraItem == QStringLiteral("key_8")) {
        window.addItem(QStringLiteral("key_8"), QStringLiteral("8号钥匙"),
                       ItemIcons::keyIcon(ItemIcons::silver(), 8));
    } else if (window.roomManager != nullptr && extraItem == QStringLiteral("silver_key")) {
        window.addItem(QStringLiteral("silver_key"), QStringLiteral("银质小钥匙"),
                       ItemIcons::keyIcon(ItemIcons::silver()));
    }

    // PIG4_SOLVE=puzzle_303.solved
    const QString solveEvent = qEnvironmentVariable("PIG4_SOLVE");
    if (window.roomManager != nullptr && !solveEvent.isEmpty())
        window.roomManager->events().publish(solveEvent);

    // 调试出生点：PIG4_CAM_POS/TARGET 优先；PIG4_SPAWN 切换 301/303。
    Vec3 camPos, camTarget;
    const bool hasCamPos = parseVec3Env("PIG4_CAM_POS", camPos);
    const bool hasCamTarget = parseVec3Env("PIG4_CAM_TARGET", camTarget);
    if (hasCamPos && hasCamTarget) {
        const Transform t = lookAt(camPos, camTarget);
        window.camera->setTransform(t);
        window.camera->updatePosition(t);
    } else if (qEnvironmentVariable("PIG4_SPAWN", "303") != "303") {
        const Transform t = lookAt({3809.3f, -498.7f, -560.0f},
                                   {1943.2f, -422.3f, 756.0f});
        window.camera->setTransform(t);
        window.camera->updatePosition(t);
    }

    if (qEnvironmentVariableIsSet("PIG4_DEBUG_OVERLAY")) {
        window.debugOverlay->show();
        window.updateDebugOverlay();
    }

    // PIG4_OPEN_PUZZLE=puzzle_303 直接打开某个 puzzle 的操作界面。
    const QString openPuzzleTest = qEnvironmentVariable("PIG4_OPEN_PUZZLE");
    if (!openPuzzleTest.isEmpty() && window.roomManager != nullptr) {
        for (InteractTarget &target : window.roomManager->interactables()) {
            if (target.id == openPuzzleTest && target.puzzle != nullptr) {
                window.openPuzzle(target.puzzle);
                break;
            }
        }
    }

    // PIG4_OPEN_ITEM=moon_block 直接打开道具查看界面。
    const QString openItemTest = qEnvironmentVariable("PIG4_OPEN_ITEM");
    if (!openItemTest.isEmpty()) {
        int index = -1;
        for (int i = 0; i < window.inventory.size(); ++i) {
            if (window.inventory[i].id == openItemTest) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            if (const ItemDef *def = window.itemCatalog.find(openItemTest)) {
                const QImage icon = def->views.isEmpty()
                    ? QImage() : def->views.first().image;
                window.addItem(openItemTest,
                               def->name.isEmpty() ? openItemTest : def->name,
                               icon);
            }
        } else {
            window.inventoryIndex = index;
            window.refreshInventoryUi();
        }
        window.openItemDetail();

        const int itemViewTest = qEnvironmentVariableIntValue("PIG4_ITEM_VIEW");
        for (int i = 0; i < itemViewTest; ++i)
            window.itemDetailView->nextView();

        const QString itemDropTest = qEnvironmentVariable("PIG4_ITEM_DROP");
        if (!itemDropTest.isEmpty())
            window.itemDetailView->handleItemDrop(itemDropTest);

        const QString itemCombineTest = qEnvironmentVariable("PIG4_ITEM_COMBINE");
        if (!itemCombineTest.isEmpty())
            window.combineItem(itemCombineTest);
    }

    // PIG4_STORY=opening 或 _4_KaiChangBai:module_1 启动剧情界面。
    const QString storyTest = qEnvironmentVariable("PIG4_STORY");
    if (!storyTest.isEmpty()) {
        QTimer::singleShot(0, &window, [&window, storyTest] {
            if (window.stageOverlay == nullptr || window.stageOverlay->hasContent())
                return;
            window.playStoryRequest(storyTest);
        });
    }
}

void DevHooks::updateOverlay(MainWindow &window)
{
    // PIG4_PUZZLE_KEY=9132 逐帧模拟点击数字键。
    static const QString puzzleTestKeys = qEnvironmentVariable("PIG4_PUZZLE_KEY");
    static bool puzzleTestKeySent = false;
    auto *puzzleView = qobject_cast<PuzzleView *>(window.stageOverlay->content());
    if (!puzzleTestKeySent && !puzzleTestKeys.isEmpty() && puzzleView != nullptr) {
        puzzleTestKeySent = true;
        for (QChar ch : puzzleTestKeys) {
            if (ch.isDigit())
                puzzleView->activateKey(ch.digitValue());
        }
    }

    // PIG4_PUZZLE_TEXT=retaw 逐键输入字母密码器。
    static const QString puzzleTestText = qEnvironmentVariable("PIG4_PUZZLE_TEXT");
    static bool puzzleTestTextSent = false;
    if (!puzzleTestTextSent && !puzzleTestText.isEmpty()) {
        if (auto *passwordView = qobject_cast<PasswordKeypadView *>(puzzleView)) {
            puzzleTestTextSent = true;
            for (QChar ch : puzzleTestText)
                passwordView->activateKeyLabel(QString(ch));
        }
    }

    // PIG4_PUZZLE_TAKE=1 + PIG4_PUZZLE_DROP=shard_pink
    static const QString shardTakeTest = qEnvironmentVariable("PIG4_PUZZLE_TAKE");
    static const QString shardDropTest = qEnvironmentVariable("PIG4_PUZZLE_DROP");
    static bool shardTestDone = false;
    if (!shardTestDone && (!shardTakeTest.isEmpty() || !shardDropTest.isEmpty())) {
        if (auto *shard = qobject_cast<ShardPinkPuzzle *>(window.activePuzzle)) {
            shardTestDone = true;
            if (!shardTakeTest.isEmpty() && shardTakeTest != QStringLiteral("0"))
                shard->takeItem();
            if (!shardDropTest.isEmpty())
                shard->insertItem(shardDropTest);
        }
    }

    // PIG4_STORY_ADVANCE=n / PIG4_STORY_SKIP=1
    static const int storyAdvanceTest =
        qEnvironmentVariableIntValue("PIG4_STORY_ADVANCE");
    static const bool storySkipTest =
        qEnvironmentVariableIsSet("PIG4_STORY_SKIP");
    static bool storyTestDone = false;
    if (!storyTestDone && (storyAdvanceTest > 0 || storySkipTest)) {
        if (auto *story = qobject_cast<StoryView *>(window.stageOverlay->content())) {
            storyTestDone = true;
            for (int i = 0; i < storyAdvanceTest; ++i)
                story->advance();
            if (storySkipTest) {
                if (auto *button = story->findChild<QPushButton *>(
                        QStringLiteral("storySkip"))) {
                    button->click();
                } else {
                    story->skip();
                }
            }
        }
    }

    // PIG4_ITEM_CLICK=1 点击道具内容区，验证纸团展开/月亮翻面。
    static const bool itemClickTest =
        qEnvironmentVariableIsSet("PIG4_ITEM_CLICK");
    static bool itemClickDone = false;
    if (!itemClickDone && itemClickTest) {
        if (auto *item = qobject_cast<ItemDetailView *>(window.stageOverlay->content())) {
            itemClickDone = true;
            if (ItemViewContent *content = item->viewContent()) {
                const QPointF point(content->width() * 0.35,
                                    content->height() * 0.5);
                QMouseEvent click(QEvent::MouseButtonPress, point, point,
                                  Qt::LeftButton, Qt::LeftButton,
                                  Qt::NoModifier);
                QCoreApplication::sendEvent(content, &click);
            }
        }
    }
}

void DevHooks::updateGameplay(MainWindow &window)
{
    if (qEnvironmentVariableIsSet("PIG4_LOG_CAMERA")) {
        static int camLogFrame = 0;
        if (camLogFrame++ % 30 == 0)
            qDebug() << "camera" << window.camera->camInfo.pos.to_string().c_str();
    }

    // 检查准星提示的 rich text 渲染。
    static const bool richPromptTest =
        qEnvironmentVariableIsSet("PIG4_RICH_PROMPT_TEST");
    if (richPromptTest) {
        window.stage->interactionPrompt =
            QStringLiteral("锁住了，需要 <span style=\"color:#38c838\">“8号钥匙”</span>");
        window.stage->interactionHover = true;
    }

    // 等准星第一次命中交互目标后，逐帧注入 PIG4_TEST_INPUT 里的按键。
    static const QString testInput = qEnvironmentVariable("PIG4_TEST_INPUT");
    static bool testInputStarted = false;
    static int testInputIndex = 0;
    if (!testInputStarted && window.hoveredTarget >= 0 && !testInput.isEmpty())
        testInputStarted = true;
    if (testInputStarted && testInputIndex < testInput.size()) {
        const QChar ch = testInput.at(testInputIndex++);
        QKeyEvent ev(QEvent::KeyPress, ch.toUpper().unicode(), Qt::NoModifier,
                     QString(ch));
        QCoreApplication::sendEvent(&window, &ev);
    }
}

void DevHooks::unlockDoorCheat(MainWindow &window)
{
    if (window.roomManager == nullptr)
        return;

    DoorSystem *doors = window.roomManager->doorSystem();
    QList<InteractTarget> &targets = window.roomManager->interactables();
    Door *targetDoor = (window.hoveredTarget >= 0 && window.hoveredTarget < targets.size())
        ? targets[window.hoveredTarget].door : nullptr;

    if (targetDoor != nullptr) {
        targetDoor->unlock();
        doors->syncCollider(targetDoor);
        window.showGameNotice(QStringLiteral("外挂：已解锁 ") + targetDoor->markerId());
        qDebug() << "cheat unlock:" << targetDoor->roomId()
                 << targetDoor->markerId();
        return;
    }

    // 没有对准门时解锁所有还锁着的门，方便批量测门。
    int unlocked = 0;
    for (Door *door : window.roomManager->doors()) {
        if (door == nullptr || !door->isLocked())
            continue;
        door->unlock();
        doors->syncCollider(door);
        ++unlocked;
    }
    window.showGameNotice(unlocked > 0
        ? QStringLiteral("外挂：已解锁 %1 扇门").arg(unlocked)
        : QStringLiteral("外挂：没有锁住的门"));
    qDebug() << "cheat unlock all:" << unlocked;
}

bool DevHooks::handleKey(MainWindow &window, QKeyEvent *event)
{
    if (window.stageOverlay != nullptr && window.stageOverlay->hasContent())
        return false;

    if (event->key() == Qt::Key_X) {
        if (!event->isAutoRepeat())
            unlockDoorCheat(window);
        return true;
    }

    // 开发者测试：T 播开场白；PIG4_STORY=<plot>:<module> 可指定其他段落。
    if (event->key() == Qt::Key_T) {
        if (!event->isAutoRepeat())
            window.playStoryRequest(QStringLiteral("opening"));
        return true;
    }

    return false;
}
