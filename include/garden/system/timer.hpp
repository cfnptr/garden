/**
 * \file timer.hpp
 * \author Sahak Grigoryan <sahakgrigoryan.am@gmail.com>
 * 
 * \brief Class definition: TimerSystem
 */

#pragma once

//--------------------------------------------------------------------------
// includes

// C/C++ standard headers
#include <chrono>
#include <unordered_set>

// third-party library headers
#include "ecsm.hpp"

// project-specific headers

//--------------------------------------------------------------------------
// forward declarations
namespace ecsm
{
template <class, bool>
class LinearPool;
} // namespace ecsm

//--------------------------------------------------------------------------
// class definition and member declarations
namespace garden
{

using ecsm::IdHash;
using ecsm::ID;
using ecsm::Singleton;
using ecsm::System;
using ecsm::View;

/**
 * \class TimerSystem
 * \brief Manages timer-based event handlers.
 */
class TimerSystem final : public System, public Singleton<TimerSystem>
{
public:
    using ClockType     = std::chrono::steady_clock;
    using TimeUnit      = std::chrono::milliseconds;
    using TimePoint     = std::chrono::time_point<ClockType, TimeUnit>;
    using TimerCallback = std::function<bool(void)>;

    /**
     * \brief Timer metadata structure.
     */
    struct TimerHandler {
        TimePoint     initTime; //< the time point of timer registration and refresh (\see registerTimer())
        TimeUnit      duration; //< the time duration (ms) that has to elapse before event handling
        TimerCallback callback; //< the event callback after time elapses
        bool          isOneShot; //< specifies if timer to be executed once (\see registerOneShotTimer()) or periodically (\see registerTimer())
    };

    /// constructor
    TimerSystem(void);

    /// destructor
    virtual ~TimerSystem(void) override final;

    /**
     * \brief Runs and manages all timer handlers; "Timer" ordered event handler.
     */
    void runTimers(void);

    /**
     * \brief Adds a periodic timer to the active handlers container.
     * 
     * \return Returns a valid and unique ID to the handlers memory pool.
     * 
     * \note Periodic timers are removed explicitly using the unregisterTimer() method.
     */
    ID<TimerHandler> registerTimer(const TimeUnit &duration, const TimerCallback &callback);

    /**
     * \brief Adds a one-shot timer to the active handlers container.
     * 
     * \return Returns a valid and unique ID to the handlers memory pool.
     * 
     * \note One-shot timers can be explicitly removed using the unregisterTimer() method or after their duration elapses.
     */
    ID<TimerHandler> registerOneShotTimer(const TimeUnit &duration, const TimerCallback &callback);

    /**
     * \brief Removes a timer from the active handlers container.
     */
    void unregisterTimer(const ID<TimerHandler> eraseId);

    /**
     * \brief Obtains the current time point in milliseconds from a monotonic clock.
     */
    static inline TimePoint getTime(void) noexcept
    {
        return std::chrono::time_point_cast<TimeUnit>(ClockType::now());
    }

    /**
     * \brief Returns the time duration elapsed since a given time point.
     */
    static inline TimeUnit elapsedTimeSince(const TimePoint &timePoint)
    {
        return std::chrono::duration_cast<TimeUnit>(getTime() - timePoint);
    }

    /**
     * \brief Returns the time duration between two time points.
     */
    static constexpr inline TimeUnit elapsedTimeSince(const TimePoint &timePoint1, const TimePoint &timePoint2)
    {
        return std::chrono::duration_cast<TimeUnit>(timePoint1 - timePoint2);
    }

protected:
private:
    using HandlerPool      = ecsm::LinearPool<TimerHandler, false>;
    using ActiveHandlers   = std::unordered_set<ID<TimerHandler>, IdHash<TimerHandler>>;
    using InactiveHandlers = std::vector<ID<TimerHandler>>;

    TimerSystem(TimerSystem &&)                 = delete;
    TimerSystem(const TimerSystem &)            = delete;
    TimerSystem &operator=(TimerSystem &&)      = delete;
    TimerSystem &operator=(const TimerSystem &) = delete;

    /**
     * \brief Wrapper method over memory pool releasing memory for handler objects.
     */
    void disposeTimers(void)
    {
        handlerPool_.dispose();
    }

    HandlerPool      handlerPool_; ///< the memory pool for timers
    ActiveHandlers   activeHandlers_; ///< hash table of registered timer IDs
    InactiveHandlers inactiveHandlers_; ///< an array of unregistered, but not yet released timers \see disposeTimers()
}; // class TimerSystem

} // namespace garden
