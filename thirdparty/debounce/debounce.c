#include "debounce.h"

void debounce_init(debounce_t *db, uint8_t initialLevel)
{
    db->stable = initialLevel ? 1 : 0;
    db->cnt = 0;
}

uint8_t debounce_sample(debounce_t *db, uint8_t rawLevel)
{
    rawLevel = rawLevel ? 1 : 0;

    if (rawLevel != db->stable) {
        if (++db->cnt >= DEBOUNCE_SAMPLES) {
            db->stable = rawLevel;
            db->cnt = 0;
            return 1;   /* debounced edge: stable level changed */
        }
    } else {
        db->cnt = 0;    /* raw matches stable: bounce did not settle */
    }
    return 0;
}

uint8_t debounce_state(const debounce_t *db)
{
    return db->stable;
}
