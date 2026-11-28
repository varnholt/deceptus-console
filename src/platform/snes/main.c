#ifndef DECEPTUS_SNES_OPENING
#include <snes.h>
#include "asset_sizes.h"
#include "deceptus/game.h"
#include "deceptus/pacing.h"

extern char room_patterns, room_patterns_end;
extern char overflow_patterns, overflow_patterns_end;
extern char room_palette, room_palette_end;
extern char room_map, room_map_end;
extern char overflow_map, overflow_map_end;
extern char actor_patterns, actor_patterns_end;
extern char actor_palette, actor_palette_end;
extern const DeceptusRoom deceptus_scene;

static DeceptusEngine engine;
static DeceptusFrame frame;
#ifndef SNES_PACING_MAX_TICKS
#define SNES_PACING_MAX_TICKS 1
#endif
static DeceptusPacing pacing;
static DeceptusVideoCaps video = {256, 224, 12};
static DeceptusI16 accepted_top[DECEPTUS_DRAW_CAPACITY];
static DeceptusI16 accepted_bottom[DECEPTUS_DRAW_CAPACITY];

/* a stable little-endian debug mailbox for emulator integration tests. */
volatile DeceptusU16 deceptus_debug[16];

static DeceptusU16 inputRead(void)
{
   DeceptusU16 pad, buttons;
   pad = padsCurrent(0);
   buttons = 0;
   if (pad & KEY_LEFT)
   {
      buttons |= DECEPTUS_LEFT;
   }
   if (pad & KEY_RIGHT)
   {
      buttons |= DECEPTUS_RIGHT;
   }
   if (pad & (KEY_B | KEY_A))
   {
      buttons |= DECEPTUS_JUMP;
   }
   if (pad & KEY_UP)
   {
      buttons |= DECEPTUS_UP;
   }
   if (pad & KEY_START)
   {
      buttons |= DECEPTUS_PAUSE;
   }
   if (pad & KEY_SELECT)
   {
      buttons |= DECEPTUS_RESTART;
   }
   return buttons;
}

static void videoInit(void)
{
   setScreenOff();
   bgInitTileSet(0, (u8*)&room_patterns, (u8*)&room_palette, 0, ROOM_PATTERN_BYTES, ROOM_PALETTE_BYTES, BG_16COLORS, 0x1000);
   bgInitMapSet(0, (u8*)&room_map, 4096, SC_64x32, 0x0000);
   bgInitTileSet(1, (u8*)&overflow_patterns, (u8*)&room_palette, 0, OVERFLOW_PATTERN_BYTES, ROOM_PALETTE_BYTES, BG_16COLORS, 0x5000);
   bgInitMapSet(1, (u8*)&overflow_map, 4096, SC_64x32, 0x0800);
   oamInitGfxSet((u8*)&actor_patterns, ACTOR_PATTERN_BYTES, (u8*)&actor_palette, 64, 0, 0x6000, OBJ_SIZE16_L32);
   setMode(BG_MODE1, 0);
   bgSetDisable(2);
   bgSetScroll(0, 0, 0);
   bgSetScroll(1, 0, 0);
   oamClear(0, 0);
   setScreenOn();
}

static void videoPrepare(void)
{
   DeceptusU16 index, other, accepted, overlaps, object_id, tile_base, piece;
   DeceptusI16 x, y;
   const DeceptusDraw* draw;
   oamClear(0, 0);
   object_id = 0;
   accepted = 0;
   for (index = 0; index < frame.count; ++index)
   {
      draw = &frame.draws[index];
      overlaps = 0;
      for (other = 0; other < accepted; ++other)
      {
         if (draw->y < accepted_bottom[other] && draw->y + 48 > accepted_top[other])
         {
            ++overlaps;
         }
      }
      /* conservative interval budget: at most 8 actors overlap, hence <=16
       * objects and <=32 eight-pixel slivers on any scanline. */
      if (overlaps >= 8 || object_id > 128 * 4 - 6 * 4)
      {
         continue;
      }
      accepted_top[accepted] = draw->y;
      accepted_bottom[accepted] = draw->y + 48;
      ++accepted;
      tile_base = (draw->frame % 4) * 4 + (draw->frame / 4) * 96;
      for (piece = 0; piece < 6; ++piece)
      {
         x = draw->x + (draw->flip_x ? 1 - piece % 2 : piece % 2) * 16;
         y = draw->y + (piece / 2) * 16;
         /* skip vertically clipped pieces to prevent 8-bit OAM y wrapping. */
         if (y <= -16 || y >= 224 || x <= -16 || x >= 256)
         {
            continue;
         }
         oamSet(object_id, x, y, 3, draw->flip_x, 0, tile_base + (piece / 2) * 32 + (piece % 2) * 2, draw->palette);
         oamSetEx(object_id, OBJ_SMALL, OBJ_SHOW);
         object_id += 4;
      }
   }
}

