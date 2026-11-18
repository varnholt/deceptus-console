#include "keyboard.h"
#include "deceptus/engine.h"
#include <hardware/cia.h>
#include <hardware/custom.h>
#include <string.h>

#define KEY_SPACE 0x40
#define KEY_BACKSPACE 0x41
#define KEY_TAB 0x42
#define KEY_ENTER 0x43
#define KEY_RETURN 0x44
#define KEY_ESCAPE 0x45
#define KEY_UP 0x4c
#define KEY_DOWN 0x4d
#define KEY_RIGHT 0x4e
#define KEY_LEFT 0x4f
#define KEY_CONTROL 0x63

static volatile struct CIA *const cia = (struct CIA *)0xbfe001;
static volatile struct Custom *const hardware = (struct Custom *)0xdff000;
static DeceptusU8 keys[128];

void amigaKeyboardReset(void)
{
   memset(keys, 0, sizeof(keys));
}

static void acknowledge(void)
{
   DeceptusU8 control, line, count;
   control = cia->ciacra;
   cia->ciacra = control | CIACRAF_SPMODE;
   /* the hardware manual requires at least 85us; three line changes ensure >=128us. */
   line = (DeceptusU8)(hardware->vhposr >> 8);
   for (count = 0; count < 3; ++count)
   {
      while ((DeceptusU8)(hardware->vhposr >> 8) == line)
      {
      }
      line = (DeceptusU8)(hardware->vhposr >> 8);
   }
   cia->ciacra = control;
}

DeceptusU16 amigaKeyboardRead(void)
{
   DeceptusU8 serial, code;
   DeceptusU16 buttons;
   if (cia->ciaicr & CIAICRF_SP)
   {
      serial = cia->ciasdr;
      code = (DeceptusU8) ~((serial >> 1) | (serial << 7));
      acknowledge();
      if ((code & 127) < 0x78)
      {
         keys[code & 127] = !(code & 128);
      }
      else
      {
         amigaKeyboardReset();
      }
   }
   buttons = 0;
   if (keys[KEY_LEFT])
   {
      buttons |= DECEPTUS_LEFT;
   }
   if (keys[KEY_RIGHT])
   {
      buttons |= DECEPTUS_RIGHT;
   }
   if (keys[KEY_UP])
   {
      buttons |= DECEPTUS_UP;
   }
   if (keys[KEY_DOWN])
   {
      buttons |= DECEPTUS_DOWN;
   }
   if (keys[KEY_CONTROL])
   {
      buttons |= DECEPTUS_JUMP;
   }
   if (keys[KEY_SPACE])
   {
      buttons |= DECEPTUS_ATTACK;
   }
   if (keys[KEY_RETURN] || keys[KEY_ENTER])
   {
      buttons |= DECEPTUS_ACCEPT;
   }
   if (keys[KEY_ESCAPE])
   {
      buttons |= DECEPTUS_PAUSE | DECEPTUS_BACK;
   }
   if (keys[KEY_BACKSPACE])
   {
      buttons |= DECEPTUS_BACK;
   }
   if (keys[KEY_TAB])
   {
      buttons |= DECEPTUS_RESTART;
   }
   return buttons;
}
