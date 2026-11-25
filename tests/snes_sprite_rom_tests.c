/* compare the assembly sprite drawing with the c reference inside an actual rom. */
#define SNES_SPRITES_REFERENCE 1
#include "sprites.c"

#define SNES_SPRITE_TEST_CASES 6000

volatile DeceptusU16 snes_sprite_test[16];
void snesClearGameRam(void);

typedef struct SnesSpriteTestState
{
   DeceptusU8 oam[SNES_OBJ_OAM_BYTES];
   DeceptusU16 line_load[224];
   SnesObjSlot slots[SNES_OBJ_PIECES];
   DeceptusU8 heads[SNES_SPRITE_RESOURCE_COUNT];
   DeceptusU16 count, unused, free_cursor;
   DeceptusU8 overflow, blank;
} SnesSpriteTestState;

static SnesSpriteTestState before_state, expected_state, actual_state;
static SnesObjRequest test_request;
static DeceptusU16 seed = 1729;

static DeceptusU16 randomWord(void)
{
   seed = seed * 109 + 89;
   return (DeceptusU16)((seed >> 8) | (seed << 8));
}

static void saveState(SnesSpriteTestState *state)
{
   memcpy(state->oam, snes_obj_oam, sizeof(state->oam));
   memcpy(state->line_load, snes_obj_line_load, sizeof(state->line_load));
   memcpy(state->slots, snes_obj_slots, sizeof(state->slots));
   memcpy(state->heads, snes_obj_heads, sizeof(state->heads));
   state->count = snes_obj_count;
   state->unused = snes_obj_unused;
   state->free_cursor = snes_obj_free_cursor;
   state->overflow = snes_sprite_overflow;
   state->blank = snes_sprite_blank;
}

static void loadState(const SnesSpriteTestState *state)
{
   memcpy(snes_obj_oam, state->oam, sizeof(state->oam));
   memcpy(snes_obj_line_load, state->line_load, sizeof(state->line_load));
   memcpy(snes_obj_slots, state->slots, sizeof(state->slots));
   memcpy(snes_obj_heads, state->heads, sizeof(state->heads));
   snes_obj_count = state->count;
   snes_obj_unused = state->unused;
   snes_obj_free_cursor = state->free_cursor;
   snes_sprite_overflow = state->overflow;
   snes_sprite_blank = state->blank;
}

/* a line holding objects and slivers up to its limits, often near them. */
static void loadLines(void)
{
   DeceptusU16 line, objects, slivers;
   for (line = 0; line < 224; ++line)
   {
      objects = randomWord() % 33;
      slivers = randomWord() % 35;
      if ((randomWord() & 3) != 0)
      {
         objects = objects / 8;
         slivers = slivers / 8;
      }
      snes_obj_line_load[line] = (DeceptusU16)(SNES_OBJ_LINE_EMPTY + objects + (slivers << 8));
   }
}

static void randomRequest(DeceptusU16 index)
{
   DeceptusU16 kind;
   const SnesSpriteResource *entry;
   test_request.resource = randomWord() % SNES_SPRITE_RESOURCE_COUNT;
   if ((index & 3) == 0)
   {
      /* a small set of resources, so pieces stay resident and slots are found. */
      test_request.resource = randomWord() % 24;
   }
   entry = &snes_sprite_resources[test_request.resource];
   test_request.x = (DeceptusI16)(randomWord() % 400) - 72;
   test_request.y = (DeceptusI16)(randomWord() % 368) - 72;
   kind = randomWord() & 7;
   if (kind == 0)
   {
      /* exactly at the clip edges. */
      test_request.x = (randomWord() & 1) ? -15 - (DeceptusI16)((entry->columns - 1) * 16) : 255;
      test_request.y = (randomWord() & 1) ? -15 - (DeceptusI16)((entry->rows - 1) * 16) : 223;
   }
   else if (kind == 1)
   {
      test_request.x = -16 - (DeceptusI16)(randomWord() & 15);
      test_request.y = 208 + (DeceptusI16)(randomWord() & 31);
   }
   test_request.flip = (DeceptusU8)(randomWord() & 1);
   test_request.priority = (DeceptusU8)(randomWord() & 3);
}

static DeceptusU16 compareStates(void)
{
   if (memcmp(expected_state.oam, actual_state.oam, sizeof(actual_state.oam)) != 0)
   {
      return 1;
   }
   if (memcmp(expected_state.line_load, actual_state.line_load, sizeof(actual_state.line_load)) != 0)
   {
      return 2;
   }
   if (memcmp(expected_state.slots, actual_state.slots, sizeof(actual_state.slots)) != 0)
   {
      return 3;
   }
   if (memcmp(expected_state.heads, actual_state.heads, sizeof(actual_state.heads)) != 0)
   {
      return 4;
   }
   if (expected_state.count != actual_state.count)
   {
      return 5;
   }
   if ((expected_state.unused != actual_state.unused) || (expected_state.free_cursor != actual_state.free_cursor))
   {
      return 6;
   }
   if ((expected_state.overflow != actual_state.overflow) || (expected_state.blank != actual_state.blank))
   {
      return 7;
   }
   return 0;
}

int main(void)
{
   DeceptusU16 index, palette, result;
   snesClearGameRam();
   consoleInit();
   snes_sprite_test[0] = 0x53a7;
   snes_sprite_test[1] = 0xd19e;
   snesObjResetCache();
   snesObjBeginReference();
   result = 0;
   for (index = 0; index < SNES_SPRITE_TEST_CASES; ++index)
   {
      if ((index & 15) == 0)
      {
         /* a new presentation: counters and lines start over, held pieces follow the used ones. */
         snesObjBeginReference();
         snes_obj_count = 0;
         snes_sprite_overflow = snes_sprite_blank = 0;
         snes_obj_free_cursor = 0;
         if ((index & 63) == 16)
         {
            loadLines();
         }
         if ((index & 255) == 32)
         {
            snes_obj_count = 120 + (randomWord() & 7);
         }
         if ((index & 1023) == 48)
         {
            snesObjResetCache();
         }
      }
      randomRequest(index);
      palette = randomWord() & 7;
      saveState(&before_state);
      snesObjDrawResourceReference(&test_request, palette);
      saveState(&expected_state);
      loadState(&before_state);
      snesObjDrawResource(&test_request, palette);
      saveState(&actual_state);
      snes_sprite_test[3] = index;
      snes_sprite_test[4] = test_request.resource;
      snes_sprite_test[5] = (DeceptusU16)test_request.x;
      snes_sprite_test[6] = (DeceptusU16)test_request.y;
      snes_sprite_test[7] = actual_state.count;
      snes_sprite_test[8] = expected_state.count;
      snes_sprite_test[10] = expected_state.overflow;
      snes_sprite_test[11] = test_request.flip;
      result = compareStates();
      snes_sprite_test[9] = result;
      if (result != 0)
      {
         break;
      }
      /* objects drawn so far, summed: shows that the cases reach the drawing. */
      snes_sprite_test[12] += actual_state.count - before_state.count;
      if (actual_state.overflow != before_state.overflow)
      {
         ++snes_sprite_test[13];
      }
      if (actual_state.unused != before_state.unused)
      {
         ++snes_sprite_test[14];
      }
      if (actual_state.blank != before_state.blank)
      {
         ++snes_sprite_test[15];
      }
   }
   snes_sprite_test[2] = (result == 0) ? 1 : 2;
   while (1)
   {
      WaitForVBlank();
   }
   return 0;
}