static void debugWrite(void)
{
   deceptus_debug[0] = 0xD3CE;
   deceptus_debug[1] = deceptusFloor(engine.entities[0].x);
   deceptus_debug[2] = deceptusFloor(engine.entities[0].y);
   deceptus_debug[3] = engine.camera_x;
   deceptus_debug[4] = engine.ticks;
   deceptus_debug[5] = engine.paused;
   deceptus_debug[6] = engine.health;
   deceptus_debug[7] = engine.entities[0].grounded;
   deceptus_debug[8] = engine.door_entries;
   deceptus_debug[9] = engine.event_count;
   deceptus_debug[10] = frame.count;
   deceptus_debug[11] = engine.entities[1].active;
   deceptus_debug[12] = engine.checkpoint_id;
   deceptus_debug[13] = engine.checkpoint_x;
   deceptus_debug[14] = engine.checkpoint_y;
   deceptus_debug[15] = engine.content_error;
}

int main(void)
{
   DeceptusU16 previous_vblank, current_vblank, buttons;
   DeceptusU8 ticks;
   deceptusInit(&engine, &deceptus_scene, &video);
   videoInit();
   deceptusPacingInit(&pacing, snes_50hz ? 50 : 60, SNES_PACING_MAX_TICKS);
   previous_vblank = snes_vblank_count - 1;
   while (1)
   {
      current_vblank = snes_vblank_count;
      ticks = deceptusPacingTicks(&pacing, current_vblank - previous_vblank);
      previous_vblank = current_vblank;
      buttons = inputRead();
      while (ticks)
      {
         deceptusTick(&engine, buttons);
         --ticks;
      }
      deceptusRender(&engine, &frame);
      videoPrepare();
      debugWrite();
      WaitForVBlank();
      /* only register writes occur here; all OAM work is buffered above. */
      bgSetScroll(0, frame.camera_x, frame.camera_y);
      bgSetScroll(1, frame.camera_x, frame.camera_y);
      setBrightness(engine.paused ? 7 : 15);
   }
   return 0;
}
#else
#include <snes.h>
#include "deceptus/adventure.h"
#include "deceptus/game.h"
#include "deceptus/pacing.h"
#include "snes_assets.h"

/* the simulation runs at 60 ticks per second; a presentation that takes longer than a display frame
 * catches up with up to three ticks, as on the Amiga, so the game keeps its speed down to 20
 * presentations per second. */
#ifndef SNES_PACING_MAX_TICKS
#define SNES_PACING_MAX_TICKS 3
#endif
/* every picture stays on screen for at least this many display frames. pictures that alternate
 * between one and two frames move the game by one tick, then two, which reads as judder; an even
 * 30 per second (25 on PAL) with two ticks each looks smoother, and the SA-1 prepares half as many
 * pictures. */
#define SNES_FRAMES_PER_PICTURE 2
typedef char pacing_covers_a_held_picture[(SNES_PACING_MAX_TICKS >= SNES_FRAMES_PER_PICTURE) ? 1 : -1];
static DeceptusPacing pacing;
volatile DeceptusU16 deceptus_debug[56];
/* zeroes the game's work RAM globals (game_ram.asm), which the sdk start-up leaves uncleared. */
void snesClearGameRam(void);

#ifdef SNES_SA1
#include "system/sa1.h"
#include "system/sa1_mailbox.h"
/* the SA-1 stack low-water mark, copied to work RAM for the emulator checks. */
DeceptusU16 snes_sa1_stack_low_water;
/* the SA-1 runs the game and keeps its state in BW-RAM; this cpu reads it through pointers only. */
#define SNES_GAME (&snes_sa1_adventure)
#define SNES_FRAME (&snes_sa1_frame)
#else
extern const DeceptusAdventureDef snes_tutorial_definition;
static const DeceptusVideoCaps video = {256, 224, DECEPTUS_DRAW_CAPACITY};
static DeceptusAdventure adventure;
static DeceptusFrame frame;
#define SNES_GAME (&adventure)
#define SNES_FRAME (&frame)
#endif

