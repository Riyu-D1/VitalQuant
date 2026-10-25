# VitalQuant fw2 — Virtual-Board Test Findings

Emulation campaign results for the fix_v22 firmware (ESP32-S3-WROOM-1-N16R8),
running the **real firmware source** (`app_main` + all 27 components) on a
host-modeled replica of the v19 PCB + WROOM swap from `hardware/pcb/fab/BOM.csv`.

## What was tested

| Area | Coverage |
|---|---|
| Board bring-up | straps, rails, RTC, gauge, expander, I²C scan (10 nodes), SPI (5 CS) |
| Sensors | 16/16 streaming end-to-end (opt. TMP117.tail correctly absent) |
| Chip models | register/CS-accurate models: TCA6408, MAX17048, RV-3028, TMP117×2, SHT45, BME280, LSM6DSV80X, AS7341, MLX90632, ADS1292R, AFE4900, AD5940 (sequencer/FIFO/hibernate), MAX86141, IM69D130, W25Q512 |
| Model | 1,385,346-param INT8 VQMB blob in the real `model` partition, mmap'd |
| Cloud | real `esp_http_client` shim → real TCP → live Python mock server |
| Faults | I²C NACK storm, SDA stuck, SPI bitflips, heap=0, Wi-Fi down, SHT45 silent |
| Self-tests | all 16 `selftest` entries over the real console REPL |

## Headline results

- Boot: clean — 16/16 sensors streaming, model loaded, decision loop at 180 s cadence
- `scn_selftest` (95 checks, all/solo/fault matrix): **0 scenario failures** —
  `selftest all` = **15/16** (only `ecg_bioz` fails in-suite, passes solo);
  solo column = 15/16; fault column degrades exactly where expected
  (i2c_scan under injected 0x44 silence, ecg_bioz with expander gated).
- `scn_model` (1.4 M-param): **PASS** — params=1,385,346, weights=1.39 MB mmap'd,
  arena=4,930 B, DENSE×5 = 564,480 cycles, inference ≈ **2.35 ms**,
  softmax out [0.992/0.008]
- `scn_busfault`: **46 checks, 0 real fails** — 9 injected-fault families all
  contained (NACK storms, wedged SDA, SPI bitflips/CS-fights, rail loss,
  heap=0, Wi-Fi loss) with full recovery; the one reported BUG-FOUND marker
  is intentional and now moot — the emit-path defect it documented is fixed
- `scn_storage` (PH1–PH9): **100 checks, 0 fails** — journal mount/corrupt-
  sector recovery/WP-engage/iterate-skip/ring-wrap all verified on the
  byte-accurate W25Q512 model
- `scn_cloud`: **functional pass** — heartbeat + escalation POSTs arrive at
  the live mock with `Authorization: Bearer devkey-TEST-001`, full JSON
  feature vectors and the 4072 B `vqeb/1` window bundle (`queued=1
  persisted=1`); `stats: ok=2`. POST timing under LED bursts is
  interlock-bound (see design section).
- `scn_faults`: **8/9 checks pass** — console + pipeline survive NACK storms,
  wedged SDA, SPI bitflips, heap-fail and Wi-Fi loss; recovers cleanly
- `scn_soak` (24 h fast-forward, 5 cold reboots, I²C kill, heap marks):
  **120 checks** — heap flat (internal 524288 B, SPIRAM plateau ~4.46 MB,
  no leak), ring wrapped cleanly (id 4334→21474), all reboots mounted;
  pre-fix run surfaced the emit/persist/NaN bugs now fixed above

## REAL firmware bugs found (would misbehave on silicon)

