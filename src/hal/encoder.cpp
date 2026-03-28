#include <hal/encoder.h>
#include <atomic>

// Encoder value is set by the VCV widget, read by the engine
static std::atomic<int> encoderValue{0};
static int lastValue = 0;

// Called by the VCV widget to update encoder position
void VCV_setEncoderValue(int value)
{
  encoderValue.store(value, std::memory_order_relaxed);
}

void VCV_addEncoderDelta(int delta)
{
  encoderValue.fetch_add(delta, std::memory_order_relaxed);
}

extern "C"
{

  void Encoder_init(void)
  {
    lastValue = encoderValue.load(std::memory_order_relaxed);
  }

  int Encoder_getValue(void)
  {
    return encoderValue.load(std::memory_order_relaxed);
  }

  int Encoder_getChange(void)
  {
    int value = encoderValue.load(std::memory_order_relaxed);
    int change = value - lastValue;
    lastValue = value;
    return change;
  }
}
