#ifndef STAGE3D_H
#define STAGE3D_H

#include <QHash>
#include <QList>
#include <QWidget>
#include "assetmanager.h"
#include "gameobject.h"
#include "raytest.h"
#include <set>

struct SceneRayHit{
    MeshActor *actor = nullptr;
    uint triangleID;
    float dis;
    Vec3 pos;
    bool miss()const{
        return actor == nullptr;
    }
};

struct StaticRayHit{
    bool hit = false;
    float dis = 0.0f;
    Vec3 pos;
};

// 动态 OBB 碰撞体：门/窗等可移动物不进入静态 BVH，单独参与 boxtest。
struct DynamicBoxCollider{
    Transform transform;
    Vec3 halfSize;
    bool enabled = true;
};

class Stage3D : public QWidget
{
    Q_OBJECT
public:
    explicit Stage3D(QWidget *parent = nullptr);

    QImage frameBuffer;
    GameObject *root;
    Camera *activeCam;

    // UI 叠加状态：准星是否命中可交互物，以及提示文字
    bool interactionHover = false;
    QString interactionPrompt;
    // Puzzle 等全屏操作界面打开时隐藏准星/提示。
    bool reticleVisible = true;

    double avgFrameTime;
    double avgRenderTime;

    void updateFrame();
    // 调试场景/无 RoomManager 时仍可构建全局 BVH；房间模式改用每房间 BVH。
    void buildStaticBVH();
    // 当前参与静态碰撞/遮挡查询的房间 BVH；端口指向 Room 持有的树。
    void setActiveStaticCollisionTrees(
        const QList<const BVHTree<Triangle>*> &trees);
    GameObject *loadObj(const QString &path, bool isStatic=false);
    // 注册运行时生成的网格（manifest sprite 等），同时建立射线测试 BVH
    int addRuntimeMesh(const Mesh &mesh);
    Ray pixelToRay(int x, int y)const;
    SceneRayHit raytest(const Ray &ray)const;
    // 只测单个 MeshActor（动态物体单独维护时用）
    SceneRayHit raytestMesh(MeshActor *actor, const Ray &ray)const;
    // 只测 static BVH：用于判断准星到动态交互物之间有没有静态墙挡住
    StaticRayHit raytestStatic(const Ray &ray)const;
    std::set<BoxtestResult> boxtest(const BBox3D &box)const;

    // 动态碰撞体接口。返回的 id 用于开关碰撞。
    int addDynamicCollider(const Transform &worldTransform, const Vec3 &halfSize,
                           bool enabled = true);
    void setDynamicColliderEnabled(int id, bool enabled);
    void removeDynamicCollider(int id);

    // F3 / 调试面板用的只读统计。
    int staticBVHNodeCount() const;
    int activeStaticCollisionCount() const {
        return int(m_activeStaticCollisionTrees.size());
    }
    int runtimeMeshCount() const { return int(assetManager.getMeshes().size()); }
    int materialCount() const { return int(assetManager.getMaterials().size()); }
    int dynamicColliderCount() const { return dynamicColliders.size(); }
    QSize renderSize() const { return frameBuffer.size(); }

protected:
    void paintEvent(QPaintEvent *evt) override;
private:
    std::vector<double> frameTimes;
    void updateAnimations(GameObject *rt, float dt) const;
    void updateObjects(GameObject *rt, const Transform &c, bool ignoreFlag=false) const;
    void submitObjects(GameObject *rt) const;

    // AssetManager assetManager;
    RaytestManager raytestManager;
    QList<const BVHTree<Triangle>*> m_activeStaticCollisionTrees;
    QHash<int, DynamicBoxCollider> dynamicColliders;
    int nextDynamicColliderId = 1;

signals:
};

#endif // STAGE3D_H
