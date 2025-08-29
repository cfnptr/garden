# How to add my custom system?

In ECS *(Entity, Component, System)* pattern, a system is the part of the 
architecture that contains logic, it processes entities that have certain 
components and updates them according to game rules, physics, rendering, AI, etc.

First, create a new system C++ header file inside your application 
include folder `include/my-app/system/my-system.hpp`.

Then declare a new system by inheriting it from the base **System** class.

#### my-system.hpp

```cpp
#pragma once
#include "ecsm.hpp"

namespace my::app
{

using namespace ecsm;

class MyCustomSystem : public System
{
    MyCustomSystem();
    ~MyCustomSystem() final;

    friend class ecsm::Manager;
};

}
```

#### my-system.cpp

```cpp
#include "my-app/system/my-system.hpp"

using namespace my::app;

MyCustomSystem::MyCustomSystem()
{
    // Initalize your system...
}
~MyCustomSystem::MyCustomSystem()
{
    // Deinitalize your system...
}
```

Once you have declared your new system, you need to create an 
instance of the system during the **Manager**'s initialization in the main function.

#### main.cpp

```cpp
#include "garden/main.hpp"
#include "my-app/system/my-system.hpp"

using namespace garden;
using namespace my::app;

static void entryPoint()
{
    auto manager = new Manager();

    //...
    manager->createSystem<MyCustomSystem>();
    //...

    manager->initialize();
    manager->start();
    delete manager;
}

GARDEN_DECLARE_MAIN(entryPoint)
```

For more **System** functions see the ECSM [documentation](https://cfnptr.github.io/ecsm/classecsm_1_1System.html).



# How to subscribe to game events?

To execute some game logic, you need to subscribe your system to the 
required event from the **Manager**, for example an "Update" or "Render" event.

To do this, declare a new function inside your system that will be called 
when the event you subscribed to is triggered. The "Update" event is executed 
once every game tick (even if the game window is minimized).

#### my-system.hpp

```cpp
//...

class MyCustomSystem : public System
{
    MyCustomSystem();
    ~MyCustomSystem() final;

    void update(); 

    friend class ecsm::Manager;
};

//...
```

#### my-system.cpp

```cpp
//...

MyCustomSystem::MyCustomSystem()
{
    ECSM_SUBSCRIBE_TO_EVENT("Update", MyCustomSystem::update);
}
~MyCustomSystem::MyCustomSystem()
{
    ECSM_UNSUBSCRIBE_FROM_EVENT("Update", MyCustomSystem::update);
}

void MyCustomSystem::update()
{
    // Your system update logic
}
```

For more **Event** functions see the ECSM [documentation](https://cfnptr.github.io/ecsm/classecsm_1_1Manager.html).



# How to initialize multiple systems in the required order?

To init several game systems sequentially, you can subscribe them to different 
initialization stages: "PreInit", "Init", and "PostInit". These stages will be 
executed for all game systems in a strict order on application launch. Similarly, 
you can sequentially deinitialize systems using "PreDeinit", "Deinit", and "PostDeinit".

Note that a system's constructor will always be executed before any initialization events are called!



# How to create custom game events?

You can add a new game event at any time using the **Manager**. Events come in two types: 
*Ordered* and *Unordered*. Ordered events are executed once per application tick in a strict order, 
while unordered events can be triggered at any moment, and any number of times.

```cpp
//...

auto manager = Manager::Instance::get();
manager->registerEvent("MyUnorderedEvent");
manager->registerEventAfter("MyOrderedEvent", "Update");

//...

manager->runEvent("MyUnorderedEvent");

//...

manager->unregisterEvent("MyUnorderedEvent");

//...
```

For more **Manager** functions see the ECSM [documentation](https://cfnptr.github.io/ecsm/classecsm_1_1Manager.html).