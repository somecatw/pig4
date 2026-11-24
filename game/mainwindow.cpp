#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "mathbase.h"
#include "structures.h"
#include "door.h"
#include "dev/devhooks.h"
#include "itemicons.h"
#include "itemui.h"
#include "overlayui.h"
#include "elevator/elevatorpanelview.h"
#include "puzzle.h"
#include "puzzleui.h"
#include "roommanifest.h"
#include "shardart.h"
#include "storycatalog.h"
#include "storyui.h"
#include <algorithm>
#include <cmath>
#include <QKeyEvent>
#include <QtGlobal>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSysInfo>
#include <QTextDocument>

using namespace std;

static const QList<InteractTarget> &emptyInteractTargets()
{
    static QList<InteractTarget> empty;
    return empty;
}

static QString richTextToPlain(const QString &text)
{
    if (!Qt::mightBeRichText(text))
        return text;
    QTextDocument doc;
    doc.setHtml(text);
    return doc.toPlainText();
}

static Transform lookAtTransform(const Vec3 &pos, const Vec3 &target){
    Vec3 axisZ = (target - pos).normalized();
    Vec3 up = {0, -1, 0};                 // 涓栫晫 Y 鍚戜笅
    Vec3 axisX = axisZ.cross(up);
    if(axisX.len() < 1e-5f) axisX = {1, 0, 0};
    axisX.normalize();
    Vec3 axisY = axisZ.cross(axisX).normalized();
    return {pos, Mat3(axisX, axisY, axisZ)};
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 左侧 3D 视口 + 右侧常驻 UI 栏；Stage3D 本身是 QWidget，HUD 直接叠在它上面。
    auto *mainRow = new QHBoxLayout(ui->centralwidget);
    mainRow->setContentsMargins(0, 0, 0, 0);
    mainRow->setSpacing(0);

    stage = new Stage3D(ui->centralwidget);
    mainRow->addWidget(stage, 1);

    sidebar = new GameSidebar(ui->centralwidget);
    mainRow->addWidget(sidebar, 0);
    connect(sidebar, &GameSidebar::slotActivated, this, [this](int localIndex) {
        const int index = inventoryPage * GameSidebar::SlotsPerPage + localIndex;
        if (index >= 0 && index < inventory.size()) {
            inventoryIndex = index;
            refreshInventoryUi();
        }
    });
    t=0;

    auto *hud = new QGridLayout(stage);
    hud->setContentsMargins(16, 10, 16, 10);
    hud->setSpacing(0);

    roomLabel = new QLabel(stage);
    roomLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    roomLabel->setStyleSheet(
        "color: #f3f3f5; font-size: 16px; font-weight: 600; background: transparent;");
    auto *roomShadow = new QGraphicsDropShadowEffect(roomLabel);
    roomShadow->setBlurRadius(6);
    roomShadow->setOffset(1, 2);
    roomShadow->setColor(QColor(0, 0, 0, 220));
    roomLabel->setGraphicsEffect(roomShadow);
    hud->addWidget(roomLabel, 0, 0, Qt::AlignTop | Qt::AlignLeft);
    hud->setRowStretch(1, 1);

    dialogueBar = new DialogueBar(stage);
    hud->addWidget(dialogueBar, 2, 0);

    // F3 调试叠层放在房间名下方，默认隐藏。
    debugOverlay = new DebugOverlay(stage);
    hud->addWidget(debugOverlay, 1, 0, Qt::AlignTop | Qt::AlignLeft);

    // 唯一的全屏交互容器：puzzle 操作界面和道具查看都挂在这里。
    stageOverlay = new StageOverlay(stage);
    hud->addWidget(stageOverlay, 0, 0, 3, 1);
    stageOverlay->hide();

    // 道具查看是常驻 widget，打开时挂进 StageOverlay，关闭时摘下来隐藏。
    itemDetailView = new ItemDetailView();
    connect(itemDetailView, &OverlayContent::closeRequested,
            this, &MainWindow::closeItemDetail);
    connect(itemDetailView, &ItemDetailView::combineRequested,
            this, &MainWindow::combineItem);

    // 剧情界面常驻，播放时挂进同一个 StageOverlay；关闭时摘下来。
    storyView = new StoryView(stage);
    storyView->hide();
    connect(storyView, &StoryView::skipRequested, this, [this] {
        closeStory(true);
    });
    connect(storyView, &StoryView::storyFinished, this, [this] {
        closeStory(false);
    });

    // 内容 widget 没处理 Esc 时，外壳的兜底关闭。
    connect(stageOverlay, &StageOverlay::closeRequested, this, [this] {
        QWidget *content = stageOverlay->content();
        if (content == itemDetailView)
            closeItemDetail();
        else if (content == storyView)
            closeStory(true);
        else if (content == activePuzzleView)
            closePuzzle();
    });

    resize(1200, 760);

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::updateFrame);
    timer->start(std::chrono::milliseconds(16));

    CameraInfo info;
    info.focalLength = 4.0f;
    info.frame.axisX = {1, 0, 0};
    info.frame.axisY = {0, 1, 0};
    info.frame.axisZ = {0, 0, 1};
    info.width = 15;
    info.height = 10;
    info.screenSize = {8, 5.6, 0};
    camera = new Camera(info, stage->root);
    stage->activeCam = camera;

    if (!DevHooks::loadDebugScene(*this)) {
        const QString appDir = QCoreApplication::applicationDirPath();
        auto resolveAsset = [&](const QString &rel) -> QString {
            QFileInfo direct(rel);
            if (direct.isAbsolute() && direct.exists()) return direct.absoluteFilePath();
            const QString candidates[] = {appDir, appDir + "/..", appDir + "/../..", QDir::currentPath()};
            for (const QString &base : candidates) {
                QFileInfo fi(base + "/" + rel);
                if (fi.exists()) return fi.absoluteFilePath();
            }
            return {};
        };

        const QString iconSample = resolveAsset("assets/images/silver_key.png");
        if (!iconSample.isEmpty())
            ItemIcons::setImageDirectory(QFileInfo(iconSample).absolutePath());

        const QString itemCatalogPath = resolveAsset("assets/items.json");
        if (!itemCatalogPath.isEmpty())
            itemCatalog.load(itemCatalogPath);
        else
            qWarning() << "items.json not found";

        const QString buildingPath = resolveAsset("assets/hotel/building.json");
        if (!buildingPath.isEmpty()) {
            roomManager = new RoomManager(stage);
            if (!roomManager->loadBuilding(buildingPath))
                qWarning() << "building load failed:" << buildingPath;
        } else {
            qWarning() << "building.json not found";
        }

        // 剧情文本：opening.json / plots.json 由 tools/extract_flash_story.py 生成。
        const QString storyFile = resolveAsset("assets/story/plots.json");
        const QString storyOpening = storyFile.isEmpty()
            ? resolveAsset("assets/story/opening.json") : storyFile;
        if (!storyOpening.isEmpty()) {
            storyCatalog = new StoryCatalog();
            storyCatalog->load(QFileInfo(storyOpening).absolutePath());
        } else {
            qWarning() << "story data not found";
        }

        // 槽位 puzzle 的取走/放回需要同时改背包和发布剧情事件。
        // Puzzle 本身不依赖背包，MainWindow 作为中介连接一次即可。
        if (roomManager != nullptr) {
            for (Puzzle *puzzle : roomManager->puzzles()) {
                auto *shard = qobject_cast<ShardPinkPuzzle *>(puzzle);
                if (shard == nullptr)
                    continue;
                connect(shard, &ShardPinkPuzzle::itemTaken, this,
                        [this](const QString &id) {
                    addItem(id, QStringLiteral("粉色碎片"),
                            ItemIcons::pinkShardIcon());
                    showGameNotice(QStringLiteral("取走了粉色碎片。"));
                    if (roomManager != nullptr)
                        roomManager->events().publish(
                            QStringLiteral("shard_pink.removed"));
                });
                connect(shard, &ShardPinkPuzzle::itemInserted, this,
                        [this](const QString &id) {
                    removeItem(id);
                    showGameNotice(QStringLiteral("碎片归位了。"));
                    if (roomManager != nullptr)
                        roomManager->events().publish(
                            QStringLiteral("shard_pink.inserted"));
                });
            }
        }

        // 默认出生点：303 房间。
        const Transform defaultCam = lookAtTransform({2532.0f, -500.0f, -700.0f},
                                                     {1132.0f, -300.0f, 400.0f});
        camera->setTransform(defaultCam);
        camera->updatePosition(defaultCam);
    }
    stage->buildStaticBVH();

    if (roomManager != nullptr) {
        if (Room *r = roomManager->roomAt(camera->camInfo.pos))
            currentRoom = r->def.id;
        qDebug() << "current room:" << currentRoom;
    }
    updateRoomUi();

    ui->statusbar->setStyleSheet(
        "QStatusBar { background-color: #101013; color: #8f8f98;"
        "  border-top: 1px solid #26262c; }"
        "QStatusBar::item { border: none; }");
    fpsLabel = new QLabel();
    fpsLabel->setFont(QFont("Times New Roman", 15));
    fpsLabel->setStyleSheet("color: #8f8f98; background: transparent;");
    fpsLabel->setText("114514");
    ui->statusbar->insertWidget(0, fpsLabel);

    memset(wasdFlags, 0, 4u);
    startPoint = {0,0};
    xacc = yacc = 0;
    jumpFlag = false;

    DevHooks::startup(*this);

    // 新游戏开场：303 醒来 + 纸条。延时到事件循环，避免挡住 DevHooks
    // 的测试场景；正常启动时仍会在第一帧显示。
    QTimer::singleShot(0, this, [this] { startIntroStory(); });
}

