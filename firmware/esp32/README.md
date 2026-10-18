# vitalq-firmware (ESP32)

Skeleton for the M2 milestone (docs/09-roadmap.md). Not built yet — the contract
below is what drivers implement when hardware lands.

## Design contract (from docs/03, 05)

- `SensorRegistry` loads the compiled-in `profiles/hw_v*.yaml` default and accepts a
  runtime override; boot reports `profile_hash` in every batch.
- Every driver implements:

  ```cpp
  class SensorDriver {
   public:
    virtual bool init(const SensorConfig& cfg) = 0;          // bus, addr, rate
    virtual size_t read(SampleFrame& out) = 0;               // fills timestamped samples
    virtual bool healthy() = 0;                              // sensor_fault detection
    virtual const char* channel_id() = 0;                    // vitalq.core channel id
  };
  ```

- `ClockService` owns `esp_timer` monotonic µs + SNTP discipline; every sample's
  timestamp is taken **in the driver task, never on transmit**.
- `RingBuffer` (flash or SD per `storage.offline_buffer`) survives Wi-Fi loss;
  `BatchUploader` sends `BatchIngest`-shaped JSON with `batch_id` idempotency and
  honours the server's returned `clock_correction`.

## Sensor wiring contract

I²C map is declared in the profile — drivers must not hard-code addresses
(MLX90637 supports software-defined addressing; use it if a second IR sensor
appears). MAX86141 rides SPI; MAX30102 is I²C `0x57`.

## Layout to fill in M2

```
drivers/   per-IC implementations (max30102, max86141, as7341, mlx, bme, imu, fsr)
core/      clock_service.*, ring_buffer.*, batch_uploader.*, sensor_registry.*
profiles/  hw_v0.yaml (breakout rig), hw_v1.yaml (custom PCB target)
```
