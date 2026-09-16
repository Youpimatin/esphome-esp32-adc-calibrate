#include "esp32_adc_calibrate.h"

#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

#include <Arduino.h>

namespace esphome {
namespace esp32_adc_calibrate {

static const char *const TAG =
    "esp32_adc_calibrate";

/*
 * Preference key.
 *
 * The LUT is stored through ESPHome's preference system,
 * so it survives reboot.
 */
static constexpr uint32_t PREF_KEY =
    0xADC32535;


/* -------------------------------------------------------------------------- */
/* Configuration                                                              */
/* -------------------------------------------------------------------------- */

void ESP32ADCComponent::dump_config() {
  ESP_LOGCONFIG(
      TAG,
      "ESP32 ADC/DAC calibration"
  );

  ESP_LOGCONFIG(
      TAG,
      "  ADC pin: GPIO%u",
      this->adc_pin_
  );

  ESP_LOGCONFIG(
      TAG,
      "  DAC pin: GPIO%u",
      this->dac_pin_
  );

  ESP_LOGCONFIG(
      TAG,
      "  ADC resolution: 12 bit (0..4095)"
  );

  ESP_LOGCONFIG(
      TAG,
      "  DAC resolution: 8 bit (0..255)"
  );

  ESP_LOGCONFIG(
      TAG,
      "  Samples: %u",
      this->samples_
  );

  ESP_LOGCONFIG(
      TAG,
      "  Settle time: %u ms",
      this->settle_ms_
  );

  ESP_LOGCONFIG(
      TAG,
      "  Restore LUT: %s",
      YESNO(this->restore_from_flash_)
  );

  ESP_LOGCONFIG(
      TAG,
      "  Calibrate first boot: %s",
      YESNO(this->calibrate_on_first_boot_)
  );
}


/* -------------------------------------------------------------------------- */
/* Setup                                                                      */
/* -------------------------------------------------------------------------- */

void ESP32ADCComponent::setup() {
  pinMode(
      this->adc_pin_,
      INPUT
  );

  /*
   * Classic ESP32 ADC:
   * 12-bit resolution = 0..4095.
   */
  analogReadResolution(12);

  /*
   * Attenuation is deliberately part of the physical
   * configuration. The LUT calibrates the resulting
   * ADC codes, so no voltage conversion is needed.
   */
  analogSetPinAttenuation(
      this->adc_pin_,
      ADC_11db
  );

  /*
   * Try to restore the previously calibrated LUT.
   */
  if (
      this->restore_from_flash_ &&
      this->load_lut_()
  ) {
    this->calibrated_ = true;

    ESP_LOGI(
        TAG,
        "Valid calibration LUT restored from flash"
    );

    return;
  }

  ESP_LOGI(
      TAG,
      "No valid calibration LUT found"
  );

  /*
   * First boot calibration.
   */
  if (
      this->calibrate_on_first_boot_
  ) {
    this->calibration_requested_ = true;
  }
}


/* -------------------------------------------------------------------------- */
/* Main loop                                                                  */
/* -------------------------------------------------------------------------- */

void ESP32ADCComponent::update() {
  /*
   * Calibration has priority over normal measurements.
   */
  if (
      this->calibration_requested_
  ) {
    if (
        this->calibrating_
    ) {
      return;
    }

    this->calibration_requested_ = false;
    this->calibrating_ = true;

    ESP_LOGI(
        TAG,
        "Starting ADC/DAC calibration"
    );

    const bool generated =
        this->generate_lut_();

    bool saved = false;

    if (generated) {
      saved = this->save_lut_();
    }

    /*
     * Never leave the DAC enabled after calibration.
     */
    dacDisable(
        this->dac_pin_
    );

    this->calibrating_ = false;

    if (
        generated &&
        saved
    ) {
      this->calibrated_ = true;

      ESP_LOGI(
          TAG,
          "ADC/DAC calibration complete"
      );
    } else {
      ESP_LOGE(
          TAG,
          "ADC/DAC calibration failed"
      );
    }

    return;
  }

  /*
   * No valid LUT -> no usable result.
   */
  if (
      !this->calibrated_ ||
      this->sensor_ == nullptr
  ) {
    return;
  }

  /*
   * Read the physical ADC.
   */
  const uint16_t adc =
      this->read_adc_average_();

  /*
   * Convert:
   *
   * ADC 12-bit
   *     ↓
   * LUT + interpolation
   *     ↓
   * equivalent DAC 8-bit
   *
   * The result remains a float so that the
   * information between two DAC codes is preserved.
   */
  const float dac =
      this->adc_to_dac_(
          adc
      );

  this->sensor_->publish_state(
      dac
  );
}


/* -------------------------------------------------------------------------- */
/* ADC measurement                                                             */
/* -------------------------------------------------------------------------- */

uint16_t ESP32ADCComponent::read_adc_average_() const {
  uint32_t total = 0;

  for (
      uint16_t i = 0;
      i < this->samples_;
      i++
  ) {
    total += analogRead(
        this->adc_pin_
    );

    delayMicroseconds(100);
  }

  return static_cast<uint16_t>(
      total /
      this->samples_
  );
}


/* -------------------------------------------------------------------------- */
/* Manual calibration                                                         */
/* -------------------------------------------------------------------------- */

void ESP32ADCComponent::start_calibration() {
  if (
      this->calibrating_
  ) {
    ESP_LOGW(
        TAG,
        "Calibration already running"
    );

    return;
  }

  ESP_LOGI(
      TAG,
      "Manual calibration requested"
  );

  this->calibration_requested_ = true;
}


/* -------------------------------------------------------------------------- */
/* LUT generation                                                             */
/* -------------------------------------------------------------------------- */

bool ESP32ADCComponent::generate_lut_() {
  this->lut_.magic =
      LUT_MAGIC;

  this->lut_.version =
      LUT_VERSION;

  this->lut_.samples =
      this->samples_;

  this->lut_.settle_ms =
      this->settle_ms_;

  ESP_LOGI(
      TAG,
      "Generating %u-point LUT",
      LUT_SIZE
  );

  ESP_LOGI(
      TAG,
      "DAC GPIO%u -> ADC GPIO%u",
      this->dac_pin_,
      this->adc_pin_
  );

  uint16_t previous_adc = 0;

  /*
   * DAC = 0..255.
   *
   * Each DAC code gets its own measured ADC value.
   */
  for (
      uint16_t dac = 0;
      dac <= DAC_MAX;
      dac++
  ) {
    dacWrite(
        this->dac_pin_,
        static_cast<uint8_t>(
            dac
        )
    );

    /*
     * Give the analog circuit time to settle.
     */
    delay(
        this->settle_ms_
    );

    const uint16_t adc =
        this->read_adc_average_();

    uint16_t calibrated_adc =
        adc;

    /*
     * The inverse lookup assumes a monotonic
     * DAC -> ADC curve.
     *
     * ADC noise can cause an occasional decrease.
     * Clamp it to preserve monotonicity.
     */
    if (
        dac > 0 &&
        calibrated_adc < previous_adc
    ) {
      ESP_LOGW(
          TAG,
          "Non-monotonic point DAC=%u: "
          "ADC %u < previous %u, clamping",
          dac,
          calibrated_adc,
          previous_adc
      );

      calibrated_adc =
          previous_adc;
    }

    this->lut_.adc[dac] =
        calibrated_adc;

    previous_adc =
        calibrated_adc;

    ESP_LOGD(
        TAG,
        "DAC=%u -> ADC=%u",
        dac,
        calibrated_adc
    );
  }

  /*
   * Turn the DAC off when finished.
   */
  dacDisable(
      this->dac_pin_
  );

  /*
   * Calculate checksum after all LUT data
   * has been generated.
   */
  this->lut_.checksum =
      this->calculate_checksum_(
          this->lut_
      );

  ESP_LOGI(
      TAG,
      "LUT generated successfully"
  );

  ESP_LOGI(
      TAG,
      "ADC range: %u .. %u",
      this->lut_.adc[0],
      this->lut_.adc[255]
  );

  return true;
}


/* -------------------------------------------------------------------------- */
/* ADC -> DAC interpolation                                                   */
/* -------------------------------------------------------------------------- */

float ESP32ADCComponent::adc_to_dac_(
    uint16_t adc
) const {
  /*
   * Below the first measured point.
   */
  if (
      adc <=
      this->lut_.adc[0]
  ) {
    return 0.0f;
  }

  /*
   * Above the last measured point.
   */
  if (
      adc >=
      this->lut_.adc[DAC_MAX]
  ) {
    return 255.0f;
  }

  /*
   * Find the two DAC codes surrounding the
   * measured ADC value.
   *
   * Example:
   *
   * DAC 127 -> ADC 2028
   * DAC 128 -> ADC 2044
   *
   * ADC = 2036
   *
   * ratio = (2036 - 2028) / (2044 - 2028)
   *       = 0.5
   *
   * DAC = 127.5
   */
  for (
      uint16_t dac = 1;
      dac <= DAC_MAX;
      dac++
  ) {
    const uint16_t adc0 =
        this->lut_.adc[dac - 1];

    const uint16_t adc1 =
        this->lut_.adc[dac];

    if (
        adc <= adc1
    ) {
      /*
       * Adjacent ADC values can be identical due
       * to quantization or noise.
       */
      if (
          adc1 <= adc0
      ) {
        return static_cast<float>(
            dac
        );
      }

      const float ratio =
          static_cast<float>(
              adc - adc0
          ) /
          static_cast<float>(
              adc1 - adc0
          );

      return
          static_cast<float>(
              dac - 1
          ) +
          ratio;
    }
  }

  return 255.0f;
}


/* -------------------------------------------------------------------------- */
/* Flash checksum                                                             */
/* -------------------------------------------------------------------------- */

uint32_t ESP32ADCComponent::calculate_checksum_(
    const LUTStorage &lut
) const {
  /*
   * FNV-1a 32-bit.
   *
   * Exclude the checksum field itself.
   */
  const uint8_t *data =
      reinterpret_cast<const uint8_t *>(
          &lut
      );

  const size_t length =
      sizeof(LUTStorage) -
      sizeof(lut.checksum);

  uint32_t hash =
      2166136261UL;

  for (
      size_t i = 0;
      i < length;
      i++
  ) {
    hash ^= data[i];
    hash *= 16777619UL;
  }

  return hash;
}


/* -------------------------------------------------------------------------- */
/* Flash save                                                                  */
/* -------------------------------------------------------------------------- */

bool ESP32ADCComponent::save_lut_() {
  auto preference =
      global_preferences
          ->make_preference<LUTStorage>(
              PREF_KEY
          );

  if (
      !preference.save(
          &this->lut_
      )
  ) {
    ESP_LOGE(
        TAG,
        "Failed to save LUT to flash"
    );

    return false;
  }

  ESP_LOGI(
      TAG,
      "Calibration LUT saved to flash"
  );

  return true;
}


/* -------------------------------------------------------------------------- */
/* Flash load                                                                  */
/* -------------------------------------------------------------------------- */

bool ESP32ADCComponent::load_lut_() {
  auto preference =
      global_preferences
          ->make_preference<LUTStorage>(
              PREF_KEY
          );

  LUTStorage stored{};

  if (
      !preference.load(
          &stored
      )
  ) {
    return false;
  }

  if (
      stored.magic !=
      LUT_MAGIC
  ) {
    ESP_LOGW(
        TAG,
        "Invalid LUT magic"
    );

    return false;
  }

  if (
      stored.version !=
      LUT_VERSION
  ) {
    ESP_LOGW(
        TAG,
        "Unsupported LUT version"
    );

    return false;
  }

  /*
   * A LUT generated with different calibration
   * parameters is not reused.
   */
  if (
      stored.samples !=
      this->samples_
  ) {
    ESP_LOGW(
        TAG,
        "LUT sample count changed"
    );

    return false;
  }

  if (
      stored.settle_ms !=
      this->settle_ms_
  ) {
    ESP_LOGW(
        TAG,
        "LUT settle time changed"
    );

    return false;
  }

  const uint32_t checksum =
      this->calculate_checksum_(
          stored
      );

  if (
      checksum !=
      stored.checksum
  ) {
    ESP_LOGW(
        TAG,
        "Invalid LUT checksum"
    );

    return false;
  }

  /*
   * Validate monotonicity.
   */
  for (
      uint16_t i = 1;
      i <= DAC_MAX;
      i++
  ) {
    if (
        stored.adc[i] <
        stored.adc[i - 1]
    ) {
      ESP_LOGW(
          TAG,
          "LUT is not monotonic"
      );

      return false;
    }
  }

  this->lut_ =
      stored;

  return true;
}

}  // namespace esp32_adc_calibrate
}  // namespace esphome
