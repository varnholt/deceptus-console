#include "deceptus/engine.h"
#include <assert.h>
#include <string.h>

static deceptus_u8 cells[80 * 64];
static deceptus_engine engine, baseline;
static deceptus_room room;
static deceptus_mechanism_def rope = {109, {320, 48, 7, 234}, 0, 0, 22, DECEPTUS_MECH_GRAB_ROPE, 1, 0};
static const deceptus_video_caps video = {240, 160, DECEPTUS_DRAW_CAPACITY};

static void setup(void)
{
    deceptus_u16 x;
    memset(&room, 0, sizeof(room));
    memset(cells, 0, sizeof(cells));
    for (x = 0; x < 80; ++x) {
        cells[50 * 80 + x] = DECEPTUS_SOLID;
    }
    room.collision = cells;
    room.columns = 80;
    room.rows = 64;
    room.spawn_x = 320;
    room.spawn_y = 288;
    room.patrol_x = -1;
    room.door_x = -1;
    room.max_health = 100;
    room.mechanisms = &rope;
    room.mechanism_count = 1;
    deceptus_init(&engine, &room, &video);
}

int main(void)
{
    deceptus_u16 tick;
    deceptus_i16 x, y;
    deceptus_fixed still_x, still_y;
    deceptus_mechanism_def definitions[2];
    deceptus_frame frame;
    setup();
    assert(!engine.content_error && engine.grab_ropes.count == 1);
    assert(engine.mechanisms.states[0].visible);
    assert(engine.grab_ropes.ropes[0].node_count == 22);
    assert(!engine.grab_ropes.ropes[0].dynamic_solids);
    assert(deceptus_grabrope_point_at(&engine, 0, 0, &x, &y) && x == 320 && y == 48);
    assert(deceptus_grabrope_point_at(&engine, 0, 1000, &x, &y) && y == 271);
    assert(deceptus_grabrope_length(&engine, 0) == 224);
    deceptus_tick(&engine, 0);
    assert(engine.player_rope.attached && engine.player_rope.node_index > 0);
    deceptus_render(&engine, &frame);
    assert(frame.count && frame.draws[0].kind == DECEPTUS_PLAYER_ROPE);
    baseline = engine;
    for (tick = 0; tick < 30; ++tick) {
        deceptus_tick(&engine, 0);
    }
    still_x = engine.entities[0].x;
    still_y = engine.entities[0].y;
    engine = baseline;
    for (tick = 0; tick < 30; ++tick) {
        deceptus_tick(&engine, DECEPTUS_UP | DECEPTUS_DOWN);
    }
    assert(engine.entities[0].x == still_x && engine.entities[0].y == still_y);
    engine = baseline;
    for (tick = 0; tick < 45; ++tick) {
        deceptus_tick(&engine, DECEPTUS_RIGHT);
    }
    assert(engine.player_rope.attached && engine.entities[0].x > still_x + DECEPTUS_FIXED(12));
    assert(engine.health == 100 && !engine.content_error);
    deceptus_tick(&engine, DECEPTUS_JUMP | DECEPTUS_RIGHT);
    assert(!engine.player_rope.attached && engine.player_rope.regrab_ticks == 60);
    assert(engine.player_rope.grace_ticks == 36);
    deceptus_place_player(&engine, 320, 288);
    assert(!engine.player_rope.attached && !engine.player_rope.regrab_ticks);
    engine.entities[0].grounded = 1;
    deceptus_tick(&engine, 0);
    assert(!engine.player_rope.attached);
    definitions[0] = rope;
    memset(&definitions[1], 0, sizeof(definitions[1]));
    definitions[1].kind = DECEPTUS_MECH_DOOR;
    definitions[1].bounds.x = 800;
    definitions[1].bounds.y = 100;
    definitions[1].bounds.width = 24;
    definitions[1].bounds.height = 72;
    engine.mechanisms.definitions = definitions;
    engine.mechanisms.count = 2;
    assert(deceptus_grabropes_init(&engine));
    assert(!engine.grab_ropes.ropes[0].dynamic_solids);
    definitions[1].bounds.x = 500;
    assert(deceptus_grabropes_init(&engine));
    assert(engine.grab_ropes.ropes[0].dynamic_solids);
    definitions[1].kind = 255;
    definitions[1].bounds.x = 800;
    assert(deceptus_grabropes_init(&engine));
    assert(engine.grab_ropes.ropes[0].dynamic_solids);
    rope.value = 25;
    setup();
    assert(engine.content_error);
    rope.value = 22;
    return 0;
}