// 准星交互：hotspot 是一个以 center 为中心、halfSize 为半边的 OBB。
static bool rayOBB(const Ray &ray, const Transform &global, const Vec3 &half, float &dis)
{
    const Transform inv = global.inverseTransform();
    const Vec3 o = ray.point * inv.rotation + inv.translation;
    const Vec3 d = ray.direction * inv.rotation;
    const float origin[3] = {o.x, o.y, o.z};
    const float dir[3] = {d.x, d.y, d.z};
    const float ext[3] = {half.x, half.y, half.z};

    float tmin = 0.0f, tmax = 1e30f;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(dir[i]) < 1e-8f) {
            if (origin[i] < -ext[i] || origin[i] > ext[i]) return false;
            continue;
        }
        float t1 = (-ext[i] - origin[i]) / dir[i];
        float t2 = ( ext[i] - origin[i]) / dir[i];
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    if (tmax < 0.0f) return false;
    dis = tmin > 0.0f ? tmin : tmax;
    return true;
}

bool MainWindow::raycastTarget(const InteractTarget &target, const Ray &ray, float &dis) const
{
    float best = 1e30f;

    if (target.door != nullptr) {
        target.door->forEach([&](MeshActor *actor) {
            SceneRayHit hit = stage->raytestMesh(actor, ray);
            if (!hit.miss() && hit.dis < best) best = hit.dis;
        });
    }
    if (target.sprite != nullptr) {
        SceneRayHit hit = stage->raytestMesh(target.sprite, ray);
        if (!hit.miss() && hit.dis < best) best = hit.dis;
    }
    if (target.hotspot != nullptr) {
        float d = 0.0f;
        if (rayOBB(ray, target.hotspot->getGlobalTransform(), target.hotspot->halfSize, d)
            && d < best)
            best = d;
    }
    for (MeshActor *actor : target.interactMeshes) {
        if (actor == nullptr)
            continue;
        SceneRayHit hit = stage->raytestMesh(actor, ray);
        if (!hit.miss() && hit.dis < best)
            best = hit.dis;
    }

    if (best >= 1e29f) return false;
    dis = best;
    return true;
}

void MainWindow::setTargetHighlight(const InteractTarget &target, bool on)
{
    auto apply = [&](GameObject *obj) {
        if (obj == nullptr) return;
        obj->forEach([&](MeshActor *actor) {
            if (on)
                actor->shaderConfigOverride |= ShaderConfig::Highlight;
            else
                actor->shaderConfigOverride &= ushort(~ShaderConfig::Highlight);
        });
    };
    apply(target.door);
    apply(target.sprite);
    for (MeshActor *actor : target.interactMeshes) {
        if (actor == nullptr)
            continue;
        if (on)
            actor->shaderConfigOverride |= ShaderConfig::Highlight;
        else
            actor->shaderConfigOverride &= ushort(~ShaderConfig::Highlight);
    }
}

void MainWindow::setHoveredTarget(int index)
{
    const QList<InteractTarget> &targets = roomManager
        ? roomManager->interactables() : emptyInteractTargets();
    if (roomManager == nullptr || index < 0 || index >= targets.size()) index = -1;

    if (index != hoveredTarget) {
        if (roomManager != nullptr && hoveredTarget >= 0 && hoveredTarget < targets.size())
            setTargetHighlight(targets[hoveredTarget], false);
        hoveredTarget = index;
        if (hoveredTarget >= 0)
            setTargetHighlight(targets[hoveredTarget], true);
        if (hoveredTarget >= 0)
            qDebug() << "interact hover:" << targets[hoveredTarget].id;
    }

    if (stage == nullptr) return;
    if (hoveredTarget >= 0) {
        const InteractTarget &target = targets[hoveredTarget];
        QString prompt;
        if (target.puzzle != nullptr) {
            if (!target.puzzle->interactive()) {
                // 线索类 puzzle 只高亮，不显示 F 提示。
                stage->interactionPrompt.clear();
                stage->interactionHover = true;
                return;
            }
            prompt = QStringLiteral("[F] ") + target.puzzle->promptText();
        } else if (target.door != nullptr && roomManager != nullptr && roomManager->doorSystem() != nullptr) {
            prompt = roomManager->doorSystem()->query(
                target.door, selectedItemId(), currentRoom).prompt;
        } else if (!target.pickupItem.isEmpty()) {
            prompt = QStringLiteral("[F] 拾取 ")
                   + (target.pickupName.isEmpty() ? target.pickupItem : target.pickupName);
        } else if (target.id == "silver_key") {
            prompt = QStringLiteral("[F] 拾取 银质小钥匙");
        } else if (target.id == QStringLiteral("moon")) {
            prompt = QStringLiteral("[F] 拾取 月亮积木");
        } else if (!target.prompt.isEmpty()) {
            prompt = QStringLiteral("[F] ") + target.prompt;
        } else if (!target.id.isEmpty()) {
            prompt = QStringLiteral("[F] 查看 ") + target.id;
        } else {
            prompt = QStringLiteral("[F] 交互");
        }
        stage->interactionPrompt = prompt;
        stage->interactionHover = true;
    } else {
        stage->interactionPrompt.clear();
        stage->interactionHover = false;
    }
}

void MainWindow::updateInteraction()
{
    if (stage == nullptr || camera == nullptr) return;
    if (roomManager == nullptr) {
        setHoveredTarget(-1);
        return;
    }

    const QList<InteractTarget> &targets = roomManager->interactables();
    const int cw = camera->camInfo.width * tileSize;
    const int ch = camera->camInfo.height * tileSize;
    const Ray ray = camera->pixelToRay(cw / 2, ch / 2);

    const StaticRayHit staticHit = stage->raytestStatic(ray);
    float bestDis = staticHit.hit ? staticHit.dis : 1e30f;
    constexpr float maxInteractDis = 900.0f;

    // INTERACT_ 几何本身就在 static BVH 里，目标命中和静态命中的距离会相同；
    // 给一点表面容差，避免自己的网格把交互目标挡掉。
    constexpr float surfaceTolerance = 0.2f;

    int best = -1;
    for (int i = 0; i < targets.size(); ++i) {
        if (targets[i].collected) continue;
        float dis = 0.0f;
        if (!raycastTarget(targets[i], ray, dis)) continue;
        if (dis <= maxInteractDis && dis <= bestDis + surfaceTolerance) {
            best = i;
            bestDis = dis;
        }
    }
    setHoveredTarget(best);
}

QString MainWindow::selectedItemId() const
{
    if (inventoryIndex < 0 || inventoryIndex >= inventory.size())
        return {};
    return inventory[inventoryIndex].id;
}

void MainWindow::refreshInventoryUi()
{
    if (sidebar == nullptr) return;

    if (inventory.isEmpty()) {
        inventoryIndex = -1;
        inventoryPage = 0;
    } else {
        if (inventoryIndex < 0 || inventoryIndex >= inventory.size())
            inventoryIndex = 0;
        inventoryPage = inventoryIndex / GameSidebar::SlotsPerPage;
    }

    sidebar->setInventory(inventory);
    sidebar->setPage(inventoryPage);
    const int localIndex = inventory.isEmpty()
        ? -1 : inventoryIndex - inventoryPage * GameSidebar::SlotsPerPage;
    sidebar->setSelection(localIndex);
}

void MainWindow::addItem(const QString &id, const QString &name, const QImage &icon)
{
    for (int i = 0; i < inventory.size(); ++i) {
        if (inventory[i].id == id) {
            inventoryIndex = i;
            refreshInventoryUi();
            return;
        }
    }
    InventoryItem item;
    item.id = id;
    item.name = name;
    item.icon = icon;
    inventory.push_back(item);
    inventoryIndex = inventory.size() - 1;
    refreshInventoryUi();
}

