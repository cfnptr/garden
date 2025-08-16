# How to create game entitites?

In ECS, an entity is essentially a unique identifier, a container that represents a game object 
in your game world. Entity doesn't contain data or logic itself, it's just an ID. Components 
are attached to entities to give them data and properties like: Position, Velocity, Health, etc.

```cpp
#include "my-app/system/health.hpp"
#include "garden/system/transform.hpp"

//...

auto manager = Manager::Instance::get();
auto player = manager->createEntity();

auto transformView = manager->add<TransformComponent>(player);
transformView->setPosition(float3(1.0f, 2.0f, 3.0f));

auto healthView = manager->add<HealthComponent>(player);
healthView->value = 100.0f;

//...

auto transformView = TransformSystem::Instance::get()->getComponent(player);
if (transformView->getPosition().getY() < -1000.0f)
{
	auto healthView = manager->get<HealthComponent>(player);
	healthView->value -= 1.0f;
}

//...
```

For more **Entity** functions see the ECSM [documentation](https://cfnptr.github.io/ecsm/classecsm_1_1Entity.html).



# How to use LinearPool for custom item arrays?

You can use a **LinearPool** to create your own arrays of items. A linear pool stores item data 
contiguously in memory, which reduces RAM fragmentation and CPU cache invalidation. This also 
makes it convenient to process items in parallel across multiple asynchronous threads.

When you create an item in the pool, you receive its unique **ID**, which can be used to get a 
**View** of the item's data in memory. Note that you must not store this item view, because 
adding new items to the pool can invalidate memory pointers!

When an item is destroyed, its **ID** is recycled, and a newly allocated item may receive it again. 
Therefore, if needed, store a unique version number of the item to ensure that a given ID refers 
to the exact object you expect.

Calling item destruction does not free it immediately, it is only freed after calling pool's 
**dispose()** function. Therefore, after destroying an item, you can still access its data. 
(This is designed to simplify certain item handling logic in the game engine)

The second argument after the item type in the **LinearPool<..., false>** specifies whether the 
item has a **destroy()** function that should be called upon final destruction after **dispose()** call. 
This function also returns information about whether the item can actually be destroyed or is still busy 
for some reason (for example waiting or processing something).

```cpp
#include "ecsm.hpp"

using namespace ecsm;

struct MyItem
{
	float someData = 3.14f;
};

LinearPool<MyItem, false> items;

//...

auto newItem = items.create();
auto itemView = components.get(newItem);
itemView->someData = 0.0f;

//...

items.destroy(newItem);
items.dispose();
```

For more **LinearPool** functions see the ECSM [documentation](https://cfnptr.github.io/ecsm/classecsm_1_1LinearPool.html).