#include "mechanisms/storm.h"
#include "snes_audio.h"
#include "system/ppu_timing.h"
#include "video/background.h"
#include "video/sprites.h"
#include "video/ui.h"

/* snesmod's soundbank (build_snes.py), read by the S-CPU through its long address. */
extern char SOUNDBANK__;
/* full volume, centred: spcEffect's volume * 16 + pan. */
#define EFFECT_VOLUME_PAN (15 * 16 + 8)

/* the module each track number plays (include/deceptus/music.h); entry 0, silence, is unused. */
static const u16 track_modules[SNES_MUSIC_TRACK_COUNT] = SNES_MUSIC_MODULES;
/* the track snesmod plays; it follows the game's. */
static DeceptusU8 music_track;

/* loads a module and, after it, the effects: they are its first module's samples, kept above the song. */
static void loadModule(u16 module)
{
   DeceptusU16 effect;
   spcLoad(module);
   for (effect = 0; effect < SNES_EFFECT_COUNT; ++effect)
   {
      spcLoadEffect(effect);
   }
}

/* snesmod on the SPC700 plays the song by itself. the first track's module comes in at start-up, so
   the effects play before any song does; silence until the game names a track. */
static void audioStart(void)
{
   spcBoot();
   spcSetBank(&SOUNDBANK__);
   spcStop();
   loadModule(track_modules[1]);
   music_track = 0;
}

/* a new track starts from its beginning; silence only stops the song, the effects stay loaded. */
static void followMusic(void)
{
   DeceptusU8 track;
   track = SNES_GAME->music.track;
   if (track == music_track)
   {
      return;
   }
   music_track = track;
   spcStop();
   if (track != 0)
   {
      loadModule(track_modules[track]);
      spcPlay(0);
   }
}

/* the cues the ticks queued, while the SA-1 rests between presentations; snesmod numbers its effects
   from 0, the cues from 1. */
static void audioPlayCues(void)
{
   SoundQueue* queue;
   DeceptusU8 index;
   queue = &SNES_GAME->sounds;
   for (index = 0; index < queue->count; ++index)
   {
      spcEffect(SNES_EFFECT_PITCH, (u16)(queue->cues[index] - 1), EFFECT_VOLUME_PAN);
   }
   queue->count = 0;
   spcProcess();
}

#define SNES_HVBJOY (*(volatile DeceptusU8*)0x4212)
#define SNES_JOY1 (*(volatile DeceptusU16*)0x4218)
#define SNES_JOYPAD_READING 0x01
/* a standard pad reports zero in the low four bits; anything else counts as no pad. */
#define SNES_JOYPAD_SIGNATURE 0x000F

/* the pad the last read returned, for the emulator checks. */
static DeceptusU16 snes_pad;

/* the pad as the automatic read at the last vblank start left it, once that read is done. the sdk
 * copies it only in vblanks its own WaitForVBlank waited for, which this loop no longer uses. */
static DeceptusU16 padRead(void)
{
   DeceptusU16 pad;
   while ((SNES_HVBJOY & SNES_JOYPAD_READING) != 0)
   {
   }
   pad = SNES_JOY1;
   if ((pad & SNES_JOYPAD_SIGNATURE) != 0)
   {
      pad = 0;
   }
   snes_pad = pad;
   return pad;
}

