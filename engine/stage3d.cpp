#include "stage3d.h"
#include "render.h"
#include "raytest.h"
#include <QPaintEvent>
#include <QPainter>
#include <QFontMetrics>
#include <QCoreApplication>
#include <QAbstractTextDocumentLayout>
#include <QTextDocument>
#include <cmath>
using namespace std;

Stage3D::Stage3D(QWidget *parent)
    : QWidget{parent}
{
    activeCam = nullptr;
    root = new GameObject(this);
}

int ttttt;
void Stage3D::updateFrame(){
    auto frameStart = chrono::system_clock::now();

    // 调试 dump：每帧额外跑一遍 alpha-test UV 可视化，指定帧后落盘。
    static const QString debugUVPath = qEnvironmentVariable("PIG4_DUMP_UV");
    static const int debugUVDumpFrame = [] {
        int frame = qEnvironmentVariableIntValue("PIG4_DUMP_UV_FRAMES");
        return frame > 0 ? frame : 12;
    }();
    static int debugUVFrame = 0;
    setDebugUVCapture(!debugUVPath.isEmpty());
    if(!debugUVPath.isEmpty())
        ++debugUVFrame;

    static auto lastAnim = chrono::steady_clock::now();
    auto nowAnim = chrono::steady_clock::now();
    float dt = chrono::duration<float>(nowAnim - lastAnim).count();
    lastAnim = nowAnim;
    if (dt > 0.05f) dt = 0.05f;
    updateAnimations(root, dt);

    clearRenderBuffer();
    updateObjects(root, Transform());
    if(activeCam != nullptr){
        submitObjects(root);
        int pw = activeCam->camInfo.width * tileSize;
        int ph = activeCam->camInfo.height * tileSize;
        if(frameBuffer.isNull() || pw != frameBuffer.width() || ph != frameBuffer.height()){
            frameBuffer = QImage(pw, ph, QImage::Format_ARGB32);
        }
        drawFrame(activeCam->camInfo, (uint*)frameBuffer.bits());
        avgRenderTime = 1e6 / frameStat.fps;

    }
    QWidget::update();
    auto frameEnd = chrono::system_clock::now();
    double t = chrono::duration_cast<chrono::microseconds>(frameEnd - frameStart).count();
    frameTimes.push_back(t);
    if(frameTimes.size() > 100u) frameTimes.erase(frameTimes.begin());
    avgFrameTime = accumulate(frameTimes.begin(), frameTimes.end(), 0.0) / frameTimes.size();

    if(!debugUVPath.isEmpty() && debugUVFrame == debugUVDumpFrame)
        dumpDebugUV(debugUVPath);

    // 无界面截图模式：PIG4_ROOM_SHOT=<png path>，渲染若干帧后保存并退出
    const QString shotPath = qEnvironmentVariable("PIG4_ROOM_SHOT");
    if (!shotPath.isEmpty() && !frameBuffer.isNull()) {
        static int captureFrames = 0;
        int captureAfter = qEnvironmentVariableIntValue("PIG4_ROOM_SHOT_FRAMES");
        if (captureAfter <= 0) captureAfter = 10;
        if (++captureFrames == captureAfter) {
            if (frameBuffer.save(shotPath))
                qDebug() << "saved room test frame:" << shotPath;
            else
                qWarning() << "failed to save room test frame:" << shotPath;
            const QString uiShotPath = qEnvironmentVariable("PIG4_UI_SHOT");
            if (!uiShotPath.isEmpty()) {
                // 抓整个窗口，才能带上右侧栏和底部剧情条。
                QPixmap uiShot = window()->grab();
                if (uiShot.save(uiShotPath))
                    qDebug() << "saved UI frame:" << uiShotPath;
                else
                    qWarning() << "failed to save UI frame:" << uiShotPath;
            }
            QCoreApplication::quit();
        }
    }
}
void Stage3D::paintEvent(QPaintEvent *evt){
    if(activeCam == nullptr) return;
    if(frameBuffer.isNull()) return;
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    QImage img = frameBuffer.scaled(size(), Qt::KeepAspectRatio, Qt::FastTransformation);
    int x = (width() - img.width())/2;
    int y = (height() - img.height())/2;
    painter.drawImage(x, y, img);

    if(!reticleVisible) return;

    // 准星与交互提示（UI 层，不写进 frameBuffer）
    painter.setRenderHint(QPainter::Antialiasing, true);
    const int cx = width() / 2;
    const int cy = height() / 2;
    const QColor crossColor = interactionHover ? QColor(255, 210, 80)
                                               : QColor(255, 255, 255, 190);
    painter.setPen(QPen(crossColor, 2));
    painter.drawLine(cx - 8, cy, cx - 3, cy);
    painter.drawLine(cx + 3, cy, cx + 8, cy);
    painter.drawLine(cx, cy - 8, cx, cy - 3);
    painter.drawLine(cx, cy + 3, cx, cy + 8);

    if(!interactionPrompt.isEmpty()){
        QFont font = painter.font();
        font.setPointSize(12);
        font.setBold(true);
        painter.setFont(font);

        // 提示文本可能带颜色标签（例如锁门时绿色的钥匙名）。
        const bool rich = Qt::mightBeRichText(interactionPrompt);
        QTextDocument doc;
        int tw = 0, th = 0;
        if(rich){
            doc.setDefaultFont(font);
            doc.setDocumentMargin(0);
            doc.setDefaultStyleSheet(QStringLiteral("p { margin: 0; }"));
            doc.setHtml(interactionPrompt);
            // 先用足够大的 page width 测理想宽度，再按该宽度单行排版。
            doc.setTextWidth(100000.0);
            tw = int(std::ceil(doc.idealWidth()));
            th = int(std::ceil(doc.size().height()));
            doc.setTextWidth(tw + 1);
        }else{
            const QFontMetrics fm(font);
            tw = fm.horizontalAdvance(interactionPrompt);
            th = fm.height();
        }

        const QRect box(cx - tw/2 - 10, cy + 24, tw + 20, th + 8);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 170));
        painter.drawRoundedRect(box, 5, 5);

        if(rich){
            painter.save();
            painter.translate(box.left() + 10, box.top() + 4);
            QAbstractTextDocumentLayout::PaintContext ctx;
            ctx.palette.setColor(QPalette::Text, QColor(255, 255, 255));
            doc.documentLayout()->draw(&painter, ctx);
            painter.restore();
        }else{
            painter.setPen(QColor(255, 255, 255));
            painter.drawText(box, Qt::AlignCenter, interactionPrompt);
        }
    }

}

