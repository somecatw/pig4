# pig4

CPU tile-based software renderer + 3D remake of the Flash game 泞之翼2.

**如果你是人类并且知道这是什么: 我闲的没事搓的，渲染器是我写的，有几个函数是哈基米写的，别的都是大肥鱼写的，别打我，可以到 4 群找我**

## Source layout

```
engine/        纯引擎：数学、资源、场景树、CPU 光栅化、BVH、Stage3D 视口
game/          游戏逻辑：房间、门/窗、交互、剧情 UI、背包、入口
game/item/     道具目录、图标、拖放和查看界面
game/puzzle/   puzzle 逻辑、操作界面和绘制
game/dev/      PIG4_* 无界面测试 / 调试钩子
assets/        运行时资产（hotel 房间、全局道具图）
tools/         OBJ / VRay 烘焙合并、PH marker manifest 生成
```

目录分层只用于组织代码，CMake 仍然只编译一个 `pig4` target，不拆成两个库。`game/`
可以包含 `engine/` 的头文件，反向依赖应避免。

所有 `PIG4_*` 无界面测试和调试钩子集中在 `game/dev/devhooks.cpp`，MainWindow 只保留
`DevHooks::loadDebugScene/startup/updateOverlay/updateGameplay/handleKey` 几个调用点。

## Build

```powershell
cmd /c '"D:\Visual Studio\VC\Auxiliary\Build\vcvars64.bat" > nul & cmake --build build'
```

Qt 6.8.3 MSVC 需要 `E:\qtlib\6.8.3\msvc2022_64\bin` 在 `PATH` 中。首次配置建议用
`CMakePresets.json` 里的 `msvc-release`，保证 `/utf-8` 被 CMake 检测到。

MSVC 的 `/showIncludes` 前缀会按当前控制台代码页输出 GBK 或 UTF-8，Qt Creator
和终端可能不同，Ninja 只记一种前缀就会静默丢掉头文件依赖。CMake 会通过
`tools/msvc_deps_wrapper.py` 统一成 ASCII 前缀，所以配置时需要能找到
`python`/`python3`。

## Controls

`WASD` 移动，鼠标拖动转视角，`Space` 跳跃，`F` 交互；`Q/E` 切换物品，
`1–5` 切换背包页，`R` 查看当前选中道具。钥匙锁必须先把对应钥匙切为当前
选中物品，准星提示才会变成 `[F] 用 <key> 开锁`；F 只开锁，再按一次 F 才开门。
按 `F3` 打开 Minecraft 风格调试叠层，显示相机世界坐标、朝向，以及准星命中的
交互目标/场景网格和命中点坐标。

无界面测试时可用 `PIG4_DEBUG_OVERLAY=1` 在启动时直接展开叠层。

对话条和准星提示支持 Qt 的 rich text 子集（`QLabel` / `QTextDocument`），
可以直接写颜色标签，例如：

```html
锁住了，需要 <span style="color:#38c838">“8号钥匙”</span>
```

调试 `PIG4_DIALOGUE_TEST=rich` 会显示一条带绿色锁名的测试对话；
`PIG4_RICH_PROMPT_TEST=1` 会显示带同样标签的准星提示。

调试 alpha-test 面片的 UV 时，可设置 `PIG4_DUMP_UV=<输出前缀>` 和
`PIG4_DUMP_UV_FRAMES=<帧号>`（默认 12）。会输出：

- `<前缀>_stage1_uv.png`：按三角形完整重算的 UV 图（R=u，G=v）；
- `<前缀>_stage1_alpha.png` / `_stage1_tex.png`：同一 UV 下的 alpha 采样和贴图；
- `<前缀>_stage1_actual_*.png`：正常 tile 路径最终接受的像素，用来和完整重算图对比。

例如：
`PIG4_DUMP_UV=build/uv PIG4_DUMP_UV_FRAMES=20`。

## Story

第一版剧情界面不运镜：相机和玩家留在原地，只在 3D 场景下方显示说话人和
一条文本。剧情期间移动、交互和背包拖放被冻结，但 3D 场景继续渲染。

- 点击画面、`Space`、`F` 或回车推进；最后一行的下一次推进结束剧情。
- 右上角“跳过”按钮和 `Esc` 直接结束整段剧情，随后恢复玩家控制。
- 目前只播放 `assets/story/plots.json` 里的 `line` / `narration`；
  `raw` 步骤（门、机关、选项、立绘、bgm 等）原样跳过，等我们按自己的配置重写。
