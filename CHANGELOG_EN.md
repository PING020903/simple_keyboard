# Changelog

All notable changes to this project will be documented in this file.

---

## [v0.4.1] - 2026-08-10

### Changed

- **Duration unit changed to ticks**: the library's time unit is the number of `skb_tick` calls, with no ms/s assumption; naming consistently drops the `_ms` suffix
  - `skb_key_cfg_t.long_ms/repeat_ms` → `long_ticks/repeat_ticks`
  - `skb_t.global_long_ms/global_repeat_ms` → `global_long_ticks/global_repeat_ticks`; `skb_init` params updated
  - internal `press_ms/next_repeat_ms` → `press_ticks/next_repeat_ticks`
  - `skb_hold_ms(kb, key_id)` → `skb_hold_ticks(kb, key_id)`
  - handler `info` semantics: `on_click`/`on_long_start`/`on_long_up` carry hold ticks (`press_ticks`, number of `skb_tick` calls), `on_long_hold` carries `repeat_idx`, `on_press` carries 0
- The tick→time mapping (e.g. 1 tick = 1ms) is defined by the application; the library makes no unit assumption

---

## [v0.4.0] - 2026-08-10

### Added

- **Per-key function-pointer handler groups**: each key config carries a `skb_handlers_t` (on_press/on_click/on_long_start/on_long_hold/on_long_up), assigning different handling logic per key; NULL = ignore that event; per-key independent `param`
- **Compile-time feature macros**: `SKB_CFG_ENABLE_PRESSED/CLICK/LONG/REPEAT` selectively enable features; disabled features compile out their handler fields, detection branches and state (saving Flash/RAM/per-tick CPU); exposed as CMake `option()` (e.g. `-DSKB_CFG_ENABLE_REPEAT=OFF`)
- **Memory optimization**: runtime state no longer embeds the config, only a `const cfg*` pointer (32-key pool on x64 2KB→768B, ~60% saving); thresholds changed to `uint16_t` (up to 65.5s); `param` and `ctx` kept separate (per-key independent)
- **C99 standard**: `set(CMAKE_C_STANDARD 99)` + `-std=gnu99` (consistent with .clangd and the door-lock project)
- **ringBuffer fix**: `_calc_count` wrap branch `wr+depth-rd` → `wr+2*depth-rd` (the bug treated a full queue as 0 when wrapped, causing scheduler event loss and duplicate pops; fixed and marked in this project's vendored copy; upstream `F:\cmake_study\ringBuffer` has the same bug and should be synced)

### Changed

- Dispatch model: dropped v0.3.0's enum classification (single callback + event-enum switch) in favor of **function-pointer handler groups** — no classification or switch needed by the user
- `skb_init(kb, cfg, n, global_long_ms, global_repeat_ms)`: removed cb/cb_param (now per-key handlers + param)
- `cfg` is now read-only (`const skb_key_cfg_t *`); removed `skb_set_longpress/set_repeat` (purely config-driven)
- Demo `main.c`: each key mounts a different handler combination; handlers encode and post to the scheduler's `biz_task`

---

## [v0.2.1] - 2026-08-10

### Added

- **Tick-driven timing**: `skb_poll(kb)` split into scan (read raw → debounce → edges) + `skb_tick(kb)` 1ms timing (press_ms++ → LONG/REPEAT), removing the hand-passed `dt` parameter entirely; `press_ms` comes from the module's internal tick counting, with time injected by the system (scheduler EVT_TICK)
- **Faithful runtime simulation demo**: `main.c` recreates the door-lock runtime — simulated SysTick clock thread (real 1ms) + scheduler thread running `EventSchedul_MainLoop` + idle hook broadcasting EVT_TICK + keyboard task (1ms timing / 10ms scan) + business task dispatched by the scheduler; scenarios scripted in real time (~10s)
- **Scheduler per-ms debug print suppression**: `EventSchedul.c` compiled with `DBG_ENABLE=0` alone (no more EVT_TICK enqueue flooding), without touching vendored sources
- **winmm link**: `timeBeginPeriod(1)` raises `Sleep` precision to 1ms

### Changed

- `skb_poll` signature: `skb_poll(kb, dt_ms)` → `skb_poll(kb)` + `skb_tick(kb)`
- Demo output adds `[sched]` business-task dispatch lines, showing the full "key → scheduler → task" pipeline
- Test scenarios: added T6 full-pipeline (3 keys dispatched in order by the scheduler), replacing the old "scheduler buffering" (with a real running MainLoop, events are dispatched immediately)

---

## [v0.2.0] - 2026-08-10

### Added

- **Flat key model**: dropped the matrix concept (rows/cols, raw bitmap, fsm[row][col], key_map); a keyboard is now N independent keys, each reading raw level via its own `read_raw` callback, supporting any number of irregular key layouts
- **Time-parameter dispatch callback**: the module only handles debounce + hold timing, handing `(key_id, moment, press_ms)` to the user callback (`SKB_MOMENT_PRESS/LONG_CROSS/REPEAT/RELEASE`); short/long/super-long classification is entirely user-defined by duration
- **Per-key configurable thresholds**: each key has its own `long_ms`/`repeat_ms`, supports long-press auto-repeat (with catch-up), runtime overridable by key_id
- **debounce standalone component**: added `thirdparty/debounce` (from the door-lock project; 1 byte per key, pure data, zero dependency)
- **memguard (GMP)**: added `thirdparty/memguard` static memory pool; scheduler context allocator replaces libc malloc
- **Test scenarios**: short press, long press (LONG_CROSS), auto-repeat (multiple REPEAT), per-key thresholds, debounce glitch rejection, scheduler event buffering

### Changed

- `skb_poll(kb, dt_ms)` replaces the old `skb_scan`/`skb_process`/`skb_get_event` (events now dispatched directly via callback)
- `skb_init` signature changed to: key config array + global long/repeat thresholds + dispatch callback
- v0.1.0 matrix-keyboard API (`skb_scan`/`skb_set_key_map`, etc.) removed

---

## [v0.1.0] - 2026-08-10

### Features

- Matrix scanning: drive rows one at a time, read columns; `raw` bitmap records scan results
- Software debounce: press/release confirmed only after N consecutive consistent scans, filtering mechanical bounce
- Key state machine: idle → press-confirm → pressed → hold → release-confirm → idle
- Long-press detection: `skb_set_hold_time` configures the hold threshold, triggering a `HOLD` event (disabled by default)
- Key mapping: `skb_set_key_map` sets the mapping table; unmapped positions fall back to `row*cols+col`
- FIFO event queue: `skb_get_event` drains press/hold/release events
- GPIO abstraction callbacks: `set_row` / `get_col` as a single porting point
- Zero dynamic memory: all state is statically allocated
- Debug macro support (`DBG_macro.h`, disable-able via `DBG_ENABLE`)

### Build

- CMake project (`include` / `sources` layout) with options: `SKB_DEBUG`, `SKB_DEMO_PRINTS`, `SKB_ARG_CHECK`
- Windows one-click build script `build.bat`
- `main.c` demo: simulates a 4x4 matrix keyboard covering press/release, long press, debounce filtering, key mapping, and multi-key simultaneous scenarios