| # | Bug | Fix applied |
|---|---|---|
| 1 | `vq_spi_add` allowed **one handle per device id** — MAX86141 could probe @1 MHz but never open its 8 MHz run handle (init fail → permanent FAULT); ADS1292 silently degraded to 1 MHz (4× throughput loss) | Handle pool `VQ_SPI_DEV_COUNT*2` in `spi_bus.c` — verified on xtensa build too |
| 2 | `VQ_LOG_BATCH_CAP=512` × 24 B = 12 KiB > `VQ_LOG_MAX_PAYLOAD` (4,072) — **every** sample batch append rejected with INVALID_SIZE; offline flash buffer stored **nothing** | Cap now derived from `VQ_LOG_MAX_PAYLOAD` (169/record); ring now persists (next_id advancing) |
| 3 | `st_sleep_current` (non-commit) left the log store sleeping → every subsequent append INVALID_STATE for the rest of the session | `vq_log_wake()` after non-commit run in `st_sys.c` |
| 4 | `power_paths` read `vq_led_tx5_on()` synchronously — the P3 write is deferred to the scheduler timer (racy even on hardware) | 10 ms waits around acquire/release |
| 5 | `power_paths` required TX5 off after release — impossible while streaming sensors hold LED grants (shared rail by design) | `off` demoted to informational; toggle proven by `on`+`release` |
| 6 | `st_rd_ad5940` expected READREG data at frame offset 1 — the driver's convention (and the real chip's turnaround) puts a dummy byte at offset 1; also couldn't tolerate a hibernating chip | Selftest reads offset 2 like `reg_rd`, plus wake-retry after sleep-prep |
| 7 | `ecg_bioz` RCAL times out after `sleep_current` (chip hibernates, first traffic wakes it) and ADS1292R half fails `busy (streaming)` on a live pipeline — the test needs either parked or live state, never both | RCAL warm-up retry added; ads1292r-vs-pipeline exclusivity documented as open |
| 8 | `optical` FAIL message blamed "delta below floor" even when delta was 4–8× the floor — real cause was `st_slot_lost` (LED grant lost mid-window); wrong diagnosis would send bench debugging astray | Failure line now distinguishes slot-loss vs floor in `vq_drv_afe4900.c` |
| 9 | `chip_wake` only handled **hibernate**, not a chip held under expander reset (`expander_walk` parks P2 low as power-off state; `asleep` flag itself documents "hibernate OR held in reset"). Poll failed → TRIGSEQ writes to a dead die → RCAL e=263 | On poll failure, `chip_wake` now runs `chip_init()` (reset pulse + full reprogram) and re-polls — verified live in-suite (`wake poll failed — re-init`, recovery engaged) |
| 10 | **Emit/counters written to driver-private `vq_sensor_t` copies** — `vq_sensor_register` copies by value; the pipeline sets `sink`/`sink_ctx` on the registry copy, so drivers emitting through their templates dropped every sample and error. 10 of 17 sensors affected: env (sht45/bme280/tmp117×2), ad5940.eda/.sweat, fsr, im69d130, afe4900, max17048 | Each driver now caches `vq_sensor_find(name)` post-register (per-poll lookup for the pre-pipeline gauge task) — verified: `sensors` shows real counts for all of them |
| 11 | **Escalation bundles > 4 KiB silently dropped** — `build_bundle` packed up to ~16.7 KB; `vq_log_append` rejects >`VQ_LOG_MAX_PAYLOAD` (4,072) → durable escalation lost (`persisted 0`). This — not sample batching — was the surviving source of `log append failed` | `build_bundle` caps `blob_cap`/`cap` at `VQ_LOG_MAX_PAYLOAD`; pack loop truncates channel windows instead of losing the bundle |
| 12 | **Motion gate counted gravity** — `VQ_F_MOT_RMS` = `sqrt(E[a²])` ≈ 1 g at rest vs `VQ_DEC_MOT_RMS_MAX` 0.50 → a motionless device always read "moving" and motion-gated cycles never scored | features.c subtracts `\|μ_window\|²` per bin (E[a²] = Var + μ²) → mot_rms now measures dynamic acceleration only |
| 13 | `ads1292` counted `sample_count += 2` even when `sink == NULL` (frames never delivered) | count moved inside the `if (s->sink)` block |

## Datasheet audit — every part vs the real datasheet

