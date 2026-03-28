#include <hal/display.h>
#include <hal/log.h>
#include <stdbool.h>
#include <atomic>

// Display buffers — engine writes, widget reads
static DisplayBuffer displayBuffers[2];
static int writeIndex = 0;
static std::atomic<DisplayBuffer *> lastPutBuffer{nullptr};
static bool started = false;

extern "C"
{

  void Display_init(void)
  {
    started = false;
    lastPutBuffer.store(nullptr, std::memory_order_relaxed);
  }

  void Display_deinit()
  {
    Display_stop();
  }

  void Display_start(void)
  {
    started = true;
  }

  void Display_stop(void)
  {
    started = false;
  }

  DisplayBuffer *Display_getBuffer()
  {
    if (started)
    {
      writeIndex = (writeIndex + 1) & 1;
      return &displayBuffers[writeIndex];
    }
    return 0;
  }

  void Display_putBuffer(DisplayBuffer *buffer)
  {
    lastPutBuffer.store(buffer, std::memory_order_release);
  }

  DisplayBuffer *Display_getLastPutBuffer()
  {
    return lastPutBuffer.load(std::memory_order_acquire);
  }
}
