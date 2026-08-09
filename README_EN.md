# Simple Keyboard Abstraction Library

A lightweight embedded key abstraction layer providing a flat key model, software debounce, tick-based hold timing with long-press / auto-repeat detection, and per-key function-pointer handler callbacks. Supports any number of irregular key layouts. Current version **v0.4.1**.

## Features

- **Flat key model**: a keyboard is N independent keys, each reading its raw level via a `read_raw` callback; fully decoupled from row/column matrices, naturally supporting irregular layouts.
- **Standalone debounce component**: reuses the door-lock project's `debounce` component; 1 byte of state per key, no hardware dependency.
- **Tick-driven timing**: `skb_tick()` accumulates press duration driven by a time-base tick (e.g. scheduler EVT_TICK); `skb_poll()` samples on a scan period (e.g. 10ms). Time is injected by the system, not passed by the caller; duration is measured in ticks (tick-to-time conversion is application-defined).
- **Per-key function-pointer handler groups**: each key carries its own `skb_handlers_t` (`on_press` / `on_click` / `on_long_start` / `on_long_hold` / `on_long_up`), assigning different handling logic per key; a NULL field ignores that event; each key has an independent `param`.
- **Compile-time feature macros**: `SKB_CFG_ENABLE_PRESSED` / `SKB_CFG_ENABLE_CLICK` / `SKB_CFG_ENABLE_LONG` / `SKB_CFG_ENABLE_REPEAT` selectively enable features; disabled features compile out their handler fields, detection branches and state, saving Flash, RAM and per-tick CPU.
- **Per-key configuration**: each key has its own `long_ticks` (long-press threshold) and `repeat_ticks` (auto-repeat period), or falls back to global defaults; purely config-driven.
- **Memory optimized**: runtime state does not embed the config, only a `const cfg *` pointer; a 32-key static pool is ~768B on x64.
- **Zero dynamic memory**: static key pool (`SKB_MAX_KEYS` cap + runtime N), no malloc.
- **Third-party components**: debounce, memguard/GMP (static memory pool), EventScheduling (event scheduler), ringBuffer (scheduler queue), c-linked-list (task list).
- **Debug output switchable**: `DBG_macro.h` can be disabled entirely via `DBG_ENABLE=0`, avoiding `snprintf`.

## Important Notice

This project targets bare-metal environments only; thread safety is not implemented. When used in RTOS or multi-threaded environments, add your own mutexes or critical sections.

## Quick Start

```c
#include "simple_keyboard.h"

/* 1. One raw-input read callback per key (1 = pressed) */
static bool read_doorbell(void *ctx) { return GPIO_ReadPin(DOORBELL_PIN); }
static bool read_reset(void *ctx)    { return GPIO_ReadPin(RESET_PIN); }

/* 2. Define handler callbacks (function pointers assign different logic) */
static void on_click(uint16_t key_id, uint32_t ticks, void *param) {
    handle_click(key_id);   /* param is per-key; ticks = hold duration in ticks */
}
static void on_long_start(uint16_t key_id, uint32_t ticks, void *param) {
    handle_long(key_id);
}

static const skb_handlers_t doorbell_h = { .on_click = on_click };
static const skb_handlers_t reset_h = {
    .on_click      = on_click,
    .on_long_start = on_long_start,
};

/* 3. Define key configs (static/const; lifetime must cover keyboard usage)
 *    Time unit = tick (number of skb_tick calls); tick->time mapping is app-defined */
static const skb_key_cfg_t keys[] = {
    { .key_id = 1, .read_raw = read_doorbell, .handlers = &doorbell_h, .param = "doorbell" },
    { .key_id = 2, .read_raw = read_reset,    .long_ticks = 5000, .handlers = &reset_h },
};

static skb_t kb;

/* 4. Driven by a scheduler task: time every tick, scan every 10 ticks */
void kb_task_on_tick(void) {
    static unsigned div = 0;
    skb_tick(&kb);                 /* per-tick timing (press_ticks++ -> LONG/REPEAT) */
    if (++div >= 10) { div = 0; skb_poll(&kb); }  /* 10-tick scan (debounce/edges) */
}

int main(void) {
    skb_init(&kb, keys, 2, 500, 0); /* global long 500 ticks, no repeat */
    /* ... register kb_task_on_tick to a time base (e.g. scheduler EVT_TICK) ... */
}
```