The sim models were written to satisfy the drivers, so wrong assumptions
self-confirm (circular verification). Six parallel audits checked every
driver against the published datasheets (TI SBAS502C, ADI Rev.G, ST
DS14764, Sensirion v7.3, Bosch DS002, Melexis, Infineon, AMS DS000504,
Winbond RevB, Micro Crystal Rev 1.4, Maxim 19-4209, TI SCPS192E).

**Bugs found only by datasheet check** (the sim could never catch these):

| # | Part | Bug | Fix |
|---|------|-----|-----|
| 14 | **MAX86141** | FIFO TAG[4:0] decoded as the LEDC code — it is the **exposure-slot index** (1–6 = PPG1 slots). Every ambient word hit `tag_err++` → `mx_healthy()` trips in ~0.3 s → sensor flagged unhealthy while streaming; `fifo_drain`/`led_pulse` selftests could never pass | `mx_slot_ledc()` maps slot→programmed LEDC code (1→LED1, 2→LED2, 3→LED3, 4→AMBIENT); **sim model also fixed** to tag by slot |
| 15 | **AD5940** | **INTCPOL semantics inverted** — datasheet Table 137: 0 = asserts LOW (negative-edge output), 1 = asserts HIGH. Driver used `pol=1 + NEGEDGE` → on silicon every `run_seq` would miss the IRQ and time out at 80 ms. Loopback auto-detect also false-passed instantly at dt≈0 (checked asserted level before verifying idle) | `int_pol=0 + NEGEDGE`; loopback tries pol=0 first, verifies idle level before triggering, checks real assert + deassert; **sim model polarity corrected to match** |
| 16 | **AD5940** | `clk_cfg` wrote `HPOSCCON.CLK32MHZEN` (16→32 MHz rate switch) **before** `CLKCON0` dividers — the documented "SYSCLKDIV write not persisting" failure (UG-1262/EZ): sysclk stuck at 32 MHz halves all real-time sequence waits and doubles ADC rate | Reordered: `CLKCON0` → dummy `ADIID` read → `HPOSCCON` → `CLKSEL` |
| 17 | **LSM6DSV80X** | Wake threshold **16× too sensitive** — `WK_THS=2` assumed the legacy `FS_XL/64` weight (250 mg); the 80X weights it by `WU_INACT_THS_W` (INACTIVITY_DUR 0x54, default 000 = 7.8125 mg/LSB) → actual threshold 15.6 mg | `WK_THS=0x20` (32 × 7.8125 = 250 mg), comment corrected |
| 18 | **LSM6DSV80X** | CTRL6 (0x15) written 0x02 — clears bit3 which the datasheet marks **"must be set to 1"** (factory trim) | Writes 0x0A |
| 19 | **LSM6DSV80X** | WHO_AM_I mask `(id&0xF0)==0x70` also accepts LSM6DSV16X (0x70), which has **different FS_XL encoding** — a mis-populated part would pass probe then misconfigure | Exact match `id==0x73` (LSM6DSV320X shares 0x73, register-identical) |
| 20 | **BME280** | `dig_H4`/`dig_H5` parsed as unsigned 12-bit; datasheet defines them signed (Bosch reference sign-extends) → negative-cal units would compute `value+4096` RH | `(int8_t)high*16 | nibble` sign-extension |
| 21 | **AS7341** | `AS7341_EN_WEN = 0x04` — WEN is **bit3 = 0x08** (0x04 hits a reserved bit). Dead code today, latent bug tomorrow | Fixed to 0x08 in driver + sim model |
| 22 | **IM69D130** | PDM clock 2.048 MHz sits in a **datasheet mode-band gap** (bands: .4–.95/1.05–1.9/2.1–2.65/2.9–3.3 MHz); interface accepts 0.4–3.3 so it streams, but acoustic specs unguaranteed | Comments corrected + flagged for bench; guaranteed-spec path = 24 kHz×128→3.072 MHz if needed |

