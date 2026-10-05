// VitalQ firmware — driver contract (skeleton; M2 milestone).
// See firmware/esp32/README.md. All timestamps are esp_timer monotonic µs.
//
// ============================================================================
// FIFO TIMESTAMP CONTRACT (hw_v2 fix — see hardware/pcb/HW_V2_SPEC.md)
// ============================================================================
// Every Sample MUST carry a device-clock timestamp taken at ACQUISITION time —
// never at FIFO pop, and never at transmit/batch-assembly time.
//
// The three streaming AFEs on the shared SPI2 host run on INDEPENDENT sample
// clocks (different time domains — each has its own free-running internal
// sequencer). They do NOT share a sample counter:
//   - ADS1292R  (ecg/resp, CS=GPIO10) → DRDY      on GPIO2  (timestamped input)
//   - AFE4900   (ppg,      CS=GPIO18) → ADC_RDY   on GPIO21 (timestamped input)
//   - MAX86178  (satellite ppg J10, CS=GPIO38) → INT on GPIO39 (timestamped input)
// Each driver's ISR/driver-task MUST latch esp_timer µs at the data-ready edge
// and carry that per-sample stamp through FIFO draining (stamp the edge, then
// offset per in-FIFO index where the device supplies a sample counter — i.e.
// maintain a per-device anchor: {device sample index ↔ esp_timer µs}).
// Samples from different AFEs are aligned ONLY via their esp_timer stamps;
// cross-device skew between DRDY/INT edges is expected and preserved — the
// backend clock model never sees FIFO-pop or transmit times.
//
// Wall-clock anchoring: the per-batch ClockAnchorIn (wall_time ↔ monotonic_us)
// anchors esp_timer to utc (SNTP). When populated, the DNP RTC (RV-3028-C7,
// I2C3V3 0x52) provides the absolute wall-clock source for multi-day logging;
// until then SNTP is the only wall-clock reference.
// ============================================================================
//
// hw_v2 device roster (I2C3V3 unless noted; full map in profiles/hw_v2.yaml):
//   ADS1292R  ecg/resp    SPI CS=10, DRDY=GPIO2, power-down = TCA6408 P1
//             (ADS1292_PWDN; held low by R14 until P1 releases it)
//   AFE4900   ppg (SFH7072) SPI CS=18, ADC_RDY=GPIO21, reset = TCA6408 P0
//             (AFE4900_RESETZ; held in reset by R9 until P0 releases it)
//   AD5940    eda + sweat SPI CS=15, reset = TCA6408 P2 (AD5940_RESET);
//             GPIO2 (ball E1) drives NIR730_GATE (see below); J11 WE/RE/CE →
//             spare mux channel (sweat.lactate is RESEARCH-GRADE — never a
//             clinical output)
//   MAX86178  satellite ppg (J10, SFH7050A) SPI CS=38, INT=GPIO39
//   W25Q512   log flash   SPI CS=16; /WP and /HOLD are tied to +3V3 — the
//             expander does not switch the flash
//   TCA6408   io expander 0x20, INT=GPIO3 — P0-P7: AFE4900_RESETZ, ADS1292_PWDN,
//             AD5940_RESET, TX5_EN, IR_GATE, CHG_STAT, VBUS_DET, CHG_DIS.
//             P5/P6 are inputs (charger status, VBUS detect). WARNING: P7 is
//             CHG_DIS — driving it high stops charging (Q2 shorts TS); it is
//             not a user/key input
//   MAX17048  fuel gauge  0x36 (GAUGE_ALRT=GPIO6) → sys.battery_v
//   SHT45     skin rh+temp 0x44 → skin.rh, skin.temp
//   TMP117    ×3: 0x48/0x49 ZHF pair (dome) → core.temp_est; 0x4A distal (J9)
//             → skin.temp_distal; TMP117_ALERT=GPIO5
//   BME280    env 0x76 · LSM6DSV80X motion 0x6A (LSM6_INT1=GPIO4)
//   AS7341    spectral 0x39 + MLX90632 IR temp 0x3A — both on I2C1V8 (PCA9306)
//   RV-3028-C7 RTC 0x52 — DNP · IM69D130 PDM mic GPIO40/41 — DNP
//
// NIR730_GATE control path: AD5940 GPIO2 (ball E1) → Q4 gate → D12 730 nm
// LED cathode; firmware toggles it through the AD5940 GPIO registers over
// SPI. ESP32 GPIO14 ↔ AD5940 GPIO0 (ball F5, net AD5940_GPIO0) is a
// different sideband link — the gate is NOT on that pin. The spectral
// driver sequences emitter_on/dark_frame with this gate; the AS7341 nir
// channel ratioed against it yields derived channel tissue.sto2.
#pragma once
#include <stdint.h>
#include <stddef.h>

struct Sample {
  const char* channel_id;   // matches vitalq.core channel registry
  int64_t     t_us;         // device monotonic µs AT ACQUISITION (data-ready edge
                            // or per-sample device-clock anchor — see FIFO contract
                            // above; NOT FIFO-pop time, NOT transmit time)
  double      value;
};

struct SensorConfig {
  int      bus;             // i2c0/spi1 index
  uint8_t  address;         // i2c addr; SPI parts use profile `cs`/`int_pin` instead
  float    rate_hz;
  // model-specific params resolved from the hardware profile
  const void* model_params;
};

class SensorDriver {
 public:
  virtual ~SensorDriver() = default;
  virtual bool   init(const SensorConfig& cfg) = 0;
  // Fills timestamped samples; each Sample.t_us must be its acquisition stamp
  // (per-device anchor — see contract above). >0 samples, or 0 (none ready).
  virtual size_t read(Sample* out, size_t max_samples) = 0;
  virtual bool   healthy() = 0;          // false → emits sensor_fault event
};
