#include "Entity.h"

void Component::Update(Entity&, float)
{
}

Entity::~Entity() = default;

bool Entity::IsActive() const
{
    return active_;
}

void Entity::SetActive(bool active)
{
    active_ = active;
}

void Entity::UpdateComponents(float dt)
{
    for (const std::unique_ptr<Component>& component : components_)
    {
        if (component != nullptr)
        {
            component->Update(*this, dt);
        }
    }
}
