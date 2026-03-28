#pragma once

#include <hal/display.h>

// VCV Rack replacement for emu/emu.h
// These functions are not used in VCV mode since our HAL files
// don't call into the emu namespace. This header exists only to
// satisfy includes that reference <emu/emu.h>.
