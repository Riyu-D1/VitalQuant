# vitalq — firmware v2 (fix_v22 board)

ESP-IDF application for the fix_v22 board: ESP32-S3-WROOM-1-N16R8
(16 MB quad flash @ 3.3 V, 8 MB octal PSRAM). Full AFE set
(ADS1292R + AFE4900 + AD5940 + MAX86141), I2C sensor groups, on-device
INT8 model, USB-Serial/JTAG console.

Research prototype — engineering signals only, no clinical claims. Not a
medical device or diagnostic system; model outputs are experimental
anomaly scores.

## Prerequisites

- ESP-IDF **v5.4.2** at `~/esp/esp-idf`
- Target `esp32s3` (pinned by `sdkconfig.defaults`; a first
  `idf.py set-target esp32s3` is harmless belt-and-braces)

## Build

```sh
. ~/esp/esp-idf/export.sh
cd firmware/vitalq
idf.py build
```

First configure merges `sdkconfig.defaults` into a generated `sdkconfig`:
QIO flash @ 80 MHz / 16 MB, OCTAL PSRAM @ 80 MHz (malloc-capable,
GPIO35/36/37 reserved), console on USB-Serial/JTAG (GPIO19/20),
brownout ~2.7 V, custom `partitions.csv`, FreeRTOS 1 kHz tick, task WDT
15 s, 240 MHz CPU, NimBLE, FATFS x2 volumes. Changes to
`sdkconfig.defaults` only apply on a fresh configure — delete `sdkconfig`
(and `build/`, or `idf.py fullclean`) to force regen.

## Flash + console port

Native USB-Serial/JTAG on GPIO19/20 is THE console and flashing path — the
CP2102N data links (R104/R105) are DNP on fix_v22. The port enumerates as
`/dev/ttyACM*`:

```sh
idf.py -p /dev/ttyACM0 flash monitor
# manual equivalent (build emits the offset/file pairs):
esptool.py --chip esp32s3 -p /dev/ttyACM0 -b 460800 \
    write_flash @build/flash_args
```

`build/flash_args` covers: bootloader @0x0, partition-table @0x8000,
otadata @0xf000, app @0x20000. OTA slots ota_0/ota_1 are 4 MB each.

Recovery if the app disabled USB or wedges early: hold **BOOT (SW2 =
IO0)**, tap **RESET (SW1 = ESP_EN)**, release BOOT → ROM download mode;
the native-USB port still enumerates for esptool. The **J8 Tag-Connect
(TC2030)** header is the UART0 alternative — pins
+3V3/GND/ESP_TX(IO43)/ESP_RX(IO44)/EN/IO0 — use any USB-UART adapter +
esptool, or for a serial log when USB is unusable.

Note: after deep sleep the native USB re-enumerates — reopen the host
port after wake.

## On-device model (VQMB)

Dedicated `model` data partition (subtype 0x40) at **0x820000**, 4 MiB —
see `partitions.csv`. INT8 weights are mmap'd from flash zero-copy; the
activation arena lives in PSRAM. Budget per the hw_v3 profile: ~1–2 M
parameters, int8 only.

`tools/export_model.py` (stdlib only) is the producer side:

```sh
python3 tools/export_model.py export --graph net.json [--weights dir] \
        [--out vqmodel.bin] [--c-header model_bin.h]
python3 tools/export_model.py stub          # deterministic 40→24→24→2 MLP for bring-up
python3 tools/export_model.py info vqmodel.bin   # header/tensor/node dump + crc check
python3 tools/export_model.py flash vqmodel.bin --port /dev/ttyACM0  # prints:
#   esptool.py --chip esp32s3 --port <port> --baud 460800 write_flash 0x820000 vqmodel.bin
#   parttool.py --port <port> write_partition --partition-name=model --input vqmodel.bin
```

`vq_model_load()` tries the partition first; absent/corrupt blob falls
back to the compiled-in stub (`vq_edge_model/stub_model_bin.h`, same
VQMB format) so the decision pipeline is always end-to-end. The `model`
console command reports which source is live.

## Console commands