static DeceptusU16 inputRead(void)
{
   DeceptusU16 pad, buttons;
   pad = padRead();
   buttons = 0;
   if (pad & KEY_LEFT)
   {
      buttons |= DECEPTUS_LEFT;
   }
   if (pad & KEY_RIGHT)
   {
      buttons |= DECEPTUS_RIGHT;
   }
   if (pad & KEY_A)
   {
      buttons |= DECEPTUS_JUMP | DECEPTUS_ACCEPT;
   }
   if (pad & KEY_B)
   {
      buttons |= DECEPTUS_JUMP | DECEPTUS_BACK;
   }
   if (pad & KEY_UP)
   {
      buttons |= DECEPTUS_UP;
   }
   if (pad & KEY_DOWN)
   {
      buttons |= DECEPTUS_DOWN;
   }
   if (pad & KEY_START)
   {
      buttons |= DECEPTUS_PAUSE;
   }
   if (pad & KEY_SELECT)
   {
      buttons |= DECEPTUS_RESTART;
   }
   if (pad & (KEY_X | KEY_Y))
   {
      buttons |= DECEPTUS_ATTACK;
   }
   if (pad & KEY_L)
   {
      buttons |= DECEPTUS_ACCEPT;
   }
   if (pad & KEY_R)
   {
      buttons |= DECEPTUS_BACK;
   }
   return buttons;
}

static void debugWrite(DeceptusU8 buffer)
{
   const DeceptusAdventure* game;
   const DeceptusEngine* engine;
   game = SNES_GAME;
   engine = &game->campaign.engine;
   deceptus_debug[0] = 0xD3CE;
   deceptus_debug[1] = deceptusFloor(engine->entities[0].x);
   deceptus_debug[2] = deceptusFloor(engine->entities[0].y);
   deceptus_debug[3] = engine->camera_x;
   deceptus_debug[4] = engine->ticks;
   deceptus_debug[5] = engine->paused;
   deceptus_debug[6] = engine->health;
   deceptus_debug[7] = engine->entities[0].grounded;
   deceptus_debug[8] = engine->door_entries;
   deceptus_debug[9] = engine->event_count;
   deceptus_debug[10] = SNES_FRAME->count;
   deceptus_debug[12] = engine->checkpoint_id;
   deceptus_debug[13] = engine->checkpoint_x;
   deceptus_debug[14] = engine->checkpoint_y;
   deceptus_debug[15] = engine->content_error;
   deceptus_debug[16] = engine->camera_y;
   deceptus_debug[17] = snesBackgroundError();
   deceptus_debug[18] = snesBackgroundUploads();
   deceptus_debug[19] = snesBackgroundFull(buffer);
   deceptus_debug[20] = snes_ui_overflow;
   deceptus_debug[21] = snesSpritesOverflow();
   deceptus_debug[22] = snesSpritesUploadBytes(buffer);
   deceptus_debug[23] = snesSpritesObjects();
   deceptus_debug[24] = snesSpritesPalettes(buffer);
   deceptus_debug[25] = snesSpritesBlank(buffer);
   deceptus_debug[26] = engine->previous_buttons;
   deceptus_debug[27] = snes_pad;
   deceptus_debug[28] = game->campaign.room_index;
   deceptus_debug[29] = game->campaign.mode;
   deceptus_debug[30] = game->campaign.deaths;
   deceptus_debug[31] = game->campaign.seconds;
   deceptus_debug[32] = engine->projectiles_fired;
   deceptus_debug[33] = game->campaign.restarts;
   deceptus_debug[36] = game->flags;
   deceptus_debug[37] = game->inventory.counts[1];
   deceptus_debug[38] = game->inventory.counts[2];
   deceptus_debug[39] = game->page;
   deceptus_debug[40] = engine->entities[0].velocity_x;
   deceptus_debug[41] = engine->entities[0].velocity_y;
   deceptus_debug[42] = engine->entities[0].x;
   deceptus_debug[43] = engine->entities[0].y;
   deceptus_debug[44] = game->campaign.previous_buttons;
   deceptus_debug[46] = game->inventory.counts[3];
   deceptus_debug[47] = game->inventory.counts[4];
   deceptus_debug[48] = game->inventory.counts[5];
   deceptus_debug[49] = game->inventory.counts[6];
   deceptus_debug[50] = game->pending_ticks;
   deceptus_debug[51] = game->has_sword;
   deceptus_debug[52] = game->inventory.counts[7];
   deceptus_debug[53] = engine->player_rope.attached;
   deceptus_debug[54] = engine->player_rope.rope_index;
   deceptus_debug[55] = engine->player_rope.node_index;
}

/* the screen goes back on after a commit only when it was actually blanked. */
static DeceptusU8 screen_blanked;

#ifdef SNES_SA1
/* hands one command to the SA-1, which runs it while this cpu goes on. */
static void gameRequestStart(DeceptusU16 command)
{
   volatile SnesSa1Mailbox* mailbox;
   mailbox = SNES_SA1_MAILBOX;
   mailbox->command = command;
   ++mailbox->request;
}