### Auto-Repeat Example

```c
/* Config key: after long-press 1000 ticks, fire on_long_hold every 300 ticks while held */
static void on_long_hold(uint16_t key_id, uint32_t repeat_idx, void *param) {
    do_increment(key_id, repeat_idx);
}
static const skb_handlers_t cfg_h = {
    .on_click      = on_click,
    .on_long_start = on_long_start,
    .on_long_hold  = on_long_hold,
};
static const skb_key_cfg_t cfg_key = {
    .key_id = 3, .read_raw = read_config,
    .long_ticks = 1000, .repeat_ticks = 300,
    .handlers = &cfg_h,
};
```

## Handler Callbacks

| Callback | Trigger condition | info |
|------|----------|-----------|
| `on_press` | debounced press edge | 0 |
| `on_click` | released before crossing long_ticks | hold ticks |
| `on_long_start` | press ticks cross long_ticks | hold ticks |
| `on_long_hold` | every repeat_ticks after long press | repeat_idx |
| `on_long_up` | released after long press | hold ticks |

All durations are in ticks (number of `skb_tick` calls); the tick-to-time mapping (e.g. 1 tick = 1ms) is application-defined — the library makes no unit assumption. A key's `handlers` may be NULL (ignore all its events); a NULL field ignores that event. Classification is done by the module using the key's `long_ticks` / `repeat_ticks`; users do not classify manually.

## Feature Macros (compile-time trimming)

| Macro | Default | When OFF |
|------|------|------|
| `SKB_CFG_ENABLE_PRESSED` | ON | press-edge detection/dispatch and the `on_press` field are compiled out |
| `SKB_CFG_ENABLE_CLICK` | ON | click detection/dispatch compiled out |
| `SKB_CFG_ENABLE_LONG` | ON | long-press (START/UP) and the `long_fired` state compiled out |
| `SKB_CFG_ENABLE_REPEAT` | ON | auto-repeat compiled out (requires LONG) |

CMake exposes `option(SKB_CFG_ENABLE_*)` (e.g. `-DSKB_CFG_ENABLE_REPEAT=OFF`); the macros default to 1 and can be overridden with `-D`.

## Debounce Tuning

Key debounce is handled by the `thirdparty/debounce` component, which confirms an edge only after N consecutive stable samples. The debounce window is approximately:

```
debounce window ≈ poll period × DEBOUNCE_SAMPLES
```

- `DEBOUNCE_SAMPLES`: sample count, defined in `thirdparty/debounce/debounce.h` (`#ifndef`-guarded, max 127, default 3).
- Poll period: the interval between `skb_poll` calls, application-defined (e.g. every 10ms).

Example: 10ms poll × 3 samples = 30ms window. If polling drops to 20ms and you still want ~40ms, lower the sample count to 2:

```bash
cmake -B build -G Ninja -DCMAKE_C_FLAGS="-DDEBOUNCE_SAMPLES=2"
```

`DEBOUNCE_SAMPLES` is a global build-time setting shared by all keys. Lowering the sample count keeps the debounce window reasonable under slow polling, but filters mechanical bounce less aggressively — balance responsiveness against bounce rejection.

## Build

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

Or run `build.bat` on Windows. The project uses the C99 standard (`CMAKE_C_STANDARD 99` / `-std=gnu99`).

### Debug Switches