esp_console REPL on the USB port, `vitalq>` prompt, `help` lists:

| command | what it does |
|---|---|
| `selftest [all\|<name>\|list]` | run the §9 bring-up suite |
| `status` | boot + power + wifi + last decision + log space |
| `sensors` | registered drivers: rate / samples / errors / last stamp / health |
| `scan` | I2C walk 0x08–0x77 with expected-device check |
| `wifi set <ssid> <pass>` · `wifi connect` · `wifi status` · `wifi disconnect` | STA control, creds in NVS |
| `cloud set <url> <key>` · `cloud flush [n]` · `cloud heartbeat` · `cloud stats` | uplink control |
| `model [info]` | load partition blob, print inputs/outputs/params/arena + selftest report |
| `score` | one decision cycle (prob/threshold/escalated/latency — advisory only) |
| `led <chain> <ms>` | one-shot scheduler test pulse; status LED mirrors the slot |
| `power status` · `power prepare` · `power sleep` | state, §4.4 park checklist, enter deep sleep |
| `sleep` | alias for `power sleep` — does not return over this console |
| `logtail [n] [flash]` | console event ring; `flash` reads stored events from log flash |
| `fsr` | FSR ADC selftest shortcut |
| `mic [s]` | mic_pdm selftest (PDM capture → RMS) |
| `reboot` | esp_restart() |
| `profile` | hw_v3 key facts (GPIO map, caps, battery limits, model contract) |

## Selftests (hw brief §9)

`selftest list` / `selftest all` / `selftest <name>`; report format
`<name>: PASS|FAIL|SKIP — <measured values>`. SKIP means absent-by-design
or probe-optional, never a required device. In table order:

```
rails_boot  i2c_scan  expander_walk  spi_ids  miso_contention  irq_latency
power_paths usb_cdc   sleep_current  optical  ecg_bioz         mic_pdm
fsr_adc     model_infer            led_interlock              decision_loop
```

Deep-sleep test: `selftest sleep_current` runs the §4.4 park checklist
(sensors parked, CS/LED/MIC_CLK held, wake sources armed) and prints the
J2 series-jumper measurement note — standby target ~110 µA. It does
**not** sleep. `selftest sleep_current commit` (or `power sleep`) enters
deep sleep for real: USB CDC drops, no return; wake via GAUGE_ALRT (IO6),
EXP_INT (IO3), LSM6 (IO4), or VBUS.

## Layout

```
main/main.c            boot order: board_init → buses → expander → services
                       (clock/power/led/log) → pipeline → decision loop →
                       net → console last. Nothing optional is fatal.
partitions.csv         flash map: ota_0/ota_1 4MB, model @0x820000 4MB,
                       storage FAT 0xc20000, coredump @0xff0000
sdkconfig.defaults     N16R8 pinned config (see Build)
tools/export_model.py  VQMB model exporter/inspector/flash helper
components/
  vq/          interface-only — every contract header in include/:
               vq_types, vq_sensor, board_pins, board_init, i2c_bus,
               spi_bus, expander, clock_service, power_mgr, led_sched,
               log_store, pipeline, features, vq_model, decision,
               wifi_mgr, cloud_client, vq_console, selftest
  vq_board     earliest bring-up: strap read-back (IO45/46/3/0), CS high
  vq_i2c       single I2C0 master, 100 kHz hard limit, mutexed helpers
  vq_spi       single SPI2 master, 5 CS, per-device clock caps, one-CS rule
  vq_expander  TCA6408 @0x20: AFE resets/PWDN, TX5_EN, IR_GATE, charge ctl
  vq_clock     esp_timer monotonic domain + wall-clock anchor (SNTP/RV-3028)
  vq_power     battery/charge/sleep governor — firmware IS the cell cutoff
  vq_led       hard-interlock LED slot scheduler; owns the TX_5V window
  vq_logstore  W25Q512 64 MB append-only CRC record ring (4-byte addressing)
  vq_pipeline  registry walk, per-driver tasks, sample fan-out
  vq_features  windowed extractor → 40-dim model input vector
  vq_edge_model VQMB loader + INT8 graph executor (vq_kernels.c op set)
  vq_decision  policy loop (default 180 s): features → model → escalation
  vq_net       STA Wi-Fi manager + LAN status endpoint
  vq_cloud     HTTPS uplink + cloud advisory flow
  vq_selftest  §9 bring-up suite harness
  vq_console   USB-Serial/JTAG REPL (commands above)
  vq_drv_ads1292   ADS1292R biopotential AFE (ecg.ii, resp) — SPI ≤4 MHz
  vq_drv_afe4900   AFE4900 main PPG (ppg.red/ppg.ir) — SPI ≤8 MHz
  vq_drv_max86141  MAX86141 satellite PPG, J10/SFH7050A — CS doubles as
                   U26 MISO shifter OE
  vq_drv_ad5940    AD5940 impedance AFE: EDA + sweat logical splits
  vq_drv_island    1.8 V island: AS7341 + MLX90632 behind PCA9306
  vq_drv_env       env group: BME280, SHT45, TMP117 x3 (skin/dome/J9 tail)
  vq_drv_aux       aux group: MAX17048 gauge, RV-3028 RTC
  vq_drv_imu       LSM6DSV80X 6-axis IMU @0x6A
  vq_drv_fsr       FSR402 contact (ADC1_CH0) + status LED
  vq_drv_mic       IM69D130 PDM mic on I2S0
```

