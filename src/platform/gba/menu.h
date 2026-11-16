#if GBA_TUTORIAL
static DeceptusU8 menu_active;
static DeceptusU16 menu_angle;
static DeceptusU8 menuRequested(void)
{
   return campaign.mode == DECEPTUS_CAMPAIGN_TITLE || campaign.mode == DECEPTUS_MENU_EXIT ||
          ((campaign.mode == DECEPTUS_MENU_OPTIONS || campaign.mode == DECEPTUS_MENU_CONTROLS) &&
           adventure.options_return == DECEPTUS_CAMPAIGN_TITLE);
}
static void menuPrepare(void)
{
   DeceptusU8 requested;
   requested = menuRequested();
   if (requested && !menu_active)
   {
      REG_DISPLAY = 1 << 6;
      dmaCopy(gba_menu_texture, (void *)0x06000000, 16384);
      dmaCopy(gba_menu_map, (void *)0x0600e000, 256);
      dmaCopy(gba_menu_palette, (void *)0x05000000, 512);
      dmaCopy(gba_font, (void *)0x0600c000, GBA_FONT_BYTES);
      REG16(0x050001e2) = 0x7fff;
      REG16(0x04000008) = (3 << 2) | (26 << 8);
      REG16(0x0400000c) = 3 | (28 << 8) | (1 << 13);
      hudReset();
      menu_active = 1;
   }
   else if (!requested && menu_active)
   {
      videoInit();
      backgroundReset();
      hudReset();
      menu_active = 0;
   }
   if (menu_active)
   {
      stream_changed = 0;
      menu_angle = (menu_angle + 3) & 4095;
   }
}
static void menuCommit(void)
{
   DeceptusI16 sine, cosine;
   const DeceptusI16 *table;
   table = (const DeceptusI16 *)gba_menu_sine;
   sine = table[menu_angle];
   cosine = table[(menu_angle + 1024) & 4095];
   REG16(0x04000020) = cosine;
   REG16(0x04000022) = -sine;
   REG16(0x04000024) = sine;
   REG16(0x04000026) = cosine;
   REG32(0x04000028) = (DeceptusU32)(16384 - (DeceptusI32)cosine * 120 + (DeceptusI32)sine * 80);
   REG32(0x0400002c) = (DeceptusU32)(16384 - (DeceptusI32)sine * 120 - (DeceptusI32)cosine * 80);
   REG16(0x04000010) = hud_scroll_x;
   REG16(0x04000012) = hud_scroll_y;
   REG16(0x04000050) = (3 << 6) | (1 << 2);
   REG16(0x04000054) = 7;
   REG_DISPLAY = 1 | (1 << 6) | (1 << 8) | (1 << 10);
}
#endif
