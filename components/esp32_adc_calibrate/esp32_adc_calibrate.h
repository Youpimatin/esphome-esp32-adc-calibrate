#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

#include <stdint.h>

namespace esphome {
namespace esp32_adc_calibrate {

static constexpr uint32_t LUT_MAGIC = 0x41444331;  // "ADC1"
static constexpr uint16_t LUT_VERSION = 1;

static constexpr uint16_t DAC_MAX = 255;
static constexpr uint16_t ADC_MAX = 4095;
static constexpr uint16_t LUT_SIZE = 256;

struct LUTStorage {
  uint32_t magic;
  uint16_t version;

  uint16_t samples;
  uint16_t settle_ms;

  // Index = DAC code 0..255
  // Value = ADC raw value 0..4095
  uint16_t adc[LUT_SIZE];

  uint32_t checksum;
};

class ESP32ADCComponent : public PollingComponent {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;

  void set_adc_pin(uint8_t pin) {
    this->adc_pin_ = pin;
  }

  void set_dac_pin(uint8_t pin) {
    this->dac_pin_ = pin;
  }

  void set_samples(uint16_t samples) {
    this->samples_ = samples;
  }

  void set_settle_ms(uint16_t settle_ms) {
    this->settle_ms_ = settle_ms;
  }

  void set_restore_from_flash(bool value) {
    this->restore_from_flash_ = value;
  }

  void set_calibrate_on_first_boot(bool value) {
    this->calibrate_on_first_boot_ = value;
  }

  void set_sensor(sensor::Sensor *sensor) {
    this->sensor_ = sensor;
  }

  void start_calibration();

 protected:
  bool generate_lut_();
  bool load_lut_();
  bool save_lut_();

  uint32_t calculate_checksum_(
      const LUTStorage &lut
  ) const;

  float adc_to_dac_(
      uint16_t adc
  ) const;

  uint16_t read_adc_average_() const;

  uint8_t adc_pin_{35};
  uint8_t dac_pin_{25};

  uint16_t samples_{16};
  uint16_t settle_ms_{5};

  bool restore_from_flash_{true};
  bool calibrate_on_first_boot_{true};

  bool calibrated_{false};
  bool calibrating_{false};
  bool calibration_requested_{false};

  LUTStorage lut_{};

  sensor::Sensor *sensor_{nullptr};
};

}  // namespace esp32_adc_calibrate
}  // namespace esphome