**Confirmed clean against datasheet** (zero mismatches):
- **W25Q512JV**: JEDEC EF-40-20, all opcodes (0x13/0x12/0x21/0x06/0x05/0x15/0xB7/0xB9/0xAB), 4-byte addressing strategy (4B opcodes + EN4B latch), mode 0 @40 MHz (< 50 MHz fR spec), status-bit polling, stacked-die EF-70-20 rejection
- **ADS1292R**: all 11 opcodes, SPI mode 1, ID@0x00≈0x73, RREG count-1, dual-clock strategy (1 MHz RREG obeys tSDECODE; 4 MHz RDATAC legal), status-word layout, reg map + must-be-1 bits
- **AD5940** register map: all ~25 spot-checked addresses correct (AFECON, SEQxINFO, CMDFIFOWADDR, INTC*, DFT*, LPTIA/HS*, clock regs), SPICMD bytes 0x20/0x2d/0x6d/0x5f, ADIID=0x4144, REG_WIDE boundary [0x1000,0x3014], hibernate/wake protocol
- **AFE4900**: mode 0, 32-bit {8-addr|24-data} frames, SPI_READ read-back, register map 0x00–0x46 vs public EVM dumps, CONTROL0/CONTROL2 bit decodes. FIFO regs remain legitimately unverifiable (selective-disclosure part) — already TODO-marked with `USE_FIFO=0` escape
- **TCA6408**: reg map 0x00–0x03, POR output-latch=0xFF init ordering, INT-read-to-clear. *Schematic check needed*: addr 0x20 requires ADDR pin strapped LOW
- **MAX17048**: all 9 regs + ATHD formula + word-access requirement; the two `TODO(datasheet)` notes (VALRT byte order, CLKOUT FD=111→LOW) verified **correct as written**
- **RV-3028**: time regs, STATUS bits, EEPROM arm/cmd protocol (0x00/0x21/0x22), mirror access while EERD armed, 950 ms bus timeout
- **SHT45**: 0x44, cmd set, CRC-8 poly 0x31, T/RH formulas
- **TMP117**: strap range 0x48–0x4B, ID 0x0F=0x0117, config layout, one-shot MOD=11
- **MLX90632**: 0x3A strap semantics, 16-bit BE framing, full EE map vs upstream Linux driver, both medical & extended measurement flows
- **AS7341**: 0x39, ID reg 0x92 mask 0x24, SMUX sequence vs AN000666 examples, STATUS2 bits, bank-1 LED regs, timing/gain math
- **IM69D130**: I2S PDM mechanics all correct (SELECT=GND→left, standby<250 kHz, 50 ms discard)

## Design behavior confirmed (not bugs)

- **TX5/Wi-Fi burst interlock**: `vq_power_wifi_burst_ok()` denies bursts while
  TX5 is on (LED ~300 mA + Wi-Fi ~400 mA on one rail). With sensors streaming,
  cloud POSTs legitimately defer ~30 s — deferred, never dropped.
- **Deep-sleep prep is terminal**: `vq_power_prepare_sleep` parks sensors,
  hibernates the AD5940, sleeps W25Q512, holds CS/LED/MIC_CLK — subsequent
  console traffic revives what it can; full recovery rides the pipeline's
  fault/restart path (~30 s per sensor).

## Simulator-side defects found & fixed (not firmware)

- MAX86141/AD5940 models: treated each `spi_xfer` as a whole frame; the
  firmware splits command/read legs under held CS → converted to CS-aware
  byte-step machines (position persists across legs, resets on CS assert)
- W25Q512 not registered (name mismatch `vbfq_` vs `vbdev_` alias)
- Virtual clock µs-quantization produced fake `nonmono` timestamp anomalies
  at 500× scale → moved to ns precision
- GPIO shim ignored internal pulls → `vboard_gpio_pull` added; MISO float
  check now reads pullup=1/pulldown=0 correctly; `gpio_pullup_en` /
  `gpio_pulldown_en` now propagate to the board table like `set_pull_mode`
- Makefile `.o` prerequisites resolved to nonexistent *relative* paths
  (stem `home/...` + bare `%.c`) → existing objects treated as forever
  fresh; pattern fixed to `%.o: /%.c`
