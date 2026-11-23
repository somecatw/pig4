#ifndef GAMEOBJECT_H
#define GAMEOBJECT_H

#include <QObject>
#include <type_traits>
#include <concepts>
#include <utility>
#include "transform.h"
#include "structures.h"

class GameObject;

namespace detail {

template<class> struct first_arg_of;

template<class R, class C, class A, class... Rest>
struct first_arg_of<R (C::*)(A, Rest...)> { using type = A; };
template<class R, class C, class A, class... Rest>
struct first_arg_of<R (C::*)(A, Rest...) const> { using type = A; };
template<class R, class C, class A, class... Rest>
struct first_arg_of<R (C::*)(A, Rest...) noexcept> { using type = A; };
template<class R, class C, class A, class... Rest>
struct first_arg_of<R (C::*)(A, Rest...) const noexcept> { using type = A; };
template<class R, class A, class... Rest>
struct first_arg_of<R (*)(A, Rest...)> { using type = A; };
template<class R, class A, class... Rest>
struct first_arg_of<R (*)(A, Rest...) noexcept> { using type = A; };

template<class F>
using first_arg_t = typename first_arg_of<decltype(&F::operator())>::type;

template<class F, class = void>
struct actor_first_arg : std::false_type {};

template<class F>
struct actor_first_arg<F, std::void_t<first_arg_t<F>>>
    : std::bool_constant<
        std::is_pointer_v<first_arg_t<F>> &&
        std::is_base_of_v<GameObject,
            std::remove_cv_t<std::remove_pointer_t<first_arg_t<F>>>>> {};

template<class F, bool = actor_first_arg<F>::value>
struct const_actor_first_arg : std::false_type {};

template<class F>
struct const_actor_first_arg<F, true>
    : std::bool_constant<
        std::is_const_v<std::remove_pointer_t<first_arg_t<F>>>> {};

template<class F>
concept ActorFirstArg = actor_first_arg<F>::value;

template<class F>
concept ConstActorFirstArg = const_actor_first_arg<F>::value;

}

class GameObject : public QObject
{
    Q_OBJECT
public:

    explicit GameObject(QObject *parent = nullptr);

    // 相对于自身的变换
    void localTranslate(const Vec3 &v);
    void moveToLocalPos(const Vec3 &v);

    // 相对于父对象的变换
    void translate(const Vec3 &v);
    void rotateAroundAxis(const Vec3 &axis, float rad);

    void setTransform(const Transform &t);
    const Transform &getTransform()const;
    Transform getGlobalTransform() const;
    virtual void updatePosition(const Transform &t);
    virtual void submitForRender();
    // 每帧动画入口；默认不做事，Door 等动态对象覆盖它
    virtual void tick(float dt) { (void)dt; }

    GameObject *parent() const;
    QList<GameObject*> children() const;

    friend class Stage3D;

    // 从 lambda 的第一个参数推断 ObjectType：
    //   root->forEach([](MeshActor *actor){ ... });
    //   root->forEach([](const MeshActor *actor){ ... });
    // 非 void 返回值会自动收集成 QList<R>。
    template<class Func>
        requires detail::ActorFirstArg<std::decay_t<Func>> &&
                 std::invocable<Func&, detail::first_arg_t<std::decay_t<Func>>>
    auto forEach(Func &&func) {
        using F = std::decay_t<Func>;
        using ActorPtr = detail::first_arg_t<F>;
        using R = std::invoke_result_t<Func&, ActorPtr>;

        if constexpr (std::is_void_v<R>) {
            auto visit = [&](ActorPtr p) { func(p); };
            m_forEach<ActorPtr>(visit, static_cast<GameObject*>(this));
        } else {
            QList<R> out;
            auto visit = [&](ActorPtr p) { out.push_back(func(p)); };
            m_forEach<ActorPtr>(visit, static_cast<GameObject*>(this));
            return out;
        }
    }

    template<class Func>
        requires detail::ConstActorFirstArg<std::decay_t<Func>> &&
                 std::invocable<Func&, detail::first_arg_t<std::decay_t<Func>>>
    auto forEach(Func &&func) const {
        using F = std::decay_t<Func>;
        using ActorPtr = detail::first_arg_t<F>;
        using R = std::invoke_result_t<Func&, ActorPtr>;

        if constexpr (std::is_void_v<R>) {
            auto visit = [&](ActorPtr p) { func(p); };
            m_forEach<ActorPtr>(visit, static_cast<const GameObject*>(this));
        } else {
            QList<R> out;
            auto visit = [&](ActorPtr p) { out.push_back(func(p)); };
            m_forEach<ActorPtr>(visit, static_cast<const GameObject*>(this));
            return out;
        }
    }

protected:
    bool modified;
    Transform transform;

    template<class ActorPtr, class SelfPtr, class Visitor>
    void m_forEach(Visitor &visit, SelfPtr self) const {
        static_assert(
            !(std::is_const_v<std::remove_pointer_t<SelfPtr>> &&
              !std::is_const_v<std::remove_pointer_t<ActorPtr>>),
            "const ObjectType* is required when traversing a const tree");

        if (auto *casted = dynamic_cast<ActorPtr>(self))
            visit(casted);

        using ChildPtr = std::conditional_t<
            std::is_const_v<std::remove_pointer_t<SelfPtr>>,
            const GameObject*, GameObject*>;
        for (GameObject *child : self->children())
            m_forEach<ActorPtr>(visit, static_cast<ChildPtr>(child));
    }

signals:
};

class MeshActor : public GameObject{
    Q_OBJECT
public:
    uint meshID;
    Mesh mesh;
    bool isStatic;
    // 用于烘焙房间：在 submit 时覆盖 mesh/triangle 的 shaderConfig
    ushort shaderConfigOverride = 0;
    explicit MeshActor(uint _meshID, bool isStatic = false, QObject *parent = nullptr);
    void updatePosition(const Transform &t) override;
    void submitForRender() override;
    void setScale(float s);
    float getScale()const{return scale;}
protected:
    float scale = 1.0f;
};

class Camera : public GameObject{
    Q_OBJECT
public:
    CameraInfo camInfo;
    explicit Camera(const CameraInfo &info, QObject *parent = nullptr);
    void updatePosition(const Transform &t) override;
    Ray pixelToRay(int w, int h)const;
};

class ParallelLight: public GameObject{
    Q_OBJECT
public:
    CameraInfo lightInfo;
    // void updatePosition(const Transform &t) override;
};


#endif // GAMEOBJECT_H