void MainWindow::cycleInventory(int delta)
{
    if (inventory.isEmpty()) return;
    const int count = inventory.size();
    if (inventoryIndex < 0)
        inventoryIndex = delta > 0 ? -1 : 0;
    inventoryIndex = (inventoryIndex + delta + count) % count;
    inventoryPage = inventoryIndex / GameSidebar::SlotsPerPage;

    qDebug() << "inventory select:" << inventoryIndex
             << (inventoryIndex < inventory.size() ? inventory[inventoryIndex].id : QString())
             << "page" << inventoryPage + 1;
    if (sidebar != nullptr) {
        sidebar->setPage(inventoryPage);
        sidebar->setSelection(inventoryIndex - inventoryPage * GameSidebar::SlotsPerPage);
    }
}

void MainWindow::selectInventoryPage(int page)
{
    page = std::clamp(page, 0, GameSidebar::PageCount - 1);
    const int first = page * GameSidebar::SlotsPerPage;
    const int last = std::min(first + GameSidebar::SlotsPerPage, int(inventory.size()));
    if (page != inventoryPage || inventoryIndex < first || inventoryIndex >= last) {
        inventoryPage = page;
        inventoryIndex = first < inventory.size() ? first : -1;
    }

    if (sidebar != nullptr) {
        sidebar->setPage(inventoryPage);
        sidebar->setSelection(inventoryIndex >= 0
            ? inventoryIndex - inventoryPage * GameSidebar::SlotsPerPage : -1);
    }
}

void MainWindow::showGameNotice(const QString &text, int durationMs)
{
    if (dialogueBar != nullptr)
        dialogueBar->showNotice(text, durationMs);
}

void MainWindow::updateRoomUi()
{
    checkRoomStoryTriggers();
    if (roomLabel == nullptr) return;
    if (currentRoom.isEmpty()) {
        roomLabel->clear();
        return;
    }

    if (roomManager != nullptr) {
        if (Room *room = roomManager->room(currentRoom)) {
            if (!room->def.label.isEmpty()) {
                roomLabel->setText(room->def.label);
                return;
            }
        }
    }

    QString digits;
    for (QChar c : currentRoom)
        if (c.isDigit()) digits += c;

    if (digits.isEmpty()) {
        roomLabel->setText(currentRoom);
        return;
    }

    const int floor = digits.at(0).digitValue();
    static const QStringList floorNames = {
        QStringLiteral("一"), QStringLiteral("二"), QStringLiteral("三"),
        QStringLiteral("四"), QStringLiteral("五")
    };
    const QString floorName = (floor >= 1 && floor <= floorNames.size())
        ? floorNames.at(floor - 1) : QString::number(floor);
    roomLabel->setText(floorName + QStringLiteral("楼·") + digits + QStringLiteral("房"));
}

void MainWindow::interactHovered()
{
    if (roomManager == nullptr) return;
    QList<InteractTarget> &targets = roomManager->interactables();
    if (hoveredTarget < 0 || hoveredTarget >= targets.size()) return;
    InteractTarget &target = targets[hoveredTarget];
    if (target.collected) return;

    if (target.puzzle != nullptr) {
        if (!target.puzzle->interactive())
            return;
        if (target.puzzle->solved()) {
            showGameNotice(QStringLiteral("门锁已经解除了。"));
            return;
        }
        openPuzzle(target.puzzle);
        return;
    }

    if (target.door != nullptr && roomManager->doorSystem() != nullptr) {
        QString notice;
        const auto action = roomManager->doorSystem()->interact(
            target.door, selectedItemId(), currentRoom, &notice);
        if (!notice.isEmpty()) {
            showGameNotice(notice);
            return;
        }
        // 钥匙开锁和这一侧的反锁都只解除锁定，下一次 F 才开门。
        if (action == DoorSystem::Action::Unlock) {
            showGameNotice(QStringLiteral("门锁解除"));
            return;
        }
        if (action == DoorSystem::Action::Climb) {
            const PortalDef *portal = usablePortal(target);
            if (portal != nullptr)
                startPortalTransition(hoveredTarget, *portal);
            else
                showGameNotice(QStringLiteral("无法从这里翻过去。"));
        }
        return;
    }

    if (!target.pickupItem.isEmpty()) {
        QImage icon;
        if (target.pickupIcon.startsWith(QStringLiteral("key:"))) {
            bool ok = false;
            const int number = target.pickupIcon.mid(4).toInt(&ok);
            icon = ItemIcons::keyIcon(ItemIcons::silver(), ok ? number : 0);
        } else if (!target.pickupIcon.isEmpty()) {
            icon = ItemIcons::imageIcon(target.pickupIcon);
        } else {
            icon = ItemIcons::imageIcon(target.pickupItem + QStringLiteral(".png"));
        }

        const QString name = target.pickupName.isEmpty()
            ? target.pickupItem : target.pickupName;
        addItem(target.pickupItem, name, icon);
        if (target.sprite != nullptr) {
            delete target.sprite;
            target.sprite = nullptr;
        }
        target.collected = true;
        setHoveredTarget(-1);
        qDebug() << "picked up:" << target.pickupItem
                 << "inventory size:" << inventory.size();
        showGameNotice(QStringLiteral("获得 ") + name);
        return;
    }

    if (target.id == "silver_key") {
        addItem(QStringLiteral("silver_key"), QStringLiteral("银质小钥匙"),
                ItemIcons::keyIcon(ItemIcons::silver()));
        if (target.sprite != nullptr) {
            delete target.sprite;
            target.sprite = nullptr;
        }
        target.collected = true;
        setHoveredTarget(-1);
        qDebug() << "picked up:" << target.id << "inventory size:" << inventory.size();
        showGameNotice(QStringLiteral("获得 银质小钥匙"));
        return;
    }

    if (target.id == QStringLiteral("moon")) {
        addItem(QStringLiteral("moon_block"), QStringLiteral("月亮积木"),
                ItemIcons::moonBlockIcon());
        if (target.sprite != nullptr) {
            delete target.sprite;
            target.sprite = nullptr;
        }
        target.collected = true;
        setHoveredTarget(-1);
        qDebug() << "picked up:" << target.id << "inventory size:" << inventory.size();
        showGameNotice(QStringLiteral("获得 月亮积木"));
        return;
    }

    // 配置了剧情的交互物优先播放剧情；播过一次后回退到 notice。
    if (!target.story.isEmpty()) {
        if (!target.storyPlayed) {
            target.storyPlayed = true;
            playStoryRequest(target.story);
        } else if (!target.notice.isEmpty()) {
            showGameNotice(target.notice);
        }
        return;
    }

    // 通用 3D 交互物：钟表、假门之类的特殊文本。
    if (!target.notice.isEmpty()) {
        showGameNotice(target.notice);
        return;
    }

    showGameNotice(QStringLiteral("暂时无法交互"));
}

void MainWindow::showOverlay(OverlayContent *content)
{
    if (content == nullptr || stageOverlay == nullptr || stageOverlay->hasContent())
        return;

    if (dialogueBar != nullptr)
        dialogueBar->clearLine();

    setHoveredTarget(-1);
    stage->interactionPrompt.clear();
    stage->interactionHover = false;
    stage->reticleVisible = false;
    memset(wasdFlags, 0, sizeof(wasdFlags));
    jumpFlag = false;
    dragFlag = false;
    xacc = yacc = 0;

    // puzzle/道具查看需要从右侧背包拖道具进来；剧情这类界面才拦侧栏。
    if (sidebar != nullptr)
        sidebar->setAttribute(Qt::WA_TransparentForMouseEvents, content->blocksSidebar());

    stageOverlay->setContent(content);
}

void MainWindow::hideOverlay(OverlayContent *content)
{
    if (content == nullptr || stageOverlay == nullptr)
        return;
    if (stageOverlay->content() != content)
        return;

    stageOverlay->takeContent();

    if (sidebar != nullptr)
        sidebar->setAttribute(Qt::WA_TransparentForMouseEvents, false);

    if (stage != nullptr) {
        stage->reticleVisible = true;
        stage->interactionPrompt.clear();
        stage->interactionHover = false;
    }
    memset(wasdFlags, 0, sizeof(wasdFlags));
    jumpFlag = false;
    dragFlag = false;
    xacc = yacc = 0;
    setFocus(Qt::OtherFocusReason);
}

