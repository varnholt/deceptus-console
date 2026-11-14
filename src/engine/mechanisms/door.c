/* original counterpart: src/game/mechanisms/door.cpp, animated version 2. */
/* private mechanism implementation; compiled through ../mechanisms.c. */

static void door_configure(deceptus_mechanism_state *state)
{
    state->active = !state->enabled;
    state->visible = state->active;
    state->frame = state->enabled ? 56 : 0;
}

static void door_enable(deceptus_mechanism_state *state, deceptus_u8 enabled)
{
    state->enabled = enabled;
    state->phase = enabled ? 1 : 2;
    state->frame = enabled ? 0 : 57;
    state->fraction = 0;
    state->timer = 0;
    state->cooldown = 60;
    state->visible = 1;
}

static void door_tick(deceptus_mechanism_state *state)
{
    if (state->cooldown) {
        --state->cooldown;
    }
    if (!state->phase) {
        return;
    }
    ++state->timer;
    /* the source has 57 authored frames of 20ms each: 69 ticks at 60hz. */
    if (state->timer >= 69) {
        state->phase = 0;
        state->active = !state->enabled;
        state->visible = state->active;
        state->frame = state->enabled ? 56 : 0;
        return;
    }
    state->fraction += 5;
    if (state->fraction >= 6) {
        state->fraction -= 6;
        ++state->frame;
    }
}