void Stage3D::updateAnimations(GameObject *rt, float dt) const{
    if(rt == nullptr) return;
    rt->tick(dt);
    for(GameObject *child: rt->children())
        updateAnimations(child, dt);
}
void Stage3D::updateObjects(GameObject *rt, const Transform &curr, bool ignoreFlag) const{
    if(rt == nullptr) return;
    rt->modified = false;
    for(GameObject *child: rt->children()){
        assert(child != nullptr);
        Transform ttfa = child->transform * curr;
        if(child->modified || ignoreFlag)
            child->updatePosition(ttfa);
        updateObjects(child, ttfa, ignoreFlag || child->modified);
    }
}
void Stage3D::submitObjects(GameObject *rt) const{
    if(rt == nullptr) return;
    for(GameObject *child: rt->children()){
        if (!child->isRenderVisible())
            continue;
        child->submitForRender();
        submitObjects(child);
    }
}

SceneRayHit Stage3D::raytestMesh(MeshActor *actor, const Ray &ray)const{
    if(actor == nullptr) return {};
    Transform global = actor->getGlobalTransform();
    Transform inv = global.inverseTransform();
    float scale = actor->getScale();
    Ray localRay = {ray.point * inv.rotation + inv.translation, ray.direction * inv.rotation};
    localRay.point /= scale;
    auto ttfa = raytestManager.meshIntersect(actor->meshID, localRay);
    if(ttfa.hit)
        return {actor, ttfa.leaf.triangleID, ttfa.dis*scale,
                scale*ttfa.pos*global.rotation+global.translation};
    return {};
}