void MainWindow::openPuzzle(Puzzle *puzzle)
{
    if (puzzle == nullptr || stageOverlay == nullptr || puzzle->solved())
        return;
    if (stageOverlay->hasContent())
        return;

    // 电梯运行中的几百毫秒里不重新打开选层面板；玩家本身仍然可以移动。
    if (elevatorTravelActive && qobject_cast<ElevatorPanelPuzzle *>(puzzle) != nullptr) {
        showGameNotice(QStringLiteral("电梯运行中……"), 900);
        return;
    }

    // 召梯键没有全屏界面，按 F 直接开本层电梯门。
    if (qobject_cast<ElevatorCallPuzzle *>(puzzle) != nullptr) {
        onElevatorCall(puzzle->def());
        return;
    }

    PuzzleView *view = puzzle->createView(stageOverlay);
    if (view == nullptr) {
        showGameNotice(QStringLiteral("这个机关还没有实现。"));
        return;
    }

    activePuzzle = puzzle;
    activePuzzleView = view;
    activePuzzleSolvedConnection = connect(
        puzzle, &Puzzle::puzzleSolved, this, &MainWindow::onActivePuzzleSolved);
    activeElevatorConnection = {};
    activeElevatorOpenConnection = {};
    if (auto *elevator = qobject_cast<ElevatorPanelPuzzle *>(puzzle)) {
        activeElevatorConnection = connect(
            elevator, &ElevatorPanelPuzzle::floorSelected,
            this, &MainWindow::onElevatorFloorSelected);
        activeElevatorOpenConnection = connect(
            elevator, &ElevatorPanelPuzzle::openDoorRequested,
            this, &MainWindow::onElevatorOpenDoor);
    }
    connect(view, &PuzzleView::closeRequested, this, &MainWindow::closePuzzle);
    // 2D 操作界面是唯一的外观来源；它的当前截图会同步成 3D sprite 的贴图。
    connect(view, &PuzzleView::appearanceChanged,
            puzzle, &Puzzle::setWorldTexture);

    // 先同步一次基础外观（此时 view 还没显示，避免把 hover 状态带进世界贴图）。
    view->syncAppearance();
    showOverlay(view);
    qDebug() << "puzzle open:" << puzzle->def().marker;
}

void MainWindow::closePuzzle()
{
    if (activePuzzleSolvedConnection) {
        disconnect(activePuzzleSolvedConnection);
        activePuzzleSolvedConnection = {};
    }
    if (activeElevatorConnection) {
        disconnect(activeElevatorConnection);
        activeElevatorConnection = {};
    }
    if (activeElevatorOpenConnection) {
        disconnect(activeElevatorOpenConnection);
        activeElevatorOpenConnection = {};
    }
    activePuzzle = nullptr;

    // puzzle view 是临时创建的；道具详情是常驻 widget，不能在这里删。
    PuzzleView *view = activePuzzleView;
    activePuzzleView = nullptr;
    if (view != nullptr) {
        hideOverlay(view);
        view->deleteLater();
    }
    qDebug() << "puzzle closed";
}

void MainWindow::onActivePuzzleSolved()
{
    if (activePuzzle == nullptr)
        return;

    const QString event = activePuzzle->def().event;
    const QString notice = activePuzzle->solvedNotice();
    closePuzzle();

    if (roomManager != nullptr && !event.isEmpty())
        roomManager->events().publish(event);
    showGameNotice(notice);
}

void MainWindow::openItemDetail()
{
    if (itemDetailView == nullptr || stageOverlay == nullptr)
        return;
    if (stageOverlay->hasContent())
        return;
    if (inventoryIndex < 0 || inventoryIndex >= inventory.size())
        return;

    const InventoryItem &item = inventory[inventoryIndex];
    activeItemId = item.id;
    const ItemDef def = itemCatalog.definitionFor(item.id, item.name, item.icon);

    QList<InventoryItem> combineCandidates;
    if (const ItemDef *base = itemCatalog.find(item.id)) {
        for (const ItemCombineDef &recipe : base->combines) {
            for (const InventoryItem &other : inventory) {
                if (other.id == recipe.with) {
                    combineCandidates.push_back(other);
                    break;
                }
            }
        }
    }

    itemDetailView->setItem(def, combineCandidates);
    showOverlay(itemDetailView);
    qDebug() << "item detail open:" << item.id
             << "views" << def.views.size()
             << "combine candidates" << combineCandidates.size();
}

void MainWindow::closeItemDetail()
{
    if (itemDetailView == nullptr)
        return;

    hideOverlay(itemDetailView);
    itemDetailView->clearItem();
    activeItemId.clear();
    qDebug() << "item detail closed";
}

void MainWindow::playStoryRequest(const QString &request)
{
    if (storyCatalog == nullptr || !storyCatalog->isLoaded() || stageOverlay == nullptr) {
        showGameNotice(QStringLiteral("没有可用的剧情文本。"), 2500);
        return;
    }
    // 剧情不抢占已经打开的 puzzle / 道具界面。
    if (stageOverlay->hasContent())
        return;

    const QString trimmed = request.trimmed();
    StoryScript script;
    const bool openingRequest = trimmed.isEmpty()
        || trimmed.compare(QStringLiteral("opening"), Qt::CaseInsensitive) == 0
        || trimmed == QStringLiteral("1");

    if (openingRequest) {
        script = storyCatalog->openingScript();
        if (script.lines.isEmpty())
            script = storyCatalog->firstScript();
    } else {
        // 格式：<plotId>[:<moduleName>]；省略 module 时用第一个 module。
        const auto parts = trimmed.split(QLatin1Char(':'), Qt::SkipEmptyParts);
        const QString moduleName = parts.size() > 1 ? parts.value(1) : QString();
        script = storyCatalog->script(parts.value(0), moduleName);
    }

    if (script.lines.isEmpty()) {
        showGameNotice(QStringLiteral("剧情不存在：") + request, 2500);
        return;
    }
    startStory(script);
}

void MainWindow::startStory(const StoryScript &script)
{
    if (stageOverlay == nullptr || storyView == nullptr || script.lines.isEmpty())
        return;
    if (stageOverlay->hasContent())
        return;

    storyView->setScript(script);
    showOverlay(storyView);
    qDebug() << "story start:" << script.id << script.lines.size() << "lines";
}

bool MainWindow::storyTriggersSuppressed() const
{
    // 测试/截图模式默认不自动弹剧情；PIG4_SHOW_STORIES 可强制打开。
    if (qEnvironmentVariableIsSet("PIG4_SHOW_STORIES"))
        return false;
    return qEnvironmentVariableIsSet("PIG4_SKIP_STORY")
        || qEnvironmentVariableIsSet("PIG4_ROOM_SHOT")
        || qEnvironmentVariableIsSet("PIG4_OPEN_PUZZLE")
        || qEnvironmentVariableIsSet("PIG4_OPEN_ITEM")
        || qEnvironmentVariableIsSet("PIG4_STORY");
}

void MainWindow::startIntroStory()
{
    if (introStarted || currentRoom != QStringLiteral("room_303")
        || qEnvironmentVariableIsSet("PIG4_SKIP_INTRO")
        || storyTriggersSuppressed()) {
        return;
    }
    if (storyCatalog == nullptr || stageOverlay == nullptr
        || stageOverlay->hasContent()) {
        return;
    }

    StoryScript script = storyCatalog->openingScript();
    const StoryScript roomScript =
        storyCatalog->script(QStringLiteral("_7_Ruby"), QStringLiteral("module_1"));
    script.lines += roomScript.lines;
    if (script.lines.isEmpty())
        return;

    introStarted = true;
    introPaperPending = true;
    script.id = QStringLiteral("303_intro");
    script.title = QStringLiteral("303 / 出生");
    startStory(script);
}

void MainWindow::checkRoomStoryTriggers()
{
    if (lobbyCryPlayed || storyTriggersSuppressed()
        || currentRoom != QStringLiteral("room_lobby")) {
        return;
    }
    if (stageOverlay == nullptr || stageOverlay->hasContent())
        return;

    lobbyCryPlayed = true;
    playStoryRequest(QStringLiteral("_7_Ruby:module_2"));
}

void MainWindow::closeStory(bool skipped)
{
    if (stageOverlay == nullptr || storyView == nullptr)
        return;
    if (stageOverlay->content() != storyView)
        return;

    hideOverlay(storyView);
    storyView->clearScript();

    if (introPaperPending) {
        introPaperPending = false;
        addItem(QStringLiteral("paper"), QStringLiteral("纸团"),
                ItemIcons::imageIcon(QStringLiteral("paper_fold.png")));
        showGameNotice(QStringLiteral("获得 纸团"));
    } else if (skipped) {
        showGameNotice(QStringLiteral("已跳过剧情。"), 1500);
    }
    qDebug() << "story closed, skipped:" << skipped;

    // 如果进入大堂时正好有 overlay，关掉后再补一次触发检查。
    checkRoomStoryTriggers();
}

bool MainWindow::removeItem(const QString &id)
{
    for (int i = 0; i < inventory.size(); ++i) {
        if (inventory[i].id != id)
            continue;
        inventory.removeAt(i);
        if (inventoryIndex > i)
            --inventoryIndex;
        else if (inventoryIndex >= inventory.size())
            inventoryIndex = inventory.size() - 1;
        refreshInventoryUi();
        return true;
    }
    return false;
}

