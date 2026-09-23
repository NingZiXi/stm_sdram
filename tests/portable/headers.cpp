#include "stm_sdram.h"
#include "stm_sdram_device.h"
#include "stm_sdram_controller.h"
static_assert(sizeof(sdram_handle_t) == sizeof(void *), "opaque handle");