## Hard rules — the MUST-NOT list

Violation is a board-risk bug, not a style issue:

- **Forbidden GPIO:** 26–32 (internal flash/PSRAM), 35/36/37 (octal
  PSRAM), 45 (VDD_SPI strap — must read 0, never drive), 46/47/48 (NC).
  IO0/IO3/IO46 are straps — input-only. GPIO19/20 are native USB D-/D+ —
  never configure as GPIO.
- **I2C0: 100 kHz hard limit** — 2.2k pull-ups give ~503 ns rise, 400 kHz
  fails spec. Never raise it.
- **SPI: one CS asserted at a time**, all CS idle HIGH (10k pull-ups).
  Clock caps: ADS1292 ≤4 MHz (mode 1), AD5940/AFE4900/MAX86141 ≤8 MHz
  (mode 0), W25Q512 ≤40 MHz.
- **LED power: one chain per slot** (all-together ~900 mA vs ~400 mA
  TPS61240 capability), ≥300 mA-class pulses ≤1% duty, and TX_5V is on
  only inside a scheduler window via `vq_led_tx5_acquire()`/
  `vq_led_tx5_release()` — never drive TX5_EN directly.
- **Battery cutoffs (cell assumed unprotected):** ≤3.2 V / 5% SOC → stop
  LEDs+RF, save, deep sleep; ≤3.0 V → emergency shutdown; never enable
  TX_5V below 3.3 V. Assert CHG_DIS during heavy bursts (500 mA USB
  budget).
- **Never burn eFuses** — VDD_SPI stays strap-selected (IO45 low); no
  secure-boot/flash-encryption/key eFuse writes, ever.
- **No GPIO19/20 reclaim** — the console is the recovery path and it
  stays.

## Open bench items

- 63 inline `TODO(datasheet)` markers across the drivers — most are in
  `vq_drv_afe4900` (TI SBAS857 is selective-disclosure; the register map
  in `afe4900_regs.h` is placeholder until the full datasheet lands) and
  `vq_drv_ad5940`. Grep before trusting any AFE4900 register decode.
- **AFE4900 I2C_SPI_SEL polarity unconfirmed.** The pin is tied to
  +3V3_ANA and the pin audit records 1=SPI, so the part should answer
  SPI. If a schematic re-spin shows the opposite polarity the part is
  really in I2C mode and all SPI traffic is a no-op → build with
  `VQ_AFE4900_SPI_ENABLE=0` and the probe degrades to a graceful absent.
- Probe-optional by design: RV-3028 RTC (INT unwired, no backup cell —
  re-anchor after power loss), IM69D130 mic, J9-tail TMP117 @0x4A.
  Absence is tolerated, never fatal.