- `assets/story/plots.json` 和 `opening.json` 由
  `python -I tools/extract_flash_story.py` 从 `flash_export/` 生成。

当前主线触发：

- 新游戏在 303 出生，自动播放 `opening` + `_7_Ruby:module_1`；
  剧情结束/跳过后直接获得纸团。
- 首次进入大堂播放 `_7_Ruby:module_2`（哭声）。
- 大堂大门按 F 首次播放 `_7_Ruby:module_6`，之后回退到普通 notice。

开发测试：

- `T` 键播放开场白；
- `PIG4_STORY=opening` 启动时播放开场白，
  `PIG4_STORY=_0_Dian:module_1` 播放指定 plot/module；
- `PIG4_STORY_ADVANCE=3` 在第一帧推进 3 行，方便截图；
- `PIG4_STORY_SKIP=1` 在第一帧点击“跳过”按钮，验证关闭与恢复控制；
- `PIG4_SHOW_STORIES=1` 让截图/调试模式也自动触发主线剧情；
- `PIG4_SKIP_INTRO=1` 跳过 303 开场，`PIG4_SKIP_STORY=1` 跳过房间自动剧情。

## Overlay framework

全屏界面只有一层外壳 `StageOverlay`：它负责背景压暗、拦截鼠标、管理焦点和
`setContent(QWidget*)`。内容 widget 继承很小的 `OverlayContent`，只需要：

- `handleContentKey()`：处理自己的按键；
- `closeRequested()` / `finishedRequested()`：请求关闭或结束；
- `backdropOpacity()`：告诉外壳要不要压暗场景。

`PuzzleView`、`ItemDetailView`、`StoryView` 都是内容 widget，不再是“半个外壳”。
`MainWindow::showOverlay()` / `hideOverlay()` 统一冻结/恢复玩家、准星和侧栏。

道具查看的中间区域再由 `ItemDetailView::setViewContent()` 挂一个
`ItemViewContent`；默认是 `ItemImageContent`（静态图片）。以后月亮积木、纸团
各自实现自己的内容 widget，触发器、状态机都放在内容里，外壳不用认识它们。

## Puzzles

Puzzle 是挂在 SPR 上的可交互实体。`building.json` 的 `puzzles` 按
`room + marker` 找到目标：

```json
"puzzles": [
  {
    "room": "room_303",
    "marker": "puzzle_303",
    "kind": "keypad",
    "interactive": true,
    "title": "机关",
    "prompt": "启动机关",
    "solution": "9132",
    "event": "puzzle_303.solved",
    "solvedText": "密码正确，门锁解除"
  }
]
```

- `interactive: false` 是线索类 puzzle：准星靠近时会高亮，但不响应 F。
- 每种 puzzle 继承 `Puzzle` 基类并实现自己的 `createView()`；目前实现了
  `KeypadPuzzle`（3×3 数字键盘，`KeypadPuzzleView`）、
  `PasswordKeypadPuzzle`（可配置字母密码器）和 `CluePuzzle`。
- `letter_keypad` / `password_keypad` 是可复用字母密码器：木纹面板和按键都由
  `puzzleart.cpp` 程序绘制，2D 界面和 3D sprite 共用同一张图。
- 2D 操作界面是外观的唯一来源：`PuzzleView::appearanceWidget()` 指向真正的
  puzzle 画面，`syncAppearance()` 把它渲染成一张图并通过
  `Puzzle::setWorldTexture()` 同步为 3D sprite 的贴图。2D 状态变化时只需再调用
  `syncAppearance()`，不需要维护第二套 3D 素材。
- `solution` 当前是 `9132`，来自月亮积木背面；事件由 `event` 发布。
  puzzle 只负责解锁，开门仍然是玩家的显式动作（按 F），不会在解谜时自动开门。
- 操作界面打开时玩家移动/视角冻结，鼠标可以自由点击；Esc 或下方三角关闭。
- `elevator_panel` 是电梯选层面板：面板本身就是传送锚点。选中楼层后按
  玩家相对当前面板的局部变换映射到目标楼层的同 id 面板，不做 3D 移动。
- id/name 含 `elevator` 的门使用灰色长方体占位，单扇门沿 marker `basisX`
  平移一个门宽（当前 3F 走廊是向西滑开），不依赖 door_9 样式资源。

### 字母密码器

