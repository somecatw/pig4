#include "puzzle.h"
#include "assetmanager.h"
#include "elevator/elevatorart.h"
#include "elevator/elevatorpanelview.h"
#include "gameobject.h"
#include "password/passwordart.h"
#include "password/passwordkeypadview.h"
#include "puzzleart.h"
#include "puzzleui.h"
#include "shardart.h"

#include <QDebug>

Puzzle::Puzzle(const PuzzleDef &def, MeshActor *sprite, QObject *parent)
    : QObject(parent)
    , m_def(def)
    , m_sprite(sprite)
{
}

QString Puzzle::promptText() const
{
    if (!m_def.prompt.isEmpty())
        return m_def.prompt;
    if (!m_def.title.isEmpty())
        return QStringLiteral("查看") + m_def.title;
    return QStringLiteral("查看 ") + m_def.marker;
}

QString Puzzle::solvedNotice() const
{
    return m_def.solvedText.isEmpty()
        ? QStringLiteral("密码正确，门锁解除")
        : m_def.solvedText;
}

void Puzzle::finishPuzzle()
{
    if (m_solved)
        return;
    m_solved = true;
    emit puzzleSolved();
}

void Puzzle::applyWorldAppearance(MeshActor *)
{
}

void KeypadPuzzle::applyWorldAppearance(MeshActor *sprite)
{
    if (sprite == nullptr)
        return;

    // 初始外观就是 KeypadPuzzleView 画出来的同一张图；打开界面后如果 2D
    // 外观变化，MainWindow 会把 view 的新截图通过 setWorldTexture 同步回来。
    setWorldTexture(keypadPuzzleImage());
}

void Puzzle::setWorldTexture(const QImage &image)
{
    if (m_sprite == nullptr || image.isNull())
        return;

    // 2D 截图通常是预乘 alpha；纹理采样按普通 ARGB 处理，统一转一下。
    const QImage normalized = image.convertToFormat(QImage::Format_ARGB32);

    auto &materials = assetManager.getMaterials();
    if (m_worldMaterialID >= 0 && m_worldMaterialID < int(materials.size())) {
        // 复用材质，避免每次 2D 状态变化都往 assetManager 里塞新材质。
        materials[m_worldMaterialID].img = normalized;
        materials[m_worldMaterialID].updateImage();
    } else {
        m_worldMaterialID = assetManager.addMaterial(normalized);
        if (m_worldMaterialID < 0)
            return;
    }

    auto &meshes = assetManager.getMeshes();
    if (m_sprite->meshID >= meshes.size())
        return;

    meshes[m_sprite->meshID].materialID = ushort(m_worldMaterialID);
    m_sprite->mesh.materialID = ushort(m_worldMaterialID);
}

PuzzleView *KeypadPuzzle::createView(QWidget *parent)
{
    auto *view = new KeypadPuzzleView(parent);
    m_input.clear();

    auto showFeedback = [view](const QString &text) {
        view->applyEvent(PuzzleViewEvent{
            QString::fromLatin1(KeypadViewEvents::Feedback),
            {{QString::fromLatin1(KeypadViewEvents::Text), text},
             {QString::fromLatin1(KeypadViewEvents::DurationMs), 900}}
        });
    };

    connect(view, &PuzzleView::viewEvent, this,
            [this, showFeedback](const PuzzleViewEvent &event) {
        if (m_solved
            || event.type != QLatin1String(KeypadViewEvents::Digit)) {
            return;
        }

        const int digit = event.payload.value(
            QString::fromLatin1(KeypadViewEvents::Value)).toInt();
        if (digit < 1 || digit > 9)
            return;

        m_input += QString::number(digit);
        if (m_def.solution.isEmpty()) {
            // 当前 slice 的占位配置：还没写真正密码，任意按键都算启动。
            m_input.clear();
            finishPuzzle();
            return;
        }

        // 不要逐位验证：只有输入完整长度后才判断对错，避免“第一位错就
        // 立刻失败”把密码空间暴露成若干次独立试探。
        if (m_input.size() < m_def.solution.size())
            return;

        const bool ok = m_input == m_def.solution;
        m_input.clear();
        if (ok)
            finishPuzzle();
        else
            showFeedback(QStringLiteral("没有反应……"));
    });

    return view;
}

