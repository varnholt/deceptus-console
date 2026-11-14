/* native harness for replaying normal inputs against imported opening content. */
#include "deceptus/adventure.h"

extern const deceptus_adventure_def gba_tutorial_definition;
static deceptus_adventure opening;
static deceptus_u16 rope_ticks;

#ifdef _WIN32
#define OPENING_API __declspec(dllexport)
#else
#define OPENING_API __attribute__((visibility("default")))
#endif

OPENING_API void opening_init(void)
{
    deceptus_video_caps video = {240, 160, DECEPTUS_DRAW_CAPACITY};
    rope_ticks = 0;
    deceptus_adventure_init(&opening, &gba_tutorial_definition, &video);
}

OPENING_API void opening_tick(int buttons)
{
    deceptus_u16 previous_ticks;
    previous_ticks = opening.campaign.engine.ticks;
    deceptus_adventure_tick(&opening, (deceptus_u16)buttons);
    if (opening.campaign.engine.player_rope.attached && opening.campaign.engine.ticks != previous_ticks) {
        ++rope_ticks;
    }
}

/* integer getters keep target-dependent structure layouts out of the verifier. */
OPENING_API int opening_state(int field)
{
    const deceptus_engine *engine;
    engine = &opening.campaign.engine;
    if (field >= 16 && field < 32) {
        return opening.inventory.counts[field - 16];
    }
    switch (field) {
    case 0:
        return opening.campaign.room_index;
    case 1:
        return deceptus_floor(engine->entities[0].x);
    case 2:
        return deceptus_floor(engine->entities[0].y);
    case 3:
        return opening.campaign.mode;
    case 4:
        return opening.flags;
    case 5:
        return engine->health;
    case 6:
        return opening.campaign.deaths;
    case 7:
        return engine->content_error;
    case 8:
        return opening.has_sword;
    case 9:
        return engine->entities[0].grounded;
    case 10:
        return engine->ticks;
    case 11:
        return opening.page;
    case 12:
        return (int)opening.campaign.elapsed_ticks;
    case 13:
        return rope_ticks;
    default:
        return 0;
    }
}
