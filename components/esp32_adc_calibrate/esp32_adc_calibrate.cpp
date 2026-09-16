#include "esp32_adc_calibrate.h"

#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

#include <Arduino.h>
#include <math.h>

namespace esphome {
namespace esp32_adc_calibrate {

static const char *const TAG =
    "esp32_adc_calibrate";

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
      "  ADC resolution: 12 bit"
  );

  ESP_LOGCONFIG(
      TAG,
      "  DAC resolution: 8 bit"
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
   * Classic ESP32:
   * ADC resolution = 12 bits = 0..4095.
   */
  analogReadResolution(12);

  /*
   * Keep the attenuation fixed for the calibration.
   * The LUT operates entirely in raw ADC-code space.
   */
  analogSetPinAttenuation(
      this->adc_pin_,
      ADC_11db
  );

  /*
   * Try to restore an existing LUT.
   */
  if (
      this->restore_from_flash_ &&
      this->load_lut_()
  ) {
    this->calibrated_ = true;

    ESP_LOGI(
        TAG,
        "Calibration LUT restored from flash"
    );

    return;
  }

  ESP_LOGI(
      TAG,
      "No valid calibration LUT found"
  );

  if (
      this->calibrate_on_first_boot_
  ) {
    this->calibration_requested_ = true;
  }
}


/* -------------------------------------------------------------------------- */
/* Periodic component                                                         */
/* -------------------------------------------------------------------------- */

void ESP32ADCComponent::update() {
  /*
   * Calibration has priority.
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
        "Starting calibration"
    );

    const bool generated =
        this->generate_lut_();

    bool saved = false;

    if (generated) {
      saved = this->save_lut_();
    }

    /*
     * Never leave the DAC enabled.
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
          "Calibration complete"
      );
    } else {
      this->calibrated_ = false;

      ESP_LOGE(
          TAG,
          "Calibration failed"
      );
    }

    return;
  }
}


/* -------------------------------------------------------------------------- */
/* ADC read                                                                   */
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
/* Public read()                                                              */
/* -------------------------------------------------------------------------- */

float ESP32ADCComponent::read() {
  if (
      !this->calibrated_
  ) {
    return NAN;
  }

  if (
      this->calibrating_
  ) {
    return NAN;
  }

  const uint16_t adc =
      this->read_adc_average_();

  return this->adc_to_dac_(
      adc
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

  this->calibration_requested_ = true;

  ESP_LOGI(
      TAG,
      "Calibration requested"
  );
}


/* -------------------------------------------------------------------------- */
/* Generate LUT                                                               */
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

  for (
      uint16_t dac = 0;
      dac <= DAC_MAX;
      dac++
  ) {
    /*
     * Output DAC code 0..255.
     */
    dacWrite(
        this->dac_pin_,
        static_cast<uint8_t>(
            dac
        )
    );

    /*
     * Let the analog signal settle.
     */
    delay(
        this->settle_ms_
    );

    uint16_t adc =
        this->read_adc_average_();

    /*
     * Ensure a monotonic DAC -> ADC curve.
     *
     * Small downward movements caused by noise
     * are clamped.
     */
    if (
        dac > 0 &&
        adc < previous_adc
    ) {
      ESP_LOGW(
          TAG,
          "Non-monotonic point DAC=%u: "
          "ADC=%u < previous=%u, clamping",
          dac,
          adc,
          previous_adc
      );

      adc =
          previous_adc;
    }

    this->lut_.adc[dac] =
        adc;

    previous_adc =
        adc;

    ESP_LOGD(
        TAG,
        "DAC=%u -> ADC=%u",
        dac,
        adc
    );
  }

  dacDisable(
      this->dac_pin_
  );

  this->lut_.checksum =
      this->calculate_checksum_(
          this->lut_
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
   * Below the calibrated range.
   */
  if (
      adc <=
      this->lut_.adc[0]
  ) {
    return 0.0f;
  }

  /*
   * Above the calibrated range.
   */
  if (
      adc >=
      this->lut_.adc[DAC_MAX]
  ) {
    return 255.0f;
  }

  /*
   * Search the two calibration points
   * surrounding the ADC measurement.
   *
   * Example:
   *
   * LUT[127] = 2028
   * LUT[128] = 2044
   *
   * ADC = 2036
   *
   * ratio = 8 / 16
   *       = 0.5
   *
   * result = 127.5
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
       * Adjacent LUT values can be identical.
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
/* Checksum                                                                   */
/* -------------------------------------------------------------------------- */

uint32_t ESP32ADCComponent::calculate_checksum_(
    const LUTStorage &lut
) const {
  const uint8_t *data =
      reinterpret_cast<const uint8_t *>(
          &lut
      );

  const size_t length =
      sizeof(LUTStorage) -
      sizeof(lut.checksum);

  /*
   * FNV-1a 32-bit.
   */
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
/* Save LUT                                                                   */
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
        "Failed to save LUT"
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
/* Load LUT                                                                   */
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
   * Calibration parameters must match.
   */
  if (
      stored.samples !=
      this->samples_
  ) {
    ESP_LOGW(
        TAG,
        "LUT sample count mismatch"
    );

    return false;
  }

  if (
      stored.settle_ms !=
      this->settle_ms_
  ) {
    ESP_LOGW(
        TAG,
        "LUT settle time mismatch"
    );

    return false;
  }

  /*
   * Verify checksum.
   */
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
        "LUT checksum invalid"
    );

    return false;
  }

  /*
   * Verify monotonicity.
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