SceneRayHit Stage3D::raytest(const Ray &ray)const{
    QList<SceneRayHit> res = root->forEach([&](MeshActor *actor) -> SceneRayHit{
        return raytestMesh(actor, ray);
    });
    if (res.isEmpty()) return {};
    return *min_element(res.begin(), res.end(), [](SceneRayHit a, SceneRayHit b){
        if(a.miss()) return false;
        if(b.miss()) return true;
        return a.dis < b.dis;
    });
}

StaticRayHit Stage3D::raytestStatic(const Ray &ray)const{
    if (!m_activeStaticCollisionTrees.isEmpty()) {
        StaticRayHit best;
        best.dis = 1e30f;
        for (const BVHTree<Triangle> *tree : m_activeStaticCollisionTrees) {
            if (tree == nullptr)
                continue;
            const auto hit = tree->intersect(ray);
            if (hit.hit && hit.dis < best.dis) {
                best.hit = true;
                best.dis = hit.dis;
                best.pos = hit.pos;
            }
        }
        return best;
    }

    if(raytestManager.staticSceneBVH.size() == 0) return {};
    auto hit = raytestManager.staticSceneBVH.intersect(ray);
    return {hit.hit, hit.dis, hit.pos};
}

// 过会给对象树也安排上 BVH

Ray Stage3D::pixelToRay(int x, int y) const{
    if(activeCam == nullptr || this->frameBuffer.isNull()) return {};
    int x1 = (float)x/this->size().width() * this->frameBuffer.width();
    int y1 = (float)y/this->size().height() * this->frameBuffer.height();
    return activeCam->pixelToRay(x1, y1);
}
namespace {

// AABB vs OBB 的分离轴检测；只返回一个撞到的面，供现有的滑墙/落地逻辑使用。
bool obbIntersectsAabb(const DynamicBoxCollider &collider, const BBox3D &box,
                       BoxtestResult &result)
{
    const Vec3 axes[3] = {
        collider.transform.rotation.row(0),
        collider.transform.rotation.row(1),
        collider.transform.rotation.row(2),
    };
    const float ext[3] = {
        collider.halfSize.x, collider.halfSize.y, collider.halfSize.z
    };
    const Vec3 worldAxes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const Vec3 boxCenter = {
        (box.x1 + box.x2) * 0.5f,
        (box.y1 + box.y2) * 0.5f,
        (box.z1 + box.z2) * 0.5f,
    };
    const Vec3 boxHalf = {
        (box.x2 - box.x1) * 0.5f,
        (box.y2 - box.y1) * 0.5f,
        (box.z2 - box.z1) * 0.5f,
    };
    const Vec3 delta = boxCenter - collider.transform.translation;

    float bestOverlap = 1e30f;
    Vec3 bestNormal = {0, 1, 0};

    auto testAxis = [&](Vec3 axis, bool faceAxis) -> bool {
        const float len = axis.len();
        if (len < 1e-6f)
            return true;
        axis /= len;

        const float boxRadius = boxHalf.x * std::abs(axis.x)
                              + boxHalf.y * std::abs(axis.y)
                              + boxHalf.z * std::abs(axis.z);
        float obbRadius = 0.0f;
        for (int i = 0; i < 3; ++i)
            obbRadius += ext[i] * std::abs(axis.dot(axes[i]));

        const float distance = std::abs(delta.dot(axis));
        const float overlap = boxRadius + obbRadius - distance;
        if (overlap <= 0.0f)
            return false;

        if (faceAxis && overlap < bestOverlap) {
            bestOverlap = overlap;
            bestNormal = axis * (delta.dot(axis) >= 0.0f ? 1.0f : -1.0f);
        }
        return true;
    };

    for (const Vec3 &axis : worldAxes)
        if (!testAxis(axis, true)) return false;
    for (const Vec3 &axis : axes)
        if (!testAxis(axis, true)) return false;
    for (const Vec3 &a : axes)
        for (const Vec3 &b : worldAxes)
            if (!testAxis(a.cross(b), false)) return false;

    result.normal = bestNormal;
    result.ymin = collider.transform.translation.y
                - (ext[0] * std::abs(axes[0].y)
                 + ext[1] * std::abs(axes[1].y)
                 + ext[2] * std::abs(axes[2].y));
    return true;
}

} // namespace

