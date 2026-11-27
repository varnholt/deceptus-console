#include "input.h"
#include "deceptus/engine.h"

/* the Amiga joystick encodes horizontal direction in the quadrature bits. */
DeceptusU16 amigaInputRead(void)
{
   DeceptusU16 joy, buttons;
   joy = *(volatile DeceptusU16*)0xdff00c;
   buttons = 0;
   if (joy & 0x0200)
   {
      buttons |= DECEPTUS_LEFT;
   }
   if (joy & 0x0002)
   {
      buttons |= DECEPTUS_RIGHT;
   }
   if (((joy >> 1) ^ joy) & 0x0100)
   {
      buttons |= DECEPTUS_UP;
   }
   if (((joy >> 1) ^ joy) & 0x0001)
   {
      buttons |= DECEPTUS_DOWN;
   }
   if (!(*(volatile DeceptusU8*)0xbfe001 & 0x80))
   {
      buttons |= DECEPTUS_JUMP;
   }
   return buttons;
}

int amigaInputQuit(void)
{
   /* focusing the emulator with a left click must not end the game. */
   return !(*(volatile DeceptusU8*)0xbfe001 & 0x40) && !(*(volatile DeceptusU16*)0xdff016 & 0x0400);
}
