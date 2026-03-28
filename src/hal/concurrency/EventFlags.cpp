#include <hal/concurrency/EventFlags.h>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <climits>

namespace od
{

  struct Pimp
  {
    Pimp() {}
    ~Pimp() {}

    uint32_t pend(uint32_t all, uint32_t any, uint32_t timeout)
    {
      uint32_t matching;
      std::unique_lock<std::mutex> lock(mMutex);
      if (timeout == UINT_MAX)
      {
        mCond.wait(lock, [&]()
                   { return (matching = check(all, any)) != 0; });
      }
      else
      {
        mCond.wait_for(lock, std::chrono::milliseconds(timeout), [&]()
                       { return (matching = check(all, any)) != 0; });
        matching = check(all, any);
      }
      return matching;
    }

    void post(uint32_t flags)
    {
      {
        std::lock_guard<std::mutex> lock(mMutex);
        mPosted |= flags;
      }
      mCond.notify_all();
    }

    uint32_t getPosted()
    {
      std::lock_guard<std::mutex> lock(mMutex);
      return mPosted;
    }

  private:
    std::mutex mMutex;
    std::condition_variable mCond;
    uint32_t mPosted = 0;

    uint32_t check(uint32_t andMask, uint32_t orMask)
    {
      uint32_t matching;
      matching = orMask & mPosted;
      if ((andMask & mPosted) == andMask)
      {
        matching |= andMask;
      }
      if (matching)
      {
        mPosted &= ~matching;
      }
      return matching;
    }
  };

  EventFlags::EventFlags()
  {
    mHandle = (void *)new Pimp();
  }

  EventFlags::~EventFlags()
  {
    delete (Pimp *)mHandle;
  }

  void EventFlags::clear(uint32_t flags)
  {
    Pimp *pimp = (Pimp *)mHandle;
    pimp->pend(0, flags, 0);
  }

  void EventFlags::post(uint32_t flags)
  {
    Pimp *pimp = (Pimp *)mHandle;
    pimp->post(flags);
  }

  uint32_t EventFlags::getPosted()
  {
    Pimp *pimp = (Pimp *)mHandle;
    return pimp->getPosted();
  }

  uint32_t EventFlags::waitForAny(uint32_t flags)
  {
    Pimp *pimp = (Pimp *)mHandle;
    return pimp->pend(0, flags, UINT_MAX);
  }

  uint32_t EventFlags::waitForAny(uint32_t flags, uint32_t timeout)
  {
    Pimp *pimp = (Pimp *)mHandle;
    return pimp->pend(0, flags, timeout);
  }

  uint32_t EventFlags::waitForAll(uint32_t flags)
  {
    Pimp *pimp = (Pimp *)mHandle;
    return pimp->pend(flags, 0, UINT_MAX);
  }

  uint32_t EventFlags::waitForAll(uint32_t flags, uint32_t timeout)
  {
    Pimp *pimp = (Pimp *)mHandle;
    return pimp->pend(flags, 0, timeout);
  }

  uint32_t EventFlags::wait(uint32_t allFlags, uint32_t anyFlags)
  {
    Pimp *pimp = (Pimp *)mHandle;
    return pimp->pend(allFlags, anyFlags, UINT_MAX);
  }

  uint32_t EventFlags::wait(uint32_t allFlags, uint32_t anyFlags, uint32_t timeout)
  {
    Pimp *pimp = (Pimp *)mHandle;
    return pimp->pend(allFlags, anyFlags, timeout);
  }

} // namespace od
