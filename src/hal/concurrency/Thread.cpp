#include <emu/tls.h>
#include <od/extras/ReferenceCounted.h>
#include <hal/concurrency/Thread.h>
#include <hal/log.h>
#include <thread>
#include <chrono>

#ifdef BUILDOPT_VERBOSE
#include <typeinfo>
#endif

namespace od
{

  struct ThreadRunner
  {
    static void threadEntry(od::Thread *thread)
    {
      logAssert(thread);
      TLS_setName(thread->mName.c_str());
      logInfo("Thread starting.");
      thread->mThreadRunning = true;
      thread->run();
      thread->mThreadRunning = false;
    }
  };

  void Thread::sleep(uint32_t timeout)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
  }

  void Thread::yield()
  {
    std::this_thread::yield();
  }

  Thread::Thread(const char *name) : mName(name)
  {
    logDebug(1, "Thread(%s): constructor", name);
  }

  Thread::Thread(const char *name, int priority) : mName(name), mPriority(priority)
  {
    logDebug(1, "Thread(%s): constructor", name);
  }

  Thread::~Thread()
  {
    logDebug(1, "Thread(%s): destructor", mName.c_str());
    if (mThreadHandle)
    {
      stop();
      join();
    }
  }

  void Thread::start()
  {
    std::thread *t = new std::thread(ThreadRunner::threadEntry, this);
    mThreadHandle = (void *)t;
    if (mThreadHandle == 0)
    {
      logError("Failed to create thread.");
    }
  }

  bool Thread::running()
  {
    return mThreadRunning;
  }

  void Thread::stop()
  {
    mEvents.post(onThreadQuit);
  }

  void Thread::join()
  {
    if (mThreadHandle)
    {
      std::thread *t = (std::thread *)mThreadHandle;
      if (t->joinable())
      {
        t->join();
      }
      delete t;
      mThreadHandle = nullptr;
    }
    else
    {
      logError("Trying to join a thread that has not started.");
    }
  }

} /* namespace od */