`keys` 按行优先配置键面，`columns` 控制每行按键数，`solution` 的字符数就是
密码位数。`clearKey` / `backspaceKey` / `submitKey` 分别绑定清空、删一位、
提交动作（标签必须出现在 `keys` 中）；不配置 `submitKey` 时输满自动校验。
显示屏只画输入位数对应数量的绿色记号，不直接显示字母。

```json
{
  "room": "room_5wcorridor",
  "marker": "5W_puzzle1",
  "kind": "letter_keypad",
  "interactive": true,
  "solution": "retaw",
  "keys": ["a", "b", "c", "d", "e", "f", "r", "s", "t", "w", "z", "C"],
  "columns": 3,
  "clearKey": "C",
  "event": "5W_puzzle1.solved",
  "errorText": "没有反应……"
}
```

当前 5F 西走廊的密码就是纸团解出的 `RETAW`，不是倒过来的 `WATER`。
绿色 `C` 是清空键；普通小写 `c` 和清除键 `C` 按大小写区分。解开会解锁
旁边的台球室门 `5W_billard`，但不会自动开门，仍需玩家按 F。

### View events

`PuzzleView` 与 puzzle 之间走通用事件，view 不需要 include `puzzle.h`：

```cpp
struct PuzzleViewEvent
{
    QString type;
    QVariantMap payload;
};
```

- 出站：`viewEvent(PuzzleViewEvent)`，只描述用户做了什么；
- 入站：`applyEvent(PuzzleViewEvent)`，由 puzzle 推送显示状态或反馈；
- `appearanceChanged(QImage)`、`closeRequested()` 仍是基类的通用信号。

`PasswordKeypadView` 的当前事件表：

```text
out:
  key      { label: QString }
  closeRequested

in:
  setInput { text: QString }
  feedback { text: QString, durationMs: int }
```

`KeypadPuzzleView` 的当前事件表：

```text
out:
  digit    { value: int }

in:
  feedback { text: QString, durationMs: int }
```

`ShardPinkPuzzleView` 的当前事件表：

```text
out:
  take     { }
  drop     { itemId: QString }

in:
  setState { filled: bool, itemId: QString }
  feedback { text: QString, durationMs: int }
```

`ElevatorPanelView` 的当前事件表：

```text
out:
  floor    { floorId: QString }
  openDoor { }

in:
  setPanel { floors: QStringList, currentFloor: QString }
  feedback { text: QString, durationMs: int }
```

view 只保存临时显示状态；密码、槽位等持久状态仍在 `Puzzle` 里。每个 view 的
详细说明和参考图放在 `game/puzzle/views/<view>/README.md`；password 和
elevator 已经迁入各自目录，keypad/shard_pink 暂时留在 `puzzleui.cpp`。

## Item detail

背包选中道具后按 `R` 打开放大查看界面：

- `items.json` 的 `"content"` 可以指定自定义内容 widget；目前 `paper`
  是折叠/展开两种状态（展开后叠加沾水前的红色图案），`moon_block`
  点击正面翻到背面、再点翻回来。
- 普通多面道具仍保留左右箭头；带自定义 content 的道具不用箭头。
- `X`、`Esc` 或 `R` 关闭。
- 底部显示当前状态的说明文本。
- `assets/items.json` 配置每个道具的视图和组合配方；目前带一个
  遥控器 + 电池 → 装上电池的遥控器的演示配方。
- 可组合道具的下方会出现其它道具图标，点击即可组合。

调试入口：`PIG4_GIVE=combine` 给遥控器和电池，
`PIG4_OPEN_ITEM=moon_block` / `PIG4_OPEN_ITEM=remote` 直接打开查看界面。
`PIG4_ITEM_CLICK=1` 会点击内容区一次，用来验证纸团展开/月亮翻面。

调试入口：`PIG4_OPEN_PUZZLE=puzzle_303` 直接打开操作界面，
`PIG4_PUZZLE_KEY=8` 在第一帧模拟点击数字 8。
`PIG4_OPEN_PUZZLE=5W_puzzle1` 打开字母密码器，
`PIG4_PUZZLE_TEXT=retaw` 逐键输入密码。

## Assets

- `assets/hotel/building.json`：房间 grid、portal、相机落点；
- `assets/hotel/styles.json`：全局门/窗样式表；
- `assets/items.json`：道具放大视图和组合配方；
- `assets/images/`：原版导出的道具图；
- `assets/hotel/README.md`：PH marker、manifest 和烘焙流程。