- `app image magic` — host flash image had no ESP header; seeded 0xE9 at
  the ota_0 base (0x20000) on fresh-image create
- Makefile ate intermediate objects → `.SECONDARY:` added

## Open items / known artifacts

- **`free(): invalid pointer` — ROOT-CAUSED AND FIXED (sim shim).** The
  heap shim prepended `alloc_hdr_t` to `heap_caps_*` blocks; IDF's
  unified heap makes `heap_caps_calloc`+`free()` legal, and firmware
  relies on it (`vq_console` logtail :916→:925/:931/:943/:1002, cloud
  resp path). glibc `free` on a header-offset pointer aborted the whole
  sim — the same class as the post-heartbeat crash. Shim now keeps
  metadata in a lock-protected side map keyed by pointer (no header);
  plain `free` on a caps block just works, `heap_caps_free` on a
  foreign block degrades to plain free. Verified: scn_selftest runs
  the `logtail` path clean.
- `led_sched submit: ESP_ERR_NO_MEM` / `grant lost` spam — pool churn under
  accelerated virtual time (grant windows drain faster than wall threads
  service them). At scale ≤2 this mostly subsides; residual NO_MEM during
  storms is a sim-pacing artifact, but the 8-slot pool + resubmit loop is
  worth a real-hardware soak.
- `i2c_scan` in the 0x44-fault phase shows 9 nodes (SHT45 silenced) — the
  scenario counts that as expected.
- `irq_latency` reports SKIP when ISR slots are driver-owned (polled edges
  still measured); `usb_cdc`/`sleep_current` are host-adapted.
- `i2c_master_probe` now runs a bare len-0 write leg (real address-phase
  ACK check) — rail-dead models NACK instead of ghost-ACKing.
- **Header-dependency blind spot**: `sim/Makefile` tracks `.c→.o` only —
  editing a shared header (`vq_env_priv.h`) leaves stale objects and an
  ABI-mismatch SIGSEGV at `vq_env_op_probe`. Workaround: `rm -rf
  sim/build/home sim/build/esp` after struct-layout edits (or add -MMD).
- **`ecg_bioz` in-suite residual**: passes solo, fails inside `selftest all`
  — RCAL e=263 after the suite's expander_walk/park churn. The `chip_wake`
  → `chip_init` recovery now engages (log shows `wake poll failed — re-init`)
  but RCAL's post-wake sequence still misses its IRQ window when the AFE
  re-entered mid-suite. Likely needs one pipeline-health cycle (~30 s) or
  a `vq_ad5940` explicit restore before RCAL. Documented as the remaining
  in-suite defect, not masked.
- `vsim_cmd` queues commands issued before the REPL exists — output lands
  in the *next* capture (harness footgun; `scn_selftest` gates on
  `vq_log_flash_present` + 10 virt-s).
- `vq_log` `rebuild_full()` on journal-less images takes >5 min wall
  through the byte-accurate flash model — sim perf limit, not a bug.

## Files

- Sim core: `sim/vboard/` (board+chips), `sim/esp_shim/` (IDF/FreeRTOS→POSIX),
  `sim/vsim/` (boot/console/sleep capture), `sim/mock/` (cloud server, model gen)
- Scenarios: `sim/tests/scn_{selftest,model,cloud,faults,busfault,storage,
  soak,boot,smoke,rcal}.c`
- Runner: `sim/run_all.sh` (`VSIM_TIME_SCALE` env or arg, default 2)
- Logs: `sim/build/*.log`, `sim/build/cloud_rx/`

## Bench recommendations

1. Soak the LED scheduler pool under sustained sensor LED activity.
2. Verify TX5/Wi-Fi interlock with a scope on J2 (VBUS) during POST+LED overlap.
3. Post-`sleep prepare` diagnostic path on real silicon: does the RCAL/IRQ
   wake sequence recover without a pipeline restart?
4. `ecg_bioz` needs a bus/pipeline arbitration story before bench CI.
5. Valgrind the cloud POST response path (heap abort above).
