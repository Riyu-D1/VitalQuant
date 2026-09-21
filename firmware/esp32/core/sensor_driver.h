// VitalQ firmware — driver contract (skeleton; M2 milestone).
// See firmware/esp32/README.md. All timestamps are esp_timer monotonic µs,
// taken in the driver's sampling task.
#pragma once
#include <stdint.h>
#include <stddef.h>

struct Sample {
  const char* channel_id;   // matches vitalq.core channel registry
  int64_t     t_us;         // device monotonic µs at acquisition
  double      value;
};

struct SensorConfig {
  int      bus;             // i2c0/spi1 index
  uint8_t  address;
  float    rate_hz;
  // model-specific params resolved from the hardware profile
  const void* model_params;
};

class SensorDriver {
 public:
  virtual ~SensorDriver() = default;
  virtual bool   init(const SensorConfig& cfg) = 0;
  virtual size_t read(Sample* out, size_t max_samples) = 0;  // >0 or 0 (none ready)
  virtual bool   healthy() = 0;          // false → emits sensor_fault event
};