void MainWindow::combineItem(const QString &otherId)
{
    if (activeItemId.isEmpty() || activeItemId == otherId)
        return;

    const ItemDef *base = itemCatalog.find(activeItemId);
    if (base == nullptr)
        return;

    const ItemCombineDef *recipe = nullptr;
    for (const ItemCombineDef &candidate : base->combines) {
        if (candidate.with == otherId) {
            recipe = &candidate;
            break;
        }
    }
    if (recipe == nullptr)
        return;

    bool hasActive = false;
    bool hasOther = false;
    QImage fallbackIcon;
    for (const InventoryItem &item : inventory) {
        if (item.id == activeItemId)
            hasActive = true;
        if (item.id == otherId) {
            hasOther = true;
            fallbackIcon = item.icon;
        }
    }
    if (!hasActive || !hasOther)
        return;

    const ItemDef resultDef = itemCatalog.definitionFor(
        recipe->result, recipe->result, QImage());
    QImage resultIcon = resultDef.views.isEmpty()
        ? QImage() : resultDef.views.first().image;
    if (resultIcon.isNull())
        resultIcon = fallbackIcon;

    const QString resultName = resultDef.name.isEmpty()
        ? recipe->result : resultDef.name;
    const QString notice = recipe->notice.isEmpty()
        ? QStringLiteral("组合成功。") : recipe->notice;

    removeItem(activeItemId);
    removeItem(otherId);
    addItem(recipe->result, resultName, resultIcon);
    closeItemDetail();
    showGameNotice(notice);
}

const PortalDef *MainWindow::usablePortal(const InteractTarget &target) const
{
    if (roomManager == nullptr) return nullptr;
    const PortalDef *portal = roomManager->portalFor(target.room, target.id);
    if (portal == nullptr) return nullptr;

    // 窗户可以从任一侧翻到另一侧；目标点必须在要去的房间存在。
    if (currentRoom == portal->fromRoom)
        return portal->hasEnd(portal->toRoom) ? portal : nullptr;
    if (currentRoom == portal->toRoom)
        return portal->hasEnd(portal->fromRoom) ? portal : nullptr;
    return nullptr;
}

void MainWindow::startPortalTransition(int targetIndex, const PortalDef &portal)
{
    if (roomManager == nullptr || camera == nullptr) return;

    const QString fromRoom = currentRoom;
    QString toRoom;
    if (fromRoom == portal.fromRoom)
        toRoom = portal.toRoom;
    else if (fromRoom == portal.toRoom)
        toRoom = portal.fromRoom;
    else {
        qWarning() << "portal cannot be used from room:" << fromRoom;
        return;
    }

    Room *to = roomManager->room(toRoom);
    if (to == nullptr || !portal.hasEnd(toRoom)) {
        qWarning() << "portal destination/end missing:" << portal.id << toRoom;
        return;
    }

    const PortalEnd &dest = portal.ends.value(toRoom);
    const Vec3 endPos = roomManager->roomSourceToWorld(
        to, {dest.pos[0], dest.pos[1], dest.pos[2]});
    const Vec3 endLook = roomManager->roomSourceToWorld(
        to, {dest.lookAt[0], dest.lookAt[1], dest.lookAt[2]});

    portalPathPositions.clear();
    portalPathLooks.clear();
    const Vec3 startPos = camera->camInfo.pos;
    const Vec3 startLook = startPos + camera->camInfo.frame.axisZ * 400.0f;
    portalPathPositions.append(startPos);
    portalPathLooks.append(startLook);

    // 路径经过墙上的开口中心，避免从奇怪的角度直接穿到对侧落点时穿墙。
    // 两侧 end 决定“走到哪、看哪里”，不强行当前相机先绕到本侧落点。
    bool hasOpening = false;
    Vec3 opening;
    if (targetIndex >= 0 && targetIndex < roomManager->interactables().size()) {
        Door *door = roomManager->interactables()[targetIndex].door;
        if (door != nullptr && door->hasColliderVolume()) {
            opening = door->colliderTransform().translation;
            hasOpening = true;
        }
    }

    const Vec3 midLook = startLook + (endLook - startLook) * 0.5f;
    if (hasOpening
        && (opening - portalPathPositions.last()).len() > 1.0f
        && (endPos - opening).len() > 1.0f) {
        portalPathPositions.append(opening);
        portalPathLooks.append(midLook);
    }

    if ((endPos - portalPathPositions.last()).len() > 1.0f) {
        portalPathPositions.append(endPos);
        portalPathLooks.append(endLook);
    } else {
        portalPathPositions.last() = endPos;
        portalPathLooks.last() = endLook;
    }

    portalPathPrefix.clear();
    portalPathLength = 0.0f;
    portalPathPrefix.append(0.0f);
    for (int i = 1; i < portalPathPositions.size(); ++i) {
        portalPathLength += (portalPathPositions[i] - portalPathPositions[i - 1]).len();
        portalPathPrefix.append(portalPathLength);
    }

    portalTransitionActive = true;
    portalTransitionStartMs = QDateTime::currentMSecsSinceEpoch();
    portalDestinationRoom = toRoom;
    portalTransitionId = portal.id;
    setHoveredTarget(-1);

    qDebug() << "portal transition:" << portal.id
             << fromRoom << "->" << toRoom
             << "path points" << portalPathPositions.size()
             << "length" << portalPathLength;
}

bool MainWindow::updatePortalTransition()
{
    if (!portalTransitionActive || camera == nullptr) return false;

    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - portalTransitionStartMs;
    float t = portalTransitionDurationMs > 0
        ? float(elapsed) / float(portalTransitionDurationMs) : 1.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float e = t * t * (3.0f - 2.0f * t);

    Vec3 pos = portalPathPositions.last();
    Vec3 look = portalPathLooks.last();

    if (portalPathLength > 1e-4f && portalPathPositions.size() >= 2) {
        const float distance = e * portalPathLength;
        int segment = 0;
        while (segment + 2 < portalPathPositions.size()
               && distance > portalPathPrefix[segment + 1])
            ++segment;
        const float segStart = portalPathPrefix[segment];
        const float segLen = portalPathPrefix[segment + 1] - segStart;
        const float u = segLen > 1e-5f
            ? std::clamp((distance - segStart) / segLen, 0.0f, 1.0f) : 0.0f;
        const Vec3 &p0 = portalPathPositions[segment];
        const Vec3 &p1 = portalPathPositions[segment + 1];
        pos = p0 + (p1 - p0) * u;
        const Vec3 &l0 = portalPathLooks[segment];
        const Vec3 &l1 = portalPathLooks[segment + 1];
        look = l0 + (l1 - l0) * u;
    }

    if (t >= 1.0f) {
        pos = portalPathPositions.last();
        look = portalPathLooks.last();
        portalTransitionActive = false;
        currentRoom = portalDestinationRoom;
        updateRoomUi();

        if (portalTransitionId == QStringLiteral("bath_window_301_303")
            && currentRoom == QStringLiteral("room_303")) {
            showGameNotice(QStringLiteral("【这边居然是……另一个厕所？】"), 3000);
        }
        qDebug() << "portal transition finished, room:" << currentRoom;
    }

    camera->setTransform(lookAtTransform(pos, look));
    return portalTransitionActive;
}

Vec3 getNormal(const Mesh &mesh, uint triangleID){
    Vec3 v0 = mesh.vertices[mesh.triangles[triangleID].vid[0]].pos;
    Vec3 v1 = mesh.vertices[mesh.triangles[triangleID].vid[1]].pos;
    Vec3 v2 = mesh.vertices[mesh.triangles[triangleID].vid[2]].pos;
    return (v1-v0).cross(v2-v0).normalized();
}
const float movSpeed = 40;
const float charaHeight = 500;
const float liftThreshold = 162;

BBox3D charaBox(const Vec3 &pos, bool extraY=false){
    BBox3D box;
    box.x1 = pos.x - 100;
    box.x2 = pos.x + 100;
    box.y1 = pos.y - 5;
    box.y2 = pos.y + 500 + (extraY?50:0);
    box.z1 = pos.z - 100;
    box.z2 = pos.z + 100;
    return box;
}
Vec3 posIterate(Stage3D *stage, Vec3 pos, Vec3 mov){
    Vec3 unit = mov.normalized();
    // mov = movSpeed * unit;
    // SceneRayHit movHit = stage->raytest({pos + Vec3({0, charaHeight-liftThreshold, 0}), unit});
    // if(movHit.actor != nullptr && movHit.dis < mov.len()){
    //     Vec3 normal = getNormal(movHit.actor->mesh, movHit.triangleID);
    //     Vec3 tangent = normal.cross({0, 1, 0});
    //     float ttfa = mov.dot(tangent);
    //     mov = tangent * ttfa;
    // }
    set<BoxtestResult> hit = stage->boxtest(charaBox(pos + mov + Vec3({0, -liftThreshold, 0}), false));
    for(const auto r:hit){
        if(r.normal.dot({0, -1, 0}) < 0.7f && r.normal.dot(unit) <= 0.0f){
            Vec3 tangent = r.normal.cross({0, 1, 0}).normalized();
            float ttfa = mov.dot(tangent);
            mov = tangent * ttfa;
            unit = mov.normalized();
            // break;
            qDebug()<<tangent.to_string()<<mov.to_string();
        }
    }
    return mov;
}
bool falling;
float ySpeed;

