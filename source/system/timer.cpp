/**
 * \file timer.cpp
 * \author Sahak Grigoryan <sahakgrigoryan.am@gmail.com>
 * \brief Class implementation: TimerSystem
 */

//--------------------------------------------------------------------------
// includes
// own header
#include "garden/system/timer.hpp"

// C/C++ standard headers

// third-party library headers
#include "linear-pool.hpp"

// project-specific headers

namespace garden
{
    
TimerSystem::TimerSystem(void) :
    System{},
    Singleton<TimerSystem>{},
    handlerPool_{},
    activeHandlers_{},
    inactiveHandlers_{}
{
    // ensure: (lowest supported time ratio) <= millisecond
    static_assert(std::ratio_less_equal_v<ClockType::period, std::milli>);
    ECSM_SUBSCRIBE_TO_EVENT("Timer", TimerSystem::runTimers);
    ECSM_SUBSCRIBE_TO_EVENT("Timer", TimerSystem::disposeTimers);
}

TimerSystem::~TimerSystem(void) 
{
    ECSM_UNSUBSCRIBE_FROM_EVENT("Timer", TimerSystem::disposeTimers);
    ECSM_UNSUBSCRIBE_FROM_EVENT("Timer", TimerSystem::runTimers);

    for (const auto &it = activeHandlers_.cbegin(); it != activeHandlers_.cend(); )
        unregisterTimer(*it);

    disposeTimers();
}

void TimerSystem::runTimers(void)
{
    const TimePoint nowTime{getTime()};

    std::for_each(activeHandlers_.begin(), activeHandlers_.end(),
        [this, &nowTime](const ID<TimerHandler> &id) -> void {
            View<TimerHandler>  view    = handlerPool_.get(id);
            TimerHandler *const handler = *view;

            if (elapsedTimeSince(nowTime, handler->initTime) >= handler->duration) {
                handler->callback();
                if (handler->isOneShot)
                    inactiveHandlers_.push_back(id);
                else
                    handler->initTime += handler->duration;
            }
        }
    );

    std::for_each(inactiveHandlers_.cbegin(), inactiveHandlers_.cend(), 
        [this](const ID<TimerHandler> &id) -> void {
            unregisterTimer(id);
        }
    );
    inactiveHandlers_.clear();
}

ID<TimerSystem::TimerHandler> TimerSystem::registerTimer(const TimeUnit &duration, const TimerCallback &callback) 
{
    const ID<TimerHandler> id      = handlerPool_.create();
    View<TimerHandler>     view    = handlerPool_.get(id);
    TimerHandler *const    handler = *view;

    handler->initTime  = getTime();
    handler->duration  = duration;
    handler->callback  = callback;
    handler->isOneShot = false;
    
    activeHandlers_.insert(id);

    return id;
}

ID<TimerSystem::TimerHandler> TimerSystem::registerOneShotTimer(const TimeUnit &duration, const TimerCallback &callback)
{
    const ID<TimerHandler> id      = handlerPool_.create();
    View<TimerHandler>     view    = handlerPool_.get(id);
    TimerHandler *const    handler = *view;

    handler->initTime  = getTime();
    handler->duration  = duration;
    handler->callback  = callback;
    handler->isOneShot = true;
    
    activeHandlers_.insert(id);

    return id;
}

void TimerSystem::unregisterTimer(const ID<TimerHandler> eraseId)
{
    activeHandlers_.erase(eraseId);
    handlerPool_.destroy(eraseId);
}

} // namespace garden