| Switch | Default | Description |
|------|------|------|
| `SKB_DEBUG` (CMake) / `-DSKB_DEBUG` | OFF | Enable simple_keyboard internal debug prints |
| `SKB_DEMO_PRINTS=OFF` (CMake) / `-DDBG_ENABLE=0` | ON | Disable all `DBG_macro.h` print macros: compile to no-ops, no `<stdio.h>` / `snprintf` |
| `SKB_ARG_CHECK=OFF` (CMake) / `-DSKB_ARG_CHECK_ENABLE=0` | ON | Disable API argument validation to reduce code/runtime overhead; invalid inputs then cause undefined behavior |

## API Reference

| Function | Description |
|------|------|
| `skb_init(kb, cfg, n, global_long_ticks, global_repeat_ticks)` | Initialize and register N keys with global trigger config |
| `skb_poll(kb)` | Key scan: read raw → debounce → edges → call on_press / on_click / on_long_up |
| `skb_tick(kb)` | Key timing: press_ticks++ for held keys → call on_long_start / on_long_hold (per-tick time base) |
| `skb_hold_ticks(kb, key_id)` | Query a key's current hold duration in ticks |
| `skb_is_pressed(kb, key_id)` | Query whether a key is currently pressed |

Return values: most functions return `SKB_OK` on success, otherwise an error code (`SKB_ERR_ARG`, `SKB_ERR_NOT_READY`, `SKB_ERR_TOO_MANY`, `SKB_ERR_NOT_FOUND`).

See [simple_keyboard.h](include/simple_keyboard.h) for the complete API.

## Demo: Faithful Runtime Simulation

`main.c` faithfully recreates the door-lock project's runtime model, running in real time (~10s):

| Role | Implementation | Description |
|------|------|------|
| Simulated SysTick | clock thread `Sleep(1); g_tick_ms++` | The only system time source (`timeBeginPeriod(1)` for 1ms precision) |
| Scheduler runtime | scheduler thread calls `EventSchedul_MainLoop(ctx)` | pull → idle hook → dispatch |
| idle hook | `evtSchedul_idle` registered as sleepMethod | broadcasts `EVT_TICK` when `g_tick_ms` advances |
| Keyboard task | `kb_task` receives EVT_TICK | `skb_tick` (1ms) + 10-tick decimated `skb_poll` (10ms) + scenario step |
| Business task | `biz_task` receives key events | each key's handlers post via `setEventToTask`; the scheduler dispatches uniformly |

In the demo output, `[kb]` lines come from per-key handlers, `[sched]` lines from the business task dispatched by the scheduler. Full pipeline: key → debounce/timing → handler → scheduler queue → task.

## Third-Party Components

| Component | Directory | Purpose |
|------|------|------|
| debounce | `thirdparty/debounce` | Key software debounce (1 byte/key, pure data, zero dependency) |
| memguard (GMP) | `thirdparty/memguard` | Static memory pool; scheduler context allocator (no malloc) |
| EventScheduling | `thirdparty/EventScheduling` | Event scheduler: task registration, event posting, MainLoop / TmosPoll |
| ringBuffer | `thirdparty/ringBuffer` | Scheduler's internal event queue (variable-size items); [FIXED] `_calc_count` wrap-around counting bug |
| c-linked-list | `thirdparty/c-linked-list` | Task linked-list management (header-only library) |

## Notes

1. Must call `skb_init` before use; `read_raw` must not be NULL.
2. The `cfg` array must be `static/const` and its lifetime must cover keyboard usage (runtime state only holds a pointer, no copy).
3. `skb_tick` is driven by the time-base tick (precision equals the time base); `skb_poll` runs on the scan period, setting the debounce sampling rate.
4. Each key's `key_id` must be unique; `read_raw` returns true = pressed; `handlers` may be NULL.
5. `repeat_ticks` only takes effect after `long_ticks` is crossed (on_long_start fired).
6. Beware of reentrancy in ISRs; add your own synchronization for multi-threaded use.

## License

This project is for learning purposes and may be freely used and modified.