void MainWindow::updateFrame(){
    // 任意全屏交互界面打开时冻结玩家移动/视角，只继续渲染背景。
    if (stageOverlay != nullptr && stageOverlay->hasContent()) {
        fpsLabel->setText(QString::asprintf("%.1f fps (render %.0f us, total %.0f us)",
                                           1e6 / stage->avgFrameTime, stage->avgRenderTime,
                                           stage->avgFrameTime));
        stage->updateFrame();
        DevHooks::updateOverlay(*this);
        return;
    }

    // 过场期间锁住玩家输入和物理，只推进相机轨迹。
    if (portalTransitionActive) {
        updatePortalTransition();
        fpsLabel->setText(QString::asprintf("%.1f fps (render %.0f us, total %.0f us)",
                                           1e6 / stage->avgFrameTime, stage->avgRenderTime,
                                           stage->avgFrameTime));
        stage->updateFrame();
        if (debugOverlay != nullptr && debugOverlay->isVisible()) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - lastDebugUpdateMs >= 150) {
                lastDebugUpdateMs = now;
                updateDebugOverlay();
            }
        }
        return;
    }

    if(dragFlag){
        camera->rotateAroundAxis({0,1,0}, -0.01*xacc);
        Transform temp = camera->getTransform();
        camera->rotateAroundAxis(camera->getTransform().rotation.row(0), 0.01*yacc);
        if(camera->getGlobalTransform().rotation.row(1).y <= 0) camera->setTransform(temp);
        xacc = yacc = 0;
    }

    Vec3 vx = camera->camInfo.frame.axisX;
    Vec3 vy = camera->camInfo.frame.axisY;
    Vec3 vz = camera->camInfo.frame.axisZ;

    Vec3 front = (vz - Vec3({0, 1, 0}) * Vec3({0, 1, 0}).dot(vz)).normalized();
    // Vec3 front = vz;
    Vec3 left = front.cross({0, 1, 0});
    Vec3 mov = {0, 0, 0};
    Vec3 pos = camera->camInfo.pos;

    if(wasdFlags[0]){
        // qDebug()<<front.to_string();
        mov += front;
    }
    if(wasdFlags[1]){
        mov += left;
    }
    if(wasdFlags[2]){
        mov -= front;
    }
    if(wasdFlags[3]){
        mov -= left;
    }
    // camera->translate(mov*100);
    if(true){
    if(mov.len() > 1e-5){
        mov = mov.normalized() * movSpeed;
        mov = posIterate(stage, pos, mov);
        // mov = posIterate(stage, pos, mov);
        set<BoxtestResult> res = stage->boxtest(charaBox(pos+mov+Vec3{0, -liftThreshold,0}));
        bool wallflag = false;
        // qDebug()<<"mov:"<<mov.to_string();
        for(const auto &r:res){
            qDebug()<<r.normal.to_string();
            if(abs(r.normal.dot({0, -1, 0})) < 0.7 && r.normal.dot(mov.normalized()) < -0.01f) wallflag = true;
        }
        // qDebug()<<wallflag;
        // SceneRayHit wHit = stage->raytest({pos, mov.normalized()});
        // if(wHit.actor != nullptr && wHit.dis < mov.len()) mov = 0.0*(wHit.pos - pos);
        if(wallflag) mov *= 0.0f;
        pos += mov;
    }
    if(jumpFlag && !falling){
        falling = true;
        jumpFlag = false;
        ySpeed = -40;
    }

    BBox3D box = charaBox(pos, true);
    auto boxRes = stage->boxtest(box);
    float groundYMin = 1e9;
    bool groundHit = false;
    // 只把脚底以下（含少量容差）的朝上表面当落地平面。
    // 否则角色箱体扫到桌子/床/天花板顶面时，会被“吸”到它们上面。
    const float feetY = pos.y + charaHeight;
    for(const auto &r:boxRes){
        if(r.normal.dot({0, -1, 0}) > 0.7f  && r.ymin >= feetY - 200.0f ){
            groundYMin = min(groundYMin, r.ymin);
            groundHit = true;
        }
    }

    SceneRayHit upHit = stage->raytest({pos, {0, -1, 0}});
    SceneRayHit downHit = stage->raytest({pos, {0, 1, 0}});

    if(ySpeed < 0.0f && !upHit.miss() && !downHit.miss() && upHit.dis < 100){
        ySpeed = 0.0f;
        camera->moveToLocalPos(upHit.pos+Vec3({0, 1, 0}));
    }else if(ySpeed >= 0.0f && groundHit /*!downHit.miss() && downHit.dis > charaHeight - liftThreshold && downHit.dis < charaHeight + 100*/){
        falling = false;
        ySpeed = 0;
        // camera->moveToLocalPos(downHit.pos + Vec3({0, -charaHeight, 0}));
        // qDebug()<<"hit:"<<downHit.pos.y - charaHeight;
        camera->moveToLocalPos({pos.x, groundYMin - charaHeight, pos.z});
    }else{
        falling = true;
        ySpeed += 2;
        // if(downHit.actor != nullptr && upHit.dis + downHit.dis > charaHeight + 100)
        camera->translate(mov);
        camera->translate({0, ySpeed, 0});
    }
    }
    fpsLabel->setText(QString::asprintf("%.1f fps (render %.0f us, total %.0f us)", 1e6 / stage->avgFrameTime, stage->avgRenderTime, stage->avgFrameTime));

    stage->updateFrame();

    // 相机移动跨过房间边界时同步 active room，portal 的方向判断依赖它。
    if (roomManager != nullptr) {
        if (Room *r = roomManager->roomAt(camera->camInfo.pos);
            r != nullptr && r->def.id != currentRoom) {
            currentRoom = r->def.id;
            updateRoomUi();
        }
    }
    updateInteraction();

    // F3 只以 6~7 Hz 刷新，避免每帧在 UI 线程里做全场景射线测试。
    if (debugOverlay != nullptr && debugOverlay->isVisible()) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastDebugUpdateMs >= 150) {
            lastDebugUpdateMs = now;
            updateDebugOverlay();
        }
    }

    DevHooks::updateGameplay(*this);
    return;
}


void MainWindow::onElevatorFloorSelected(const QString &floorId)
{
    if (activePuzzle == nullptr || roomManager == nullptr || camera == nullptr)
        return;
    if (elevatorTravelActive)
        return;

    auto showElevatorError = [this](const QString &text) {
        if (auto *view = qobject_cast<ElevatorPanelView *>(activePuzzleView)) {
            view->applyEvent(PuzzleViewEvent{
                QString::fromLatin1(ElevatorViewEvents::Feedback),
                {{QString::fromLatin1(ElevatorViewEvents::Text), text},
                 {QString::fromLatin1(ElevatorViewEvents::DurationMs), 1200}}
            });
        }
    };

    MeshActor *source = activePuzzle->sprite();
    if (source == nullptr) {
        showElevatorError(QStringLiteral("电梯面板没有锚点。"));
        return;
    }

    const QString destinationId = QStringLiteral("elevator_panel_") + floorId;
    InteractTarget *destination = roomManager->findInteractableById(destinationId);
    if (destination == nullptr || destination->sprite == nullptr) {
        showElevatorError(QStringLiteral("电梯暂时无法到达 ") + floorId);
        return;
    }
    if (destination->sprite == source) {
        showElevatorError(QStringLiteral("已经在这一层了。"));
        return;
    }

    const PuzzleDef &def = activePuzzle->def();
    const QString sourcePrefix = QStringLiteral("elevator_panel_");
    const QString sourceFloor = def.marker.startsWith(sourcePrefix)
        ? def.marker.mid(sourcePrefix.size()) : QString();

    // 记录源/目标面板变换；传送时刻再取玩家当前相对源面板的位姿，
    // 这样关门等待的几百毫秒里玩家仍然可以自由移动。
    elevatorTravelActive = true;
    elevatorTravelSourceWorld = source->getGlobalTransform();
    elevatorTravelDestinationWorld = destination->sprite->getGlobalTransform();
    elevatorTravelRoomId = destination->room != nullptr
        ? destination->room->def.id : QString();
    elevatorTravelFloor = floorId;

    if (Door *sourceDoor = elevatorDoor(def.room, sourceFloor))
        sourceDoor->close();

    // 选层后立即释放玩家，不把操作界面硬留在屏幕上。
    closePuzzle();
    showGameNotice(QStringLiteral("电梯运行中……"), 900);

    qDebug() << "elevator departing:" << sourceFloor << "->" << floorId
             << "room" << def.room;
    // 平移门约 0.4 秒关完；留一点余量再传送。
    QTimer::singleShot(700, this, &MainWindow::finishElevatorTravel);
}

