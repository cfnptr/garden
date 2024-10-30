# How to add my custom component?

To create a custom component, you need to declare a new system that will store and manage 
your components. The component itself contains only data and functions to handle this 
data. All other component logic should reside in the system.

First, declare a new component in the header file of your system 
`include/my-app/system/my-system.hpp`, by inheriting it from the base **Component** structure.

#### my-system.hpp

```cpp
#pragma once
#include "ecsm.hpp"

namespace my::app
{

usign namespace ecsm;

struct MyCustomComponent : public Component
{
    int someValue = 123;
};

// Declare system here \/

}
```

Next, declare a new system by inheriting from **ComponentSystem<>**, which already includes 
some functions necessary for the system to work with components. You can also inherit from 
the base **System** class and manually define all required functions. After declaring your 
system’s functions in the header file, you also need to implement the logic for these 
functions in the system *.cpp* file `source/system/my-system.cpp`.

#### my-system.hpp

```cpp
//...

class MyCustomSystem : public ComponentSystem<MyCustomComponent, false>
{
    const string& getComponentName() const final;
    friend class ecsm::Manager;
};

//...
```

#### my-system.cpp

```cpp
#include "my-app/system/my-system.hpp"

using namespace my::app;

const string& MyCustomSystem::getComponentName() const
{
    static const string name = "My Custom";
    return name;
}
```

Once you have declared your component and its system, you need to create an 
instance of the system during the manager's initialization in the main function.

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

Now you can add, modify, or delete your component on an entity using the **Manager** 
instance to implement the desired functionality for the new component.

```cpp
//...

auto manager = Manager::Instance::get();
auto someEntity = manager->createEntity();
auto myCustomView = manager->add<MyCustomComponent>(someEntity);
myCustomView->someValue = 456;

//...

auto myCustomView = manager->get<MyCustomComponent>(someEntity);
if (myCustomView->someValue == 456)
    manager->remove<MyCustomComponent>(someEntity);
manager->destroy(someEntity);

//...
```

## Additionally

You can also inherit your system from the **Singleton<>** class for faster 
access to your system and the components added to an entity. (These functions 
bypass system lookup inside the **Manager**, which improves performance)

```cpp
//...

class MyCustomSystem : public ComponentSystem<MyCustomComponent, false>,
    public Singleton<MyCustomSystem>

//...
```

Faster system and entity component access:

```cpp
//...

auto myCustomSystem = MyCustomSystem::Instance::get();
auto myCustomView = myCustomSystem->getComponent(someEntity);
myCustomView->someValue = 789;

//...
```

#### See also [Systems](docs/Systems.md) tutorial <-