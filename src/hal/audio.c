#include <hal/audio.h>
#include <hal/log.h>
#include <od/config.h>
#include <stdbool.h>

// In VCV mode, audio is driven by Module::process(), not by a device callback.
// Audio_callback is still defined in pump.cpp and called from ER301Module.

static bool running = false;

void Audio_init()
{
  logInfo("Audio subsystem initialized (VCV Rack mode).");
}

void Audio_start(void)
{
  running = true;
  logInfo("Audio started (VCV Rack mode).");
}

void Audio_stop(void)
{
  running = false;
}

void Audio_restart(void)
{
  Audio_stop();
  Audio_start();
}

int Audio_getRate(void)
{
  return globalConfig.sampleRate;
}

void Audio_printErrorStatus(void)
{
  logInfo("audio: VCV Rack mode");
}

int Audio_getLoad()
{
  return 0;
}

bool Audio_running()
{
  return running;
}

uint32_t Audio_errorCount(void)
{
  return 0;
}