/* waits until the SA-1 has done the last command. */
static void gameRequestWait(void)
{
   volatile SnesSa1Mailbox* mailbox;
   mailbox = SNES_SA1_MAILBOX;
   while (mailbox->done != mailbox->request)
   {
   }
}

static void gameRequest(DeceptusU16 command)
{
   gameRequestStart(command);
   gameRequestWait();
}

static void gameStart(void)
{
   volatile SnesSa1Mailbox* mailbox;
   mailbox = SNES_SA1_MAILBOX;
   mailbox->request = 0;
   mailbox->done = 0;
   snesSa1Start();
   gameRequest(SNES_SA1_INIT);
}

/* starts up to ticks simulation ticks on the SA-1, the rendering of their frame and its preparation
 * into the presentation buffer. */
static void gamePresentStart(DeceptusU16 buttons, DeceptusU8 ticks, DeceptusU8 buffer)
{
   volatile SnesSa1Mailbox* mailbox;
   mailbox = SNES_SA1_MAILBOX;
   mailbox->buttons = buttons;
   mailbox->ticks = ticks;
   mailbox->buffer = buffer;
   gameRequestStart(SNES_SA1_PRESENT);
}

/* waits for the started presentation; nonzero when a tick entered another room, which ends the
 * ticks early and leaves the frame unprepared. */
static DeceptusU8 gamePresentFinish(void)
{
   volatile SnesSa1Mailbox* mailbox;
   mailbox = SNES_SA1_MAILBOX;
   gameRequestWait();
   snes_sa1_stack_low_water = mailbox->stack_low_water;
   return (DeceptusU8)mailbox->room_changed;
}

/* the background data and sprite cache of the current room; their video half has run already. */
static void gameLoadRoom(void)
{
   gameRequest(SNES_SA1_LOAD_ROOM);
}

/* the presentation work that needs no video access: scenery flags, the background window, the
 * sprites and their staged transfers, into the presentation buffer. */
static void gamePrepare(DeceptusU8 buffer)
{
   volatile SnesSa1Mailbox* mailbox;
   mailbox = SNES_SA1_MAILBOX;
   mailbox->buffer = buffer;
   gameRequest(SNES_SA1_PREPARE);
}
#else
static void gameStart(void)
{
   deceptusAdventureInit(&adventure, &snes_tutorial_definition, &video);
}

static void gameLoadRoom(void)
{
   snesBackgroundLoad(adventure.campaign.room_index);
   snesSpritesReset();
   snesStormReset();
}

static void gamePrepare(DeceptusU8 buffer)
{
   snesBackgroundSetFlags(deceptusAdventurePresentationFlags(&adventure));
   snesBackgroundPrepare(frame.camera_x, frame.camera_y);
   snesBackgroundStage(buffer);
   snesSpritesPrepare(&adventure, &frame);
   snesSpritesStage(buffer);
   snesStormStage(&adventure, buffer);
}

/* without a second cpu the presentation waits for gamePresentFinish, after the transfers of the
 * previous one. */
static DeceptusU16 present_buttons;
static DeceptusU8 present_ticks, present_buffer;

static void gamePresentStart(DeceptusU16 buttons, DeceptusU8 ticks, DeceptusU8 buffer)
{
   present_buttons = buttons;
   present_ticks = ticks;
   present_buffer = buffer;
}

static DeceptusU8 gamePresentFinish(void)
{
   while (present_ticks)
   {
      --present_ticks;
      if (deceptusAdventureTick(&adventure, present_buttons))
      {
         deceptusRender(&adventure.campaign.engine, &frame);
         return 1;
      }
   }
   deceptusRender(&adventure.campaign.engine, &frame);
   deceptus_debug[11] = 10;
   gamePrepare(present_buffer);
   return 0;
}
#endif

static void videoLoad(void)
{
   deceptus_debug[11] = 3;
   setScreenOff();
   screen_blanked = 1;
   snesBackgroundLoadVideo(SNES_GAME->campaign.room_index);
   gameLoadRoom();
   deceptus_debug[11] = 4;
   snesSpritesInitVideo();
   deceptus_debug[11] = 5;
   snesUiInit();
   deceptus_debug[11] = 6;
}

