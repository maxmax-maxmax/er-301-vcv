#include <hal/concurrency/Mutex.h>
#include <mutex>

namespace od
{

  Mutex::Mutex()
  {
    mHandle = (void *)new std::mutex();
  }

  Mutex::~Mutex()
  {
    delete (std::mutex *)mHandle;
  }

  void Mutex::enter()
  {
    ((std::mutex *)mHandle)->lock();
  }

  void Mutex::leave()
  {
    ((std::mutex *)mHandle)->unlock();
  }

  bool Mutex::tryEnter()
  {
    return ((std::mutex *)mHandle)->try_lock();
  }

} /* namespace od */
