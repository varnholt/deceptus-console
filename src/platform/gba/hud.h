#if GBA_CAMPAIGN
static DeceptusU16 hud_map[1024] __attribute__((aligned(4)));
static DeceptusU8 hud_map_nonempty, hud_map_dirty;

static void hudText(DeceptusU16 row, DeceptusU16 column, const char *text)
{
   DeceptusU8 character;
   hud_map_nonempty = 1;
   hud_map_dirty = 1;
   while (*text && column < 30 && row < 20)
   {
      character = (DeceptusU8)*text++;
      if (character >= 'a' && character <= 'z')
      {
         character -= 'a' - 'A';
      }
      if (character < 32 || character > 127)
      {
         character = 32;
      }
      hud_map[row * 32 + column++] = (DeceptusU16)(character - 31) | (15 << 12);
   }
}
static void hudNumber(DeceptusU16 row, DeceptusU16 column, DeceptusU16 value, DeceptusU8 digits)
{
   char text[6];
   DeceptusU16 quotient;
   DeceptusU8 index;
   if (digits > 5)
   {
      digits = 5;
   }
   text[digits] = 0;
   for (index = digits; index > 0; --index)
   {
      quotient = value / 10;
      text[index - 1] = (char)('0' + value - quotient * 10);
      value = quotient;
   }
   hudText(row, column, text);
}
#if GBA_TUTORIAL
static DeceptusU16 hud_ui_selected = 65535, hud_ui_loaded = 65535;
static DeceptusU16 hud_scroll_x, hud_scroll_y, hud_prompt_start;
static const DeceptusInteraction *hud_prompt_action;
static DeceptusU8 hud_cue_pixels[64 * 32] __attribute__((aligned(4)));
static DeceptusU16 hud_cue_resource = 65535, hud_cue_x, hud_cue_y, hud_cue_bytes;
static DeceptusU8 hud_cue_dirty;
static DeceptusU8 hud_title_pixels[32 * 32] __attribute__((aligned(4)));
static DeceptusU16 hud_title_bytes;
static DeceptusU16 hud_title_cached_bytes;
static const char *hud_title_cached;
static DeceptusU8 hudTitleActive(void)
{
   return adventure.room_label_ticks && campaign.mode == DECEPTUS_CAMPAIGN_PLAYING;
}
static void hudReset(void)
{
   hud_ui_loaded = 65535;
   hud_ui_selected = 65535;
   hud_prompt_action = 0;
   hud_title_cached = 0;
   hud_map_nonempty = 1;
}
static void hudImage(DeceptusU16 resource, DeceptusI16 x, DeceptusI16 y)
{
   const GbaUiResource *image;
   const DeceptusU16 *cells;
   DeceptusU16 row, column;
   hud_map_nonempty = 1;
   hud_map_dirty = 1;
   image = &gba_ui_resources[resource];
   if (x < 0)
   {
      x = 0;
   }
   if (y < 0)
   {
      y = 0;
   }
   if (x + image->columns * 8 > 240)
   {
      x = 240 - image->columns * 8;
   }
   if (y + image->rows * 8 > 160)
   {
      y = 160 - image->rows * 8;
   }
   hud_scroll_x = (256 - x) & 255;
   hud_scroll_y = (256 - y) & 255;
   hud_ui_selected = resource;
   cells = (const DeceptusU16 *)(gba_ui_blob + image->map);
   for (row = 0; row < image->rows; ++row)
   {
      for (column = 0; column < image->columns; ++column)
      {
         hud_map[row * 32 + column] = cells[row * image->columns + column] | (15 << 12);
      }
   }
}
static void hudCue(DeceptusU16 resource, DeceptusI16 x, DeceptusI16 y)
{
   const GbaUiResource *image;
   const DeceptusU16 *cells;
   DeceptusU16 row, column, px, py, shift_x, shift_y, columns, rows, source_tile, destination;
   DeceptusI16 label_x, label_y;
   DeceptusU8 packed, value;
   hud_map_nonempty = 1;
   hud_map_dirty = 1;
   image = &gba_ui_resources[resource];
   cells = (const DeceptusU16 *)(gba_ui_blob + image->map);
   if (x < 0)
   {
      x = 0;
   }
   if (x > 192)
   {
      x = 192;
   }
   if (y < 0)
   {
      y = 0;
   }
   if (y > 112)
   {
      y = 112;
   }
   hud_ui_selected = resource;
   hud_scroll_x = (256 - (x & 7)) & 255;
   hud_scroll_y = (256 - (y & 7)) & 255;
   for (row = 0; row < 6; ++row)
   {
      for (column = 0; column < 6; ++column)
      {
         hud_map[((y >> 3) + row) * 32 + (x >> 3) + column] = cells[row * image->columns + column] | (15 << 12);
      }
   }
   label_x = 240 - image->columns * 8;
   label_y = 136;
   shift_x = (label_x - (x & 7)) & 7;
   shift_y = (label_y - (y & 7)) & 7;
   columns = image->columns + (shift_x ? 1 : 0);
   rows = 3 + (shift_y ? 1 : 0);
   hud_cue_bytes = columns * rows * 32;
   if (resource != hud_cue_resource || shift_x != hud_cue_x || shift_y != hud_cue_y)
   {
      for (destination = 0; destination < sizeof(hud_cue_pixels); ++destination)
      {
         hud_cue_pixels[destination] = 0;
      }
      for (row = 0; row < 24; ++row)
      {
         for (column = 0; column < image->columns * 8; ++column)
         {
            source_tile = cells[((48 + row) >> 3) * image->columns + (column >> 3)];
            if (source_tile >= 192)
            {
               source_tile -= 64;
            }
            packed = gba_ui_blob[image->graphics + source_tile * 32 + ((48 + row) & 7) * 4 + ((column & 7) >> 1)];
            value = (packed >> ((column & 1) * 4)) & 15;
            px = column + shift_x;
            py = row + shift_y;
            destination = ((py >> 3) * columns + (px >> 3)) * 32 + (py & 7) * 4 + ((px & 7) >> 1);
            hud_cue_pixels[destination] |= value << ((px & 1) * 4);
         }
      }
      hud_cue_resource = resource;
      hud_cue_x = shift_x;
      hud_cue_y = shift_y;
      hud_cue_dirty = 1;
   }
   label_x = (label_x - (x & 7)) >> 3;
   label_y = (label_y - (y & 7)) >> 3;
   for (row = 0; row < rows; ++row)
   {
      for (column = 0; column < columns; ++column)
      {
         hud_map[(label_y + row) * 32 + label_x + column] = (192 + row * columns + column) | (15 << 12);
      }
   }
}
static void __attribute__((section(".romtext"), noinline)) hudRoomTitle(void)
{
   const char *text;
   GbaObject *object;
   DeceptusU16 index, pixel, glyphs;
   DeceptusU8 character, packed, low, high;
   if (!hudTitleActive())
   {
      return;
   }
   text = active_stage->title;
   if (text != hud_title_cached)
   {
      for (index = 0; index < 28 && text[index]; ++index)
      {
         character = (DeceptusU8)text[index];
         if (character >= 'a' && character <= 'z')
         {
            character -= 'a' - 'A';
         }
         if (character < 32 || character > 127)
         {
            character = 32;
         }
         for (pixel = 0; pixel < 32; ++pixel)
         {
            packed = gba_font[(character - 31) * 32 + pixel];
            low = (packed & 15) ? 15 : 0;
            high = (packed & 240) ? 15 : 0;
            hud_title_pixels[index * 32 + pixel] = low | (high << 4);
         }
      }
      glyphs = (index + 1) & 0xfffe;
      if (glyphs != index)
      {
         for (pixel = 0; pixel < 32; ++pixel)
         {
            hud_title_pixels[index * 32 + pixel] = 0;
         }
      }
      hud_title_bytes = glyphs * 32;
      hud_title_cached_bytes = hud_title_bytes;
      hud_title_cached = text;
   }
   else
   {
      hud_title_bytes = hud_title_cached_bytes;
      glyphs = hud_title_bytes / 32;
   }
   /* fixed screen-space objects are independent of the moving cue's BG2 scroll. */
   for (index = 0; index < glyphs; index += 2)
   {
      if (object_count >= 128)
      {
         engine.content_error = 4;
         return;
      }
      object = &objects[object_count++];
      object->attribute0 = 16 | (1 << 14);
      object->attribute1 = 8 + index * 8;
      object->attribute2 = (3 * GBA_ACTOR_FRAME_BYTES / 32 + index) | (15 << 12);
   }
}
static void hudLines(DeceptusU16 row, const char *text)
{
   char line[29];
   DeceptusU8 length;
   while (*text && row < 19)
   {
      length = 0;
      while (*text && *text != '\n' && length < 28)
      {
         line[length++] = *text++;
      }
      line[length] = 0;
      hudText(row++, 1, line);
      if (*text == '\n')
      {
         ++text;
      }
   }
}
static void __attribute__((section(".romtext"), noinline)) hudPrepare(void)
{
   DeceptusU16 index;
   DeceptusI16 x, y, column;
   DeceptusU16 resource, elapsed;
   const DeceptusInteraction *action;
   const DeceptusAdventureRoom *room;
   const GbaUiPresentation *presentation;
   hud_map_dirty = 0;
   if (hud_map_nonempty)
   {
      for (index = 0; index < 1024; ++index)
      {
         hud_map[index] = 0;
      }
      hud_map_nonempty = 0;
      hud_map_dirty = 1;
   }
   hud_ui_selected = 65535;
   hud_scroll_x = 0;
   hud_scroll_y = 0;
   hud_cue_dirty = 0;
   hud_title_bytes = 0;
   if (campaign.mode != DECEPTUS_CAMPAIGN_PLAYING)
   {
      hud_cue_resource = 65535;
   }
   if (campaign.mode != DECEPTUS_CAMPAIGN_PLAYING)
   {
      hud_prompt_action = 0;
   }
   if (campaign.mode == DECEPTUS_CAMPAIGN_TITLE)
   {
      hudImage(gba_menu_main[adventure.selection], 0, 0);
      return;
   }
   if ((campaign.mode == DECEPTUS_DIALOGUE || campaign.mode == DECEPTUS_NOTIFICATION) && adventure.dialogue)
   {
      for (index = 0; index < GBA_UI_PRESENTATIONS; ++index)
      {
         presentation = &gba_ui_presentations[index];
         if (presentation->action == adventure.dialogue)
         {
            hudImage(presentation->page_first + adventure.page, 8, 8);
            return;
         }
      }
      hudLines(14, adventure.dialogue->pages[adventure.page]);
      return;
   }
   if (campaign.mode == DECEPTUS_MENU_PAUSE)
   {
      hudImage(gba_menu_pause[adventure.selection], 0, 0);
      return;
   }
   if (campaign.mode == DECEPTUS_MENU_INVENTORY)
   {
      hudImage(gba_menu_inventory[adventure.selection], 0, 0);
      return;
   }
   if (campaign.mode == DECEPTUS_MENU_CONTROLS)
   {
      hudImage(gba_menu_controls[0], 0, 0);
      return;
   }
   if (campaign.mode == DECEPTUS_MENU_OPTIONS)
   {
      hudImage(gba_menu_options[adventure.selection], 0, 0);
      return;
   }
   if (campaign.mode == DECEPTUS_MENU_CONFIRM)
   {
      hudImage(gba_menu_confirm[adventure.selection], 0, 0);
      return;
   }
   if (campaign.mode == DECEPTUS_MENU_EXIT)
   {
      hudImage(gba_menu_exit[0], 0, 0);
      return;
   }
   if (engine.content_error)
   {
      hudText(18, 2, "CONTENT LIMIT");
      hudNumber(18, 18, engine.content_error, 2);
      return;
   }
   room = &adventure.definition->rooms[campaign.room_index];

   if (adventure.prompt < room->interaction_count)
   {
      action = &room->interactions[adventure.prompt];
      for (index = 0; index < GBA_UI_PRESENTATIONS; ++index)
      {
         presentation = &gba_ui_presentations[index];
         if (presentation->action != action)
         {
            continue;
         }
         if (hud_prompt_action != action)
         {
            hud_prompt_action = action;
            hud_prompt_start = engine.ticks;
         }
         elapsed = (DeceptusU16)(engine.ticks - hud_prompt_start);
         resource = elapsed >= 28 ? presentation->cue_count - 1 : elapsed * 5 / 3;
         if (resource >= presentation->cue_count)
         {
            resource = presentation->cue_count - 1;
         }
         resource += presentation->cue_first;
         hudCue(resource, presentation->x - frame.camera_x - 24, presentation->y - frame.camera_y - 24);
         hudRoomTitle();
         return;
      }
      column = (deceptusFloor(engine.entities[0].x) - frame.camera_x) / 8 - 3;
      if (column < 1)
      {
         column = 1;
      }
      if (column > 20)
      {
         column = 20;
      }
      hudText(7, column, action->grant_item ? "UP TAKE" : action->cost_item ? "UP USE" : "UP");
   }
   else
   {
      hud_prompt_action = 0;
      x = deceptusFloor(engine.entities[0].x);
      y = deceptusFloor(engine.entities[0].y);
      for (index = 0; index < engine.mechanisms.count; ++index)
      {
         if (engine.mechanisms.definitions[index].kind == DECEPTUS_MECH_LEVER && x + 8 > engine.mechanisms.definitions[index].bounds.x &&
             x - 8 < engine.mechanisms.definitions[index].bounds.x + engine.mechanisms.definitions[index].bounds.width &&
             y > engine.mechanisms.definitions[index].bounds.y &&
             y - 30 < engine.mechanisms.definitions[index].bounds.y + engine.mechanisms.definitions[index].bounds.height)
         {
            hudText(7, 13, "UP");
            break;
         }
      }
   }
   hudRoomTitle();
}
#else
static void hudPrepare(void)
{
   DeceptusU16 index, minutes;
   for (index = 0; index < 1024; ++index)
   {
      hud_map[index] = 0;
   }
   if (campaign.mode == DECEPTUS_CAMPAIGN_TITLE)
   {
      hudText(5, 10, "DECEPTUS");
      hudText(7, 5, "GBA PLAYABLE PREVIEW");
      hudText(9, 6, "FOUR CATACOMB AREAS");
      hudText(11, 4, "ARROWS MOVE. A/B JUMP");
      hudText(12, 3, "UP INTERACT. START PAUSE");
      hudText(13, 3, "SELECT RESTARTS THE ROOM");
      hudText(16, 9, "PRESS START");
      return;
   }
   if (campaign.mode == DECEPTUS_CAMPAIGN_READY)
   {
      hudText(5, 10, "AREA");
      hudNumber(5, 15, campaign.room_index + 1, 1);
      hudText(7, 3, active_stage->title);
      hudText(10, 1, active_stage->hint1);
      hudText(12, 1, active_stage->hint2);
      hudText(16, 9, "PRESS START");
      return;
   }
   minutes = campaign.seconds / 60;
   if (campaign.mode == DECEPTUS_CAMPAIGN_FINISHED)
   {
      hudText(5, 7, "PREVIEW COMPLETE");
      hudText(7, 6, "THANKS FOR PLAYING");
      hudText(10, 8, "TIME");
      hudNumber(10, 15, minutes, 2);
      hudText(10, 17, ":");
      hudNumber(10, 18, campaign.seconds - minutes * 60, 2);
      hudText(12, 8, "DEATHS");
      hudNumber(12, 17, campaign.deaths, 3);
      hudText(13, 8, "RESTARTS");
      hudNumber(13, 17, campaign.restarts, 3);
      hudText(16, 6, "START TO PLAY AGAIN");
      return;
   }
   if (engine.paused)
   {
      hudText(9, 4, "PAUSED - START TO RESUME");
   }
   else if (deceptusCampaignAtGoal(&campaign))
   {
      hudText(18, 7, "UP TO CONTINUE");
   }
   if (engine.content_error)
   {
      hudText(18, 0, "CONTENT LIMIT ");
      hudNumber(18, 15, engine.content_error, 2);
   }
}
#endif
static void hudCommit(void)
{
#if GBA_TUTORIAL
   const GbaUiResource *image;
   DeceptusU16 count;
   if (hud_ui_selected != hud_ui_loaded)
   {
      if (hud_ui_selected == 65535)
      {
         dmaCopy(gba_font, (void *)0x0600c000, GBA_FONT_BYTES);
         REG16(0x050001e2) = 0x7fff;
         REG16(0x050001e6) = 0x0821;
      }
      else
      {
         image = &gba_ui_resources[hud_ui_selected];
         count = image->patterns < 128 ? image->patterns : 128;
         dmaCopy(gba_ui_blob + image->graphics, (void *)0x0600c000, count * 32);
         if (image->patterns > 128)
         {
            dmaCopy(gba_ui_blob + image->graphics + 4096, (void *)0x0600d800, (image->patterns - 128) * 32);
         }
         dmaCopy(gba_ui_blob + image->palette, (void *)0x050001e0, 32);
      }
      hud_ui_loaded = hud_ui_selected;
   }
   REG16(0x04000018) = hud_scroll_x;
   REG16(0x0400001a) = hud_scroll_y;
   if (hud_cue_dirty)
   {
      dmaCopy(hud_cue_pixels, (void *)0x0600d800, hud_cue_bytes);
   }
   if (hud_title_bytes)
   {
      dmaCopy(hud_title_pixels, (void *)(0x06010000 + 3 * GBA_ACTOR_FRAME_BYTES), hud_title_bytes);
      REG16(0x050003fe) = 0x7fff;
   }
#endif
#if GBA_TUTORIAL
   if (hud_map_dirty)
   {
      dmaCopy(hud_map, (void *)0x0600d000, sizeof(hud_map));
   }
#else
   dmaCopy(hud_map, (void *)0x0600d000, sizeof(hud_map));
#endif
}
#else
static void hudPrepare(void)
{
}
static void hudCommit(void)
{
}
#endif