void MainWindow::finishElevatorTravel()
{
    if (!elevatorTravelActive)
        return;
    elevatorTravelActive = false;

    closePuzzle();

    // 使用传送时刻的相机位姿，保留玩家在电梯里的走动/转向。
    const Transform cameraWorld = camera->getGlobalTransform();
    const Transform local = cameraWorld * elevatorTravelSourceWorld.inverseTransform();
    const Transform newWorld = local * elevatorTravelDestinationWorld;

    const Transform rootGlobal = stage->root->getGlobalTransform();
    camera->setTransform(newWorld * rootGlobal.inverseTransform());
    camera->updatePosition(newWorld);

    falling = false;
    ySpeed = 0.0f;
    jumpFlag = false;

    Room *destinationRoom = elevatorTravelRoomId.isEmpty()
        ? roomManager->roomAt(newWorld.translation)
        : roomManager->room(elevatorTravelRoomId);
    if (destinationRoom != nullptr) {
        currentRoom = destinationRoom->def.id;
        updateRoomUi();
        // 到站开门；面板上的开门键用于门自动关上后再开一次。
        if (Door *door = elevatorDoor(destinationRoom->def.id, elevatorTravelFloor)) {
            door->openTemporarily(5.0f);
            qDebug() << "elevator arrival opens door:" << elevatorTravelFloor
                     << destinationRoom->def.id;
        }
    }

    showGameNotice(QStringLiteral("电梯到了 ") + elevatorTravelFloor + QStringLiteral("。"));
    setHoveredTarget(-1);
    qDebug() << "elevator teleport:" << elevatorTravelFloor
             << "to" << currentRoom
             << "pos" << camera->camInfo.pos.to_string().c_str();
}

Door *MainWindow::elevatorDoor(const QString &roomId, const QString &floorId) const
{
    if (roomManager == nullptr || roomId.isEmpty() || floorId.isEmpty())
        return nullptr;
    return roomManager->doorSystem()->findDoor(
        roomId, floorId + QStringLiteral("_elevator"));
}

void MainWindow::onElevatorOpenDoor()
{
    if (activePuzzle == nullptr || roomManager == nullptr || elevatorTravelActive)
        return;

    const PuzzleDef &def = activePuzzle->def();
    const QString prefix = QStringLiteral("elevator_panel_");
    if (!def.marker.startsWith(prefix))
        return;
    const QString floorId = def.marker.mid(prefix.size());

    Door *door = elevatorDoor(def.room, floorId);
    closePuzzle();
    if (door == nullptr) {
        showGameNotice(QStringLiteral("电梯门没有响应。"));
        return;
    }
    door->openTemporarily(5.0f);
    showGameNotice(QStringLiteral("电梯门开了。"));
}

void MainWindow::onElevatorCall(const PuzzleDef &def)
{
    const QString prefix = QStringLiteral("call_elevator_");
    if (!def.marker.startsWith(prefix))
        return;
    const QString floorId = def.marker.mid(prefix.size());

    Door *door = elevatorDoor(def.room, floorId);
    if (door == nullptr) {
        showGameNotice(QStringLiteral("电梯门没有响应。"));
        return;
    }
    // 多次按只重置停留时间。
    door->openTemporarily(5.0f);
    showGameNotice(QStringLiteral("电梯门开了。"));
    qDebug() << "elevator call:" << def.room << floorId;
}

void MainWindow::updateDebugOverlay()
{
    if (debugOverlay == nullptr || camera == nullptr || stage == nullptr)
        return;

    const Vec3 eye = camera->camInfo.pos;
    // 世界 Y 轴向下，脚底在眼睛下方 charaHeight。
    const Vec3 feet = {eye.x, eye.y + charaHeight, eye.z};
    const Vec3 fwd = camera->camInfo.frame.axisZ.normalized();

    constexpr float pi = 3.14159265358979323846f;
    // Minecraft 约定：yaw 0 = +Z，90 = -X，180 = -Z，270 = +X。
    float yaw = std::atan2(-fwd.x, fwd.z) * 180.0f / pi;
    if (yaw < 0.0f) yaw += 360.0f;
    const float pitch = std::asin(std::clamp(fwd.y, -1.0f, 1.0f)) * 180.0f / pi;

    QString facing;
    if (yaw >= 315.0f || yaw < 45.0f)       facing = QStringLiteral("south (+Z)");
    else if (yaw < 135.0f)                  facing = QStringLiteral("west (-X)");
    else if (yaw < 225.0f)                  facing = QStringLiteral("north (-Z)");
    else                                    facing = QStringLiteral("east (+X)");

    auto f2 = [](float v) { return QString::number(v, 'f', 2); };
    auto f3 = [](float v) { return QString::number(v, 'f', 3); };

#ifdef NDEBUG
    const QString buildType = QStringLiteral("Release");
#else
    const QString buildType = QStringLiteral("Debug");
#endif
#if defined(_MSC_VER)
    const QString compiler = QStringLiteral("MSVC %1").arg(_MSC_VER);
#elif defined(__clang__)
    const QString compiler = QStringLiteral("Clang %1.%2")
        .arg(__clang_major__).arg(__clang_minor__);
#elif defined(__GNUC__)
    const QString compiler = QStringLiteral("GCC %1.%2")
        .arg(__GNUC__).arg(__GNUC_MINOR__);
#else
    const QString compiler = QStringLiteral("unknown compiler");
#endif

    const double frameMs = stage->avgFrameTime / 1000.0;
    const double renderMs = stage->avgRenderTime / 1000.0;
    const double fps = stage->avgFrameTime > 1e-6 ? 1e6 / stage->avgFrameTime : 0.0;

    // 准星命中：优先给出交互目标；没有交互目标时退回到场景网格。
    QString lookLine = QStringLiteral("Looking at: —");
    QStringList lookDetails;
    const Ray ray = camera->pixelToRay(camera->camInfo.width * tileSize / 2,
                                       camera->camInfo.height * tileSize / 2);
    const QList<InteractTarget> &targets = roomManager
        ? roomManager->interactables() : emptyInteractTargets();

    if (hoveredTarget >= 0 && hoveredTarget < targets.size()) {
        const InteractTarget &target = targets[hoveredTarget];

        QString label = stage->interactionPrompt;
        if (label.startsWith(QStringLiteral("[F] ")))
            label.remove(0, 4);
        label = richTextToPlain(label);
        if (label.isEmpty())
            label = target.id;
        lookLine = QStringLiteral("Looking at: ") + label;

        QString kind = QStringLiteral("target");
        QString state;
        if (target.puzzle != nullptr) {
            kind = target.puzzle->interactive()
                ? QStringLiteral("puzzle") : QStringLiteral("puzzle clue");
            if (target.puzzle->solved())
                state = QStringLiteral("Solved");
        } else if (target.door != nullptr) {
            kind = target.door->isWindow() ? QStringLiteral("window")
                                           : QStringLiteral("door");
            switch (target.door->state()) {
            case DoorState::Locked: state = QStringLiteral("Locked"); break;
            case DoorState::Closed: state = QStringLiteral("Closed"); break;
            case DoorState::Open:   state = QStringLiteral("Open");   break;
            }
        } else if (target.sprite != nullptr) {
            kind = QStringLiteral("sprite");
        } else if (target.hotspot != nullptr) {
            kind = QStringLiteral("hotspot");
        }

        QString detail = QStringLiteral("  ") + target.id
                       + QStringLiteral(" · ") + kind;
        if (!state.isEmpty())
            detail += QStringLiteral(" · ") + state;
        if (target.room != nullptr)
            detail += QStringLiteral(" · ") + target.room->def.id;
        lookDetails << detail;

        if (target.door != nullptr && target.door->isLocked())
            lookDetails << QStringLiteral("  lock: ") + target.door->lockDisplayName();

        float dis = 0.0f;
        if (raycastTarget(target, ray, dis)) {
            const Vec3 hit = ray.point + ray.direction * dis;
            lookDetails << QStringLiteral("  hit %1 / %2 / %3  ·  d=%4")
                .arg(f3(hit.x), f3(hit.y), f3(hit.z), f2(dis));
        }
    } else {
        const SceneRayHit sceneHit = stage->raytest(ray);
        if (!sceneHit.miss()) {
            lookLine = QStringLiteral("Looking at: mesh #%1  tri #%2")
                .arg(sceneHit.actor->meshID).arg(sceneHit.triangleID);
            lookDetails << QStringLiteral("  hit %1 / %2 / %3  ·  d=%4")
                .arg(f3(sceneHit.pos.x), f3(sceneHit.pos.y), f3(sceneHit.pos.z),
                     f2(sceneHit.dis));
        } else {
            const StaticRayHit staticHit = stage->raytestStatic(ray);
            if (staticHit.hit) {
                lookLine = QStringLiteral("Looking at: static geometry");
                lookDetails << QStringLiteral("  hit %1 / %2 / %3  ·  d=%4")
                    .arg(f3(staticHit.pos.x), f3(staticHit.pos.y), f3(staticHit.pos.z),
                         f2(staticHit.dis));
            }
        }
    }

    const int blockX = int(std::floor(feet.x));
    const int blockY = int(std::floor(feet.y));
    const int blockZ = int(std::floor(feet.z));

    QStringList left;
    left << QStringLiteral("pig4  %1  ·  %2  ·  Qt %3")
        .arg(buildType, compiler, QString::fromLatin1(qVersion()));
    left << QStringLiteral("%1 fps  ·  frame %2 ms  ·  render %3 ms")
        .arg(QString::number(fps, 'f', 1),
             QString::number(frameMs, 'f', 2),
             QString::number(renderMs, 'f', 2));
    left << QString();
    left << QStringLiteral("XYZ: %1 / %2 / %3  (eye)")
        .arg(f2(eye.x), f2(eye.y), f2(eye.z));
    left << QStringLiteral("Feet: %1 / %2 / %3")
        .arg(f2(feet.x), f2(feet.y), f2(feet.z));
    left << QStringLiteral("Block: %1 %2 %3").arg(blockX).arg(blockY).arg(blockZ);
    left << QStringLiteral("Facing: %1  (yaw %2° / pitch %3°)")
        .arg(facing, QString::number(yaw, 'f', 1), QString::number(pitch, 'f', 1));
    left << QStringLiteral("Forward: %1 / %2 / %3")
        .arg(f3(fwd.x), f3(fwd.y), f3(fwd.z));
    left << QString();
    left << lookLine;
    left.append(lookDetails);

    QString roomText = currentRoom.isEmpty() ? QStringLiteral("—") : currentRoom;
    if (roomLabel != nullptr && !roomLabel->text().isEmpty())
        roomText += QStringLiteral(" · ") + roomLabel->text();

    QString selected = QStringLiteral("—");
    if (inventoryIndex >= 0 && inventoryIndex < inventory.size()) {
        const InventoryItem &item = inventory[inventoryIndex];
        selected = item.name.isEmpty() ? item.id
                                        : item.name + QStringLiteral(" (") + item.id + QStringLiteral(")");
    }

    int doorTotal = 0, doorOpen = 0, doorLocked = 0;
    if (roomManager != nullptr) {
        for (Door *door : roomManager->doors()) {
            if (door == nullptr) continue;
            ++doorTotal;
            if (door->isOpen()) ++doorOpen;
            if (door->isLocked()) ++doorLocked;
        }
    }

    const BuildingDef &building = roomManager != nullptr
        ? roomManager->building() : BuildingDef();

    QStringList right;
    right << QStringLiteral("Room: %1").arg(roomText);
    right << QStringLiteral("Building: %1 rooms · %2 portals · %3 locks · %4 conn")
        .arg(roomManager != nullptr ? roomManager->rooms().size() : 0)
        .arg(building.portals.size()).arg(building.locks.size())
        .arg(building.connections.size());
    right << QStringLiteral("Doors: %1 · open %2 · locked %3")
        .arg(doorTotal).arg(doorOpen).arg(doorLocked);
    right << QStringLiteral("Inventory: %1 items · page %2/%3")
        .arg(inventory.size()).arg(inventoryPage + 1)
        .arg(GameSidebar::PageCount);
    right << QStringLiteral("Selected: %1").arg(selected);
    right << QString();
    right << QStringLiteral("Viewport: %1x%2 · buffer %3x%4")
        .arg(stage->width()).arg(stage->height())
        .arg(stage->renderSize().width()).arg(stage->renderSize().height());
    right << QStringLiteral("Frame: %1 verts · %2 tris · %3 tile frags")
        .arg(frameStat.vcnt).arg(frameStat.tcnt).arg(frameStat.tileFragmentSum);
    right << QStringLiteral("Pixels: %1")
        .arg(QString::number(frameStat.pixelIterated.load()));
    right << QStringLiteral("BVH: %1 nodes · meshes %2 · materials %3")
        .arg(stage->staticBVHNodeCount()).arg(stage->runtimeMeshCount())
        .arg(stage->materialCount());
    right << QStringLiteral("Dynamic colliders: %1").arg(stage->dynamicColliderCount());
    right << QString();
    right << QStringLiteral("OS: %1").arg(QSysInfo::prettyProductName());
    right << QStringLiteral("CPU: %1 · Qt %2")
        .arg(QSysInfo::currentCpuArchitecture(), QString::fromLatin1(qVersion()));
    right << QString();
    right << QStringLiteral("F3 debug · WASD move · F interact");
    right << QStringLiteral("Q/E item · 1-5 page · drag look · Space jump");

    debugOverlay->setColumns(left.join(QLatin1Char('\n')), right.join(QLatin1Char('\n')));
}

