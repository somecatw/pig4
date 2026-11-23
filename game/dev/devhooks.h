#ifndef DEVHOOKS_H
#define DEVHOOKS_H

class MainWindow;
class QKeyEvent;

// 所有 PIG4_* 无界面测试/调试钩子集中在这里。
// MainWindow 只保留几个调用点，不再混入测试代码。
class DevHooks
{
public:
    static bool loadDebugScene(MainWindow &window);
    static void startup(MainWindow &window);
    static void updateOverlay(MainWindow &window);
    static void updateGameplay(MainWindow &window);
    static bool handleKey(MainWindow &window, QKeyEvent *event);

private:
    static void unlockDoorCheat(MainWindow &window);
};

#endif // DEVHOOKS_H