BBox3D boxTransform(const BBox3D &box, const Transform &transform){
    vector<Vec3> lst;
    for(float x:{box.x1, box.x2})
        for(float y:{box.y1, box.y2})
            for(float z:{box.z1, box.z2}){
                Vec3 v = {x, y, z};
                v = v*transform.rotation + transform.translation;
                lst.push_back(v);
            }
    BBox3D ret(lst);
    return ret;
}
set<BoxtestResult> Stage3D::boxtest(const BBox3D &box)const{
    set<BoxtestResult> result;
    if (!m_activeStaticCollisionTrees.isEmpty()) {
        for (const BVHTree<Triangle> *tree : m_activeStaticCollisionTrees) {
            if (tree == nullptr)
                continue;
            result.merge(tree->boxIntersect(box));
        }
    } else {
        result = raytestManager.sceneBoxIntersect(box);
    }
    for (auto it = dynamicColliders.constBegin(); it != dynamicColliders.constEnd(); ++it) {
        if (!it.value().enabled)
            continue;
        BoxtestResult hit;
        if (obbIntersectsAabb(it.value(), box, hit))
            result.insert(hit);
    }
    return result;
}

int Stage3D::addDynamicCollider(const Transform &worldTransform, const Vec3 &halfSize,
                                bool enabled)
{
    const int id = nextDynamicColliderId++;
    dynamicColliders.insert(id, {worldTransform, halfSize, enabled});
    return id;
}

void Stage3D::setDynamicColliderEnabled(int id, bool enabled)
{
    auto it = dynamicColliders.find(id);
    if (it != dynamicColliders.end())
        it.value().enabled = enabled;
}

void Stage3D::removeDynamicCollider(int id)
{
    dynamicColliders.remove(id);
}

void Stage3D::buildStaticBVH(){
    updateFrame();
    raytestManager.buildStaticBVH(root->forEach([](const MeshActor *actor)->Mesh{
        if(actor->isStatic)
            return actor->mesh;
        else return {};
    }));
    qDebug()<<"scene static BVH built:"<<raytestManager.staticSceneBVH.size();
}

void Stage3D::setActiveStaticCollisionTrees(
    const QList<const BVHTree<Triangle>*> &trees)
{
    m_activeStaticCollisionTrees = trees;
}

int Stage3D::staticBVHNodeCount() const
{
    if (m_activeStaticCollisionTrees.isEmpty())
        return int(raytestManager.staticSceneBVH.size());

    int nodes = 0;
    for (const BVHTree<Triangle> *tree : m_activeStaticCollisionTrees) {
        if (tree != nullptr)
            nodes += int(tree->size());
    }
    return nodes;
}

GameObject *Stage3D::loadObj(const QString &path, bool isStatic){
    GameObject *ret = assetManager.loadOBJ(path, isStatic);
    if(ret == nullptr) return nullptr;
    ret->forEach([this](MeshActor *curr){
        raytestManager.appendMesh(curr->mesh);
    });
    return ret;
}

int Stage3D::addRuntimeMesh(const Mesh &mesh){
    const int id = assetManager.addMesh(mesh);
    raytestManager.appendMesh(assetManager.getMeshes()[id]);
    return id;
}
