#pragma once
using namespace std;
#include "raylib.h"
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

class Entity;

class Component
{
public:
    virtual ~Component() = default;                 //能准确销毁
    virtual void Update(Entity& owner, float dt);   //能更新
    //owner组件属于哪个实体
};

class Entity
{
public:
    Entity() = default; //默认无参构造
    virtual ~Entity();

    // 禁止复制/拷贝
    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;

    // 获取包围盒->绘制大小、碰撞
    virtual BoundingBox GetBoundingBox() const = 0;
    
    virtual bool IsActive() const;  //查询存活状态
    void SetActive(bool active);    //true->活，false->死

    void UpdateComponents(float dt);    //更新实体的所有组件，如生命

    // 添加组件
    template <typename T, typename... Args>
    T* AddComponent(Args&&... args)
    {
        //T必须继承自Component
        static_assert(is_base_of<Component, T>::value, "T must derive from Component");

        // 根据传入参数创建具体组件对象
        unique_ptr<T> component(new T(forward<Args>(args)...));
        // 如AddComponent<AttackComponent>(1.0f, 10); -> new AttackComponent(1.0f, 10);

        // 保存组件
        // 返回的result是临时指针，交给调用者使用
        T* result = component.get();
        components_.push_back(move(component));
        return result;
    }

    // 查找组件
    template <typename T>
    T* GetComponent()
    {
        for (const unique_ptr<Component>& component : components_)
        {
            // 动态判断这个组件是不是要查的类型
            if (T* result = dynamic_cast<T*>(component.get()))
            {
                return result;
            }
        }
        // 没找到
        return nullptr;
    }

    // 查找组件--常量实体，只读
    template <typename T>
    const T* GetComponent() const
    {
        for (const unique_ptr<Component>& component : components_)
        {
            if (const T* result = dynamic_cast<const T*>(component.get()))
            {
                return result;
            }
        }
        return nullptr;
    }

private:
    vector<unique_ptr<Component>> components_;  //保管所有组件
    bool active_ = true;                        //默认初始为 活
};
