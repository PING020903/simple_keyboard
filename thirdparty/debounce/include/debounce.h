#ifndef _DEBOUNCE_H_
#define _DEBOUNCE_H_

#include <stdint.h>

/* Consecutive stable samples required to accept a level change. With a 10 ms
 * sampling period, 3 samples = 30 ms. This is a compile-time constant shared
 * by all debouncers — kept out of the per-instance state to save RAM. */
#ifndef DEBOUNCE_SAMPLES
#define DEBOUNCE_SAMPLES 3
#endif

#if DEBOUNCE_SAMPLES > 127
#error "DEBOUNCE_SAMPLES must be <= 127 (cnt is a 7-bit field)"
#endif

/**
 * Per-input debounce state, packed into a single byte.
 *
 * Feed one raw sample per sampling period via debounce_sample(). The stable
 * level only changes once the raw level has differed from it for
 * DEBOUNCE_SAMPLES consecutive samples, so contact bounce (which keeps
 * flipping the raw level) never accumulates enough stable samples to register.
 */
typedef struct {
    uint8_t cnt : 7;     /* consecutive samples differing from stable */
    uint8_t stable : 1;  /* debounced (accepted) level, 0 or 1 */
} debounce_t;

/**
 * Initialise the debouncer.
 * @param initialLevel  assumed level before any sampling (e.g. 1 = idle/high).
 */
void debounce_init(debounce_t *db, uint8_t initialLevel);

/**
 * Feed one raw sample (any non-zero value is treated as 1).
 * @return 1 if the stable level CHANGED on this sample (a debounced edge
 *         occurred), 0 otherwise. Read the new level with debounce_state().
 */
uint8_t debounce_sample(debounce_t *db, uint8_t rawLevel);

/** Current debounced (stable) level, 0 or 1. */
uint8_t debounce_state(const debounce_t *db);

#endif /* _DEBOUNCE_H_ */