namespace {

QImage passwordArtwork(const PuzzleDef &def, const QString &input)
{
    return passwordPanelImage(def.keys, qMax(1, def.columns), input,
                              def.clearKey, def.backspaceKey, def.submitKey);
}

bool sameActionKey(const QString &label, const QString &configured)
{
    // 精确匹配：密码键面可能同时有普通 c 和绿色清除键 C。
    return !configured.isEmpty() && label == configured;
}

QStringList elevatorFloors(const PuzzleDef &def)
{
    if (!def.floors.isEmpty())
        return def.floors;
    return {QStringLiteral("1F"), QStringLiteral("2F"), QStringLiteral("3F"),
            QStringLiteral("4F"), QStringLiteral("5F"), QStringLiteral("6F")};
}

QString elevatorCurrentFloor(const PuzzleDef &def)
{
    const QString prefix = QStringLiteral("elevator_panel_");
    if (def.marker.startsWith(prefix))
        return def.marker.mid(prefix.size());
    return {};
}

QImage elevatorArtwork(const PuzzleDef &def)
{
    return elevatorPanelImage(elevatorFloors(def), elevatorCurrentFloor(def));
}

} // namespace

void PasswordKeypadPuzzle::applyWorldAppearance(MeshActor *sprite)
{
    if (sprite == nullptr)
        return;
    setWorldTexture(passwordArtwork(m_def, QString()));
}

PuzzleView *PasswordKeypadPuzzle::createView(QWidget *parent)
{
    auto *view = new PasswordKeypadView(m_def.keys, qMax(1, m_def.columns),
                                        m_def.clearKey, m_def.backspaceKey,
                                        m_def.submitKey, parent);

    // 两个方向都用通用事件：view 只负责发“按了哪个键”和接收显示状态，
    // 不需要知道 PasswordKeypadPuzzle 是否存在。
    auto setViewInput = [view](const QString &text) {
        view->applyEvent(PuzzleViewEvent{
            QString::fromLatin1(PasswordKeypadViewEvents::SetInput),
            {{QString::fromLatin1(PasswordKeypadViewEvents::Text), text}}
        });
    };
    auto showViewFeedback = [view](const QString &text) {
        view->applyEvent(PuzzleViewEvent{
            QString::fromLatin1(PasswordKeypadViewEvents::Feedback),
            {{QString::fromLatin1(PasswordKeypadViewEvents::Text), text},
             {QString::fromLatin1(PasswordKeypadViewEvents::DurationMs), 900}}
        });
    };

    setViewInput(m_input);

    auto submitInput = [this, view, setViewInput, showViewFeedback] {
        if (m_def.solution.isEmpty()) {
            m_input.clear();
            setViewInput(m_input);
            view->syncAppearance();
            finishPuzzle();
            return;
        }

        if (m_input.compare(m_def.solution, Qt::CaseInsensitive) == 0) {
            setViewInput(m_input);
            view->syncAppearance();
            finishPuzzle();
            return;
        }

        // 和数字键盘一样，只在输入完整长度后统一判定，不逐位泄露对错。
        m_input.clear();
        setViewInput(m_input);
        showViewFeedback(m_def.errorText.isEmpty()
                             ? QStringLiteral("没有反应……")
                             : m_def.errorText);
        view->syncAppearance();
    };

    connect(view, &PuzzleView::viewEvent, this,
            [this, view, submitInput, setViewInput](const PuzzleViewEvent &event) mutable {
        if (m_solved
            || event.type != QLatin1String(PasswordKeypadViewEvents::Key)) {
            return;
        }

        const QString label = event.payload.value(
            QString::fromLatin1(PasswordKeypadViewEvents::Label)).toString();
        if (label.isEmpty())
            return;

        if (sameActionKey(label, m_def.clearKey)) {
            m_input.clear();
            setViewInput(m_input);
            view->syncAppearance();
            return;
        }
        if (sameActionKey(label, m_def.backspaceKey)) {
            if (!m_input.isEmpty())
                m_input.chop(1);
            setViewInput(m_input);
            view->syncAppearance();
            return;
        }
        if (sameActionKey(label, m_def.submitKey)) {
            submitInput();
            return;
        }

        if (m_def.solution.isEmpty()) {
            // 没配密码的占位机关：任意普通键都算启动，和 KeypadPuzzle 一致。
            m_input.clear();
            setViewInput(m_input);
            view->syncAppearance();
            finishPuzzle();
            return;
        }

        // solution 的字符数就是密码位数；输满后忽略多余按键。
        if (m_input.size() >= m_def.solution.size())
            return;

        m_input += label;
        setViewInput(m_input);
        view->syncAppearance();

        if (m_def.submitKey.isEmpty()
            && m_input.size() >= m_def.solution.size()) {
            submitInput();
        }
    });

    return view;
}

void ElevatorPanelPuzzle::applyWorldAppearance(MeshActor *sprite)
{
    if (sprite == nullptr)
        return;
    setWorldTexture(elevatorArtwork(m_def));
}

