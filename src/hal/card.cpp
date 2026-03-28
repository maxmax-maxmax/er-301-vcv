#include <hal/log.h>
#include <hal/card.h>
#include <hal/events.h>

typedef struct sd
{
  int mode;
} sd_t;

static sd_t sd[2];

// Both cards are always present in VCV mode
static bool rearPresent = true;
static bool frontPresent = true;

void Card_init()
{
  sd[0].mode = CARD_MODE_NOT_CONNECTED;
  sd[1].mode = CARD_MODE_NOT_CONNECTED;
}

bool Card_mount(uint32_t drv)
{
  if (drv < 2)
  {
    if (sd[drv].mode == CARD_MODE_NOT_CONNECTED)
    {
      if (Card_connect(drv, CARD_MODE_FATFS))
      {
        return true;
      }
    }
  }
  return false;
}

void Card_unmount(uint32_t drv)
{
  if (Card_isMounted(drv))
  {
    Card_disconnect(drv);
  }
}

bool Card_isMounted(uint32_t drv)
{
  if (drv < 2)
  {
    return sd[drv].mode == CARD_MODE_FATFS;
  }
  return false;
}

bool Card_format(uint32_t drv)
{
  return false;
}

uint32_t Card_sizeInBlocks(uint32_t drv)
{
  return 0;
}

bool Card_readBlocks(uint32_t drv, uint8_t *buffer, uint32_t sector, uint32_t count)
{
  return false;
}

bool Card_readAlignedBlocks(uint32_t drv, uint8_t *buffer, uint32_t sector, uint32_t count)
{
  return false;
}

bool Card_writeBlocks(uint32_t drv, uint8_t *buffer, uint32_t sector, uint32_t count)
{
  return false;
}

void Card_printErrorStatus(void)
{
  logInfo("card 0: VCV mode");
  logInfo("card 1: VCV mode");
}

bool Card_isHighCapacity(uint32_t drv)
{
  return false;
}

uint32_t Card_getVersion(uint32_t drv)
{
  return 0;
}

uint32_t Card_getBusWidth(uint32_t drv)
{
  return 0;
}

uint32_t Card_getTransferSpeed(uint32_t drv)
{
  return 0;
}

bool Card_supportsCMD23(uint32_t drv)
{
  return false;
}

bool Card_isConnected(uint32_t drv)
{
  return sd[drv].mode != CARD_MODE_NOT_CONNECTED;
}

uint32_t Card_getMode(uint32_t drv)
{
  return sd[drv].mode;
}

bool Card_connect(uint32_t drv, uint32_t requestedMode)
{
  if (sd[drv].mode == (int)requestedMode)
  {
    return true;
  }

  Card_disconnect(drv);

  if (requestedMode == CARD_MODE_NOT_CONNECTED)
  {
    return true;
  }
  if (Card_isPresent(drv))
  {
    sd[drv].mode = requestedMode;
    switch (requestedMode)
    {
    case CARD_MODE_RAW:
      logDebug(1, "drv=%d connected in raw mode", drv);
      if (drv == CARD_REAR)
      {
        Events_push(EVENT_USB_REAR_CARD_MOUNT);
      }
      else
      {
        Events_push(EVENT_USB_FRONT_CARD_MOUNT);
      }
      break;
    case CARD_MODE_FATFS:
      logDebug(1, "drv=%d connected in fatfs mode", drv);
      break;
    default:
      break;
    }
    return true;
  }
  else
  {
    logWarn("Card(%d) is not present. Cannot connect.", drv);
    return false;
  }
}

void Card_disconnect(uint32_t drv)
{
  if (sd[drv].mode == CARD_MODE_RAW)
  {
    if (drv == CARD_REAR)
    {
      Events_push(EVENT_USB_REAR_CARD_UNMOUNT);
    }
    else
    {
      Events_push(EVENT_USB_FRONT_CARD_UNMOUNT);
    }
  }
  sd[drv].mode = CARD_MODE_NOT_CONNECTED;
}

bool Card_isPresent(uint32_t drv)
{
  if (drv == 0)
  {
    return rearPresent;
  }
  if (drv == 1)
  {
    return frontPresent;
  }
  return false;
}