MainWindow::~MainWindow()
{
    delete roomManager;
    roomManager = nullptr;
    delete storyCatalog;
    storyCatalog = nullptr;
    delete ui;
}

namespace pig4_detail {
template<>
void MainWindowLayoutGuard<unsigned(sizeof(MainWindow))>::check() {}
}

void MainWindow::keyPressEvent(QKeyEvent *evt){
    // F3：Minecraft 风格调试叠层。忽略长按自动重复，避免闪烁。
    if (evt->key() == Qt::Key_F3) {
        if (!evt->isAutoRepeat() && debugOverlay != nullptr) {
            const bool show = !debugOverlay->isVisible();
            debugOverlay->setVisible(show);
            if (show) {
                lastDebugUpdateMs = 0;
                updateDebugOverlay();
                debugOverlay->raise();
            }
        }
        return;
    }

    // 全屏交互界面打开时，按键交给当前内容 widget；外壳只兜底。
    if (stageOverlay != nullptr && stageOverlay->hasContent()) {
        if (auto *content = qobject_cast<OverlayContent *>(stageOverlay->content()))
            content->handleContentKey(evt);
        return;
    }

    if (DevHooks::handleKey(*this, evt))
        return;

    // 等待推进的剧情行优先消费 F/Space，避免同时触发交互或跳跃。
    if ((evt->key() == Qt::Key_F || evt->key() == Qt::Key_Space)
        && dialogueBar != nullptr && dialogueBar->advance())
        return;

    // 数字键 1–5 直接跳背包页；Q/E 仍负责前后选物品。
    if (evt->key() >= Qt::Key_1 && evt->key() <= Qt::Key_5) {
        selectInventoryPage(evt->key() - Qt::Key_1);
        return;
    }

    // R：查看当前选中的道具。忽略长按自动重复，避免刚打开就被重复事件关掉。
    if (evt->key() == Qt::Key_R) {
        if (!evt->isAutoRepeat())
            openItemDetail();
        return;
    }

    if(evt->key() == Qt::Key_W){
        wasdFlags[0] = true;
    }
    if(evt->key() == Qt::Key_A){
        wasdFlags[1] = true;
    }
    if(evt->key() == Qt::Key_S){
        wasdFlags[2] = true;
    }
    if(evt->key() == Qt::Key_D){
        wasdFlags[3] = true;
    }
    if(evt->key() == Qt::Key_Space){
        jumpFlag = true;
    }
    if(evt->key() == Qt::Key_Q) cycleInventory(-1);
    if(evt->key() == Qt::Key_E) cycleInventory(1);
    if(evt->key() == Qt::Key_F) interactHovered();

}

void MainWindow::keyReleaseEvent(QKeyEvent *evt){
    if (stageOverlay != nullptr && stageOverlay->hasContent())
        return;
    if(evt->key() == Qt::Key_W){
        wasdFlags[0] = false;
    }
    if(evt->key() == Qt::Key_A){
        wasdFlags[1] = false;
    }
    if(evt->key() == Qt::Key_S){
        wasdFlags[2] = false;
    }
    if(evt->key() == Qt::Key_D){
        wasdFlags[3] = false;
    }
}

void MainWindow::mousePressEvent(QMouseEvent *evt){
    if (stageOverlay != nullptr && stageOverlay->hasContent())
        return;
    dragFlag = true;
    startPoint = evt->pos();
    xacc = yacc = 0;
    // Ray ttfa = stage->pixelToRay(evt->pos().x(), evt->pos().y());
    // Raytest::intersectCallCnt = 0;
    // SceneRayHit hit = stage->raytest(ttfa);
    // qDebug()<<Raytest::intersectCallCnt;
    // if(hit.actor != nullptr){
    //     qDebug()<<hit.actor->meshID<<hit.triangleID<<hit.dis;
    //     hit.actor->mesh.triangles[hit.triangleID].shaderConfig |= ShaderConfig::WireframeOnly;
    // }
}

void MainWindow::mouseMoveEvent(QMouseEvent *evt){
    if (stageOverlay != nullptr && stageOverlay->hasContent())
        return;
    dragFlag = true;
    xacc += (evt->pos() - startPoint).x();
    yacc += (evt->pos() - startPoint).y();
    startPoint = evt->pos();
}

void MainWindow::mouseReleaseEvent(QMouseEvent *evt){
    if (stageOverlay != nullptr && stageOverlay->hasContent())
        return;
    dragFlag = false;
}