/* the vblank the previous ticks started in. */
static DeceptusU16 previous_vblank;

/* the next ticks have their input while the commit runs: below phase 15, which tells the emulator
 * checks that the packet is complete and the pad not read yet, and above 9, during which they
 * compare video memory with the work area. */
#define SNES_PHASE_COMMITTING 11

static void presentStart(DeceptusU8 ticks, DeceptusU8 buffer)
{
   gamePresentStart(inputRead(), ticks, buffer);
   deceptus_debug[11] = SNES_PHASE_COMMITTING;
}

/* waits until the coming vblank is SNES_FRAMES_PER_PICTURE frames after the previous start. */
static void holdPicture(void)
{
   while ((DeceptusU16)(snes_vblank_count + 1 - previous_vblank) < SNES_FRAMES_PER_PICTURE)
   {
      snesWaitForVblank();
   }
}

/* the ticks for the time from the previous start to the coming vblank, which starts the next ones.
 * counted before the wait, which leaves vblank to the transfers; should the vblank arrive before
 * the wait, the following count includes the missed frame. a room load is not made up for. */
static DeceptusU8 ticksUntilNextVblank(DeceptusU8 room_loaded)
{
   DeceptusU16 next_vblank;
   DeceptusU8 ticks;
   next_vblank = snes_vblank_count + 1;
   ticks = deceptusPacingTicks(&pacing, room_loaded ? 1 : next_vblank - previous_vblank);
   previous_vblank = next_vblank;
   return ticks;
}

int main(void)
{
   DeceptusU8 room_loaded, ticks, buffer, committed, serial;
   snesClearGameRam();
#ifdef SNES_SA1
   snesSa1MapDefaultBlocks();
#endif
   deceptus_debug[0] = 0xD3CE;
   deceptus_debug[11] = 1;
   gameStart();
   deceptus_debug[11] = 2;
   videoLoad();
   audioStart();
   deceptus_debug[11] = 7;
   deceptusPacingInit(&pacing, snes_50hz ? 50 : 60, SNES_PACING_MAX_TICKS);
   previous_vblank = snes_vblank_count;
   buffer = 0;
   presentStart(1, buffer);
   while (1)
   {
      room_loaded = 0;
      deceptus_debug[11] = 9;
      if (gamePresentFinish())
      {
         videoLoad();
         deceptusPacingReset(&pacing);
         room_loaded = 1;
         deceptus_debug[11] = 10;
         gamePrepare(buffer);
      }
      deceptus_debug[11] = 12;
      followMusic();
      audioPlayCues();
      snesUiPrepare(SNES_GAME);
      deceptus_debug[11] = 13;
      snesSpritesUploadTiles(buffer);
      deceptus_debug[11] = 14;
      debugWrite(buffer);
      holdPicture();
      ticks = ticksUntilNextVblank(room_loaded);
      /* phase 15 tells emulator checks the packet is complete and the pad not read yet. */
      deceptus_debug[11] = 15;
      if (snesSpritesBlank(buffer))
      {
         screen_blanked = 1;
      }
      if (snesBackgroundFull(buffer))
      {
         setScreenOff();
         screen_blanked = 1;
      }
      snesWaitForVblank();
      committed = buffer;
      buffer ^= 1;
      /* the pad as this vblank read it. the SA-1 runs the next ticks and prepares their frame into
       * the other buffer while this cpu commits this one; a commit that still reads the work area
       * finishes first. */
      serial = snesBackgroundSerial(committed);
      if (serial == 0)
      {
         presentStart(ticks, buffer);
      }
      /* every video transfer first, while vblank lasts. when the background leaves too little of it,
         the sprites take the next vblank; their tiles and the oam that refers to them stay together. */
      snesBackgroundCommit(committed);
      if (!screen_blanked && snesVblankLinesLeft() < snesSpritesCommitLines(committed) + snesStormCommitLines(committed))
      {
         snesWaitForVblank();
      }
      snesSpritesCommitTransfers(committed);
      snesStormCommit(committed);
      snesUiCommit();
      if (screen_blanked)
      {
         snesScreenOn();
         screen_blanked = 0;
      }
      if (serial)
      {
         presentStart(ticks, buffer);
      }
   }
   return 0;
}
#endif
