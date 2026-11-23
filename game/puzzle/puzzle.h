#ifndef PUZZLE_H
#define PUZZLE_H

#include <QObject>
#include <QString>
#include "buildingmanifest.h"

class MeshActor;
class PuzzleView;
class QImage;
class QWidget;

// Puzzle 基类：场景里仍然是一张二维 sprite，这里只保存公共配置和运行状态。
// 每种具体机关继承它，自己决定操作界面长什么样。
class Puzzle : public QObject
{
    Q_OBJECT
public:
    Puzzle(const PuzzleDef &def, MeshActor *sprite, QObject *parent = nullptr);
    ~Puzzle() override = default;

    const PuzzleDef &def() const { return m_def; }
    MeshActor *sprite() const { return m_sprite; }
    bool interactive() const { return m_def.interactive; }
    bool solved() const { return m_solved; }

    // 准星提示和 F3 调试用的短名称。
    QString promptText() const;
    QString solvedNotice() const;

    // 交互式 puzzle 的操作界面；非交互式线索返回 nullptr。
    virtual PuzzleView *createView(QWidget *parent) = 0;

    // 让具体 puzzle 覆盖世界里的 sprite 外观。默认使用 marker 原本的贴图。
    virtual void applyWorldAppearance(MeshActor *sprite);

    // 把 2D view 的当前外观同步到 3D sprite 的材质。
    void setWorldTexture(const QImage &image);

signals:
    void puzzleSolved();

protected:
    void finishPuzzle();

    PuzzleDef m_def;
    MeshActor *m_sprite = nullptr;
    bool m_solved = false;
    int m_worldMaterialID = -1;
};

// 不可交互线索：只参与高亮，不打开操作界面。
class CluePuzzle : public Puzzle
{
public:
    using Puzzle::Puzzle;
    PuzzleView *createView(QWidget *) override { return nullptr; }
};

// 九宫格数字键盘。
class KeypadPuzzle : public Puzzle
{
public:
    using Puzzle::Puzzle;
    PuzzleView *createView(QWidget *parent) override;
    void applyWorldAppearance(MeshActor *sprite) override;

private:
    QString m_input;
};

// 可配置字母密码器：键位、列数、密码和动作键都来自 building.json。
// solution 的长度就是密码位数；未配置 submitKey 时输满自动校验。
class PasswordKeypadPuzzle : public Puzzle
{
public:
    using Puzzle::Puzzle;
    PuzzleView *createView(QWidget *parent) override;
    void applyWorldAppearance(MeshActor *sprite) override;

private:
    QString m_input;
};

// 电梯选层面板。选择和 3D 锚点都使用这个 panel，不额外放 trigger。
class ElevatorPanelPuzzle : public Puzzle
{
    Q_OBJECT
public:
    using Puzzle::Puzzle;
    PuzzleView *createView(QWidget *parent) override;
    void applyWorldAppearance(MeshActor *sprite) override;

signals:
    void floorSelected(const QString &floorId);
    // 面板上的“开门”键；由 MainWindow 找到当前楼层的电梯门并临时打开。
    void openDoorRequested();
};

// 电梯外面的召梯按键：不打开全屏界面，按 F 直接让本层电梯门打开。
class ElevatorCallPuzzle : public Puzzle
{
    Q_OBJECT
public:
    using Puzzle::Puzzle;
    PuzzleView *createView(QWidget *) override { return nullptr; }
    void applyWorldAppearance(MeshActor *sprite) override;
};

// 单槽碎片 puzzle：槽里初始嵌着一块碎片，可以点击取走、拖回放回。
// 后续九碎片机关沿用同一套 slot 思路，只是槽位从 1 个变成 N 个。
class ShardPinkPuzzle : public Puzzle
{
    Q_OBJECT
public:
    explicit ShardPinkPuzzle(const PuzzleDef &def, MeshActor *sprite,
                             QObject *parent = nullptr);

    PuzzleView *createView(QWidget *parent) override;
    void applyWorldAppearance(MeshActor *sprite) override;

    bool filled() const { return m_filled; }
    const QString &itemId() const { return m_itemId; }

    // 取走：槽变空，发 itemTaken；返回 false 表示本来就是空的。
    bool takeItem();
    // 放回：只接受本槽的 itemId；返回 false 表示槽已占用或 id 不对。
    bool insertItem(const QString &id);

signals:
    void itemTaken(const QString &id);
    void itemInserted(const QString &id);
    void stateChanged();

private:
    QString m_itemId = QStringLiteral("shard_pink");
    bool m_filled = true;
};

bool isPasswordKeypadKind(const QString &kind);
Puzzle *createPuzzle(const PuzzleDef &def, MeshActor *sprite);

#endif // PUZZLE_H
