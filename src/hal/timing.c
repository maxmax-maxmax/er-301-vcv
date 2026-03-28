#include <hal/timing.h>
#include <hal/log.h>
#include <time.h>
#include <mach/mach_time.h>

static uint64_t offset = 0;
static mach_timebase_info_data_t timebase;

tick_t ticks(void)
{
  return (tick_t)mach_absolute_time();
}

double ticks2secsD(tick_t nticks)
{
  return (double)nticks * timebase.numer / timebase.denom / 1000000000.0;
}

float ticks2secs(tick_t nticks)
{
  return (float)((double)nticks * timebase.numer / timebase.denom / 1000000000.0);
}

float wallclock(void)
{
  uint64_t t = mach_absolute_time() - offset;
  return (float)((double)t * timebase.numer / timebase.denom / 1000000000.0);
}

void wallclock_reset(void)
{
  offset = mach_absolute_time();
}

unsigned long micros(void)
{
  return (unsigned long)(mach_absolute_time() * timebase.numer / timebase.denom / 1000);
}

void Timing_init(void)
{
  mach_timebase_info(&timebase);
  logInfo("--------Timing Init--------------");
  wallclock_reset();
  uint64_t freq = 1000000000ULL * timebase.denom / timebase.numer;
  logInfo("Tick Frequency: %llu Hz", freq);
  logInfo("Tick Period: %d ns", (int)(ticks2secsD(1) * 1000000000));
  logInfo("---------------------------------");
}