PuzzleView *ElevatorPanelPuzzle::createView(QWidget *parent)
{
    auto *view = new ElevatorPanelView(parent);
    view->applyEvent(PuzzleViewEvent{
        QString::fromLatin1(ElevatorViewEvents::SetPanel),
        {{QString::fromLatin1(ElevatorViewEvents::Floors), elevatorFloors(m_def)},
         {QString::fromLatin1(ElevatorViewEvents::CurrentFloor),
          elevatorCurrentFloor(m_def)}}
    });

    connect(view, &PuzzleView::viewEvent, this,
            [this](const PuzzleViewEvent &event) {
        if (event.type == QLatin1String(ElevatorViewEvents::Floor)) {
            emit floorSelected(event.payload.value(
                QString::fromLatin1(ElevatorViewEvents::FloorId)).toString());
            return;
        }
        if (event.type == QLatin1String(ElevatorViewEvents::OpenDoor))
            emit openDoorRequested();
    });
    return view;
}

void ElevatorCallPuzzle::applyWorldAppearance(MeshActor *sprite)
{
    if (sprite == nullptr)
        return;
    setWorldTexture(elevatorCallButtonImage());
}

ShardPinkPuzzle::ShardPinkPuzzle(const PuzzleDef &def, MeshActor *sprite,
                                 QObject *parent)
    : Puzzle(def, sprite, parent)
{
    if (!def.item.isEmpty())
        m_itemId = def.item;
    m_filled = def.filled;
}

PuzzleView *ShardPinkPuzzle::createView(QWidget *parent)
{
    auto *view = new ShardPinkPuzzleView(parent);

    auto sendState = [this, view] {
        view->applyEvent(PuzzleViewEvent{
            QString::fromLatin1(ShardPinkViewEvents::SetState),
            {{QString::fromLatin1(ShardPinkViewEvents::Filled), m_filled},
             {QString::fromLatin1(ShardPinkViewEvents::ItemId), m_itemId}}
        });
    };
    auto sendFeedback = [view](const QString &text) {
        view->applyEvent(PuzzleViewEvent{
            QString::fromLatin1(ShardPinkViewEvents::Feedback),
            {{QString::fromLatin1(ShardPinkViewEvents::Text), text},
             {QString::fromLatin1(ShardPinkViewEvents::DurationMs), 1200}}
        });
    };

    connect(view, &PuzzleView::viewEvent, this,
            [this, sendFeedback](const PuzzleViewEvent &event) {
        if (event.type == QLatin1String(ShardPinkViewEvents::Take)) {
            if (!takeItem())
                sendFeedback(QStringLiteral("凹槽是空的。"));
            return;
        }
        if (event.type == QLatin1String(ShardPinkViewEvents::Drop)) {
            const QString id = event.payload.value(
                QString::fromLatin1(ShardPinkViewEvents::ItemId)).toString();
            if (!insertItem(id))
                sendFeedback(QStringLiteral("放不进去。"));
        }
    });

    // 持久状态归 puzzle；view 只接收状态快照并刷新显示。
    connect(this, &ShardPinkPuzzle::stateChanged, view, sendState);
    sendState();
    return view;
}

void ShardPinkPuzzle::applyWorldAppearance(MeshActor *sprite)
{
    if (sprite == nullptr)
        return;
    setWorldTexture(ShardArt::pinkPanelImage(m_filled));
}

bool ShardPinkPuzzle::takeItem()
{
    if (!m_filled || m_itemId.isEmpty())
        return false;
    m_filled = false;
    emit stateChanged();
    emit itemTaken(m_itemId);
    return true;
}

bool ShardPinkPuzzle::insertItem(const QString &id)
{
    if (m_filled || id != m_itemId)
        return false;
    m_filled = true;
    emit stateChanged();
    emit itemInserted(id);
    return true;
}

bool isPasswordKeypadKind(const QString &kind)
{
    return kind.compare(QStringLiteral("letter_keypad"), Qt::CaseInsensitive) == 0
        || kind.compare(QStringLiteral("password_keypad"), Qt::CaseInsensitive) == 0;
}

Puzzle *createPuzzle(const PuzzleDef &def, MeshActor *sprite)
{
    if (!def.interactive)
        return new CluePuzzle(def, sprite);
    if (def.kind.compare(QStringLiteral("keypad"), Qt::CaseInsensitive) == 0)
        return new KeypadPuzzle(def, sprite);
    if (isPasswordKeypadKind(def.kind))
        return new PasswordKeypadPuzzle(def, sprite);
    if (def.kind.compare(QStringLiteral("elevator_panel"), Qt::CaseInsensitive) == 0)
        return new ElevatorPanelPuzzle(def, sprite);
    if (def.kind.compare(QStringLiteral("elevator_call"), Qt::CaseInsensitive) == 0)
        return new ElevatorCallPuzzle(def, sprite);
    if (def.kind.compare(QStringLiteral("shard_pink"), Qt::CaseInsensitive) == 0)
        return new ShardPinkPuzzle(def, sprite);

    qWarning() << "unknown puzzle kind:" << def.kind << def.marker;
    return nullptr;
}
