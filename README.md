# esphome-esp32-adc-calibrate

ESPHome external component for calibrating the ADC of a classic ESP32
using its internal 8-bit DAC.

The component creates a lookup table containing 256 calibration points:

    DAC 8-bit
    0..255
       │
       ▼
    ESP32 DAC
       │
       ▼
    analog signal
       │
       ▼
    ESP32 ADC
    0..4095
       │
       ▼
    LUT[256]

The LUT is stored in ESPHome persistent storage.

On subsequent boots, the LUT is restored from flash instead of being
generated again.

## Hardware

This component targets the classic ESP32 with an internal DAC.

Default example:

    GPIO25 (DAC1) ───────── GPIO35 (ADC1)

The DAC output must be connected to the ADC input used for calibration.

The ADC pin must be suitable for ADC input.

## Resolution

The ESP32 DAC is 8-bit:

    0..255

The ESP32 ADC is 12-bit:

    0..4095

The LUT therefore contains 256 ADC measurements, one for every DAC code:

    LUT[0]   = ADC measured with DAC = 0
    LUT[1]   = ADC measured with DAC = 1
    ...
    LUT[255] = ADC measured with DAC = 255

The ADC-to-DAC conversion uses linear interpolation.

For example:

    DAC 127 -> ADC 2028
    DAC 128 -> ADC 2044

For:

    ADC = 2036

the result is:

    DAC = 127 + (2036 - 2028) / (2044 - 2028)
        = 127.5

The sensor therefore reports:

    127.50

The sensor does NOT report volts and does NOT report the raw ADC value.

## Flash storage

The LUT is stored using ESPHome's persistent preferences.

The stored structure contains:

- magic number
- LUT version
- number of ADC samples
- DAC settle time
- 256 ADC values
- checksum

The LUT is rejected if its metadata or checksum is invalid.

The LUT is also checked for monotonicity.

## First boot

By default:

    restore_from_flash: true
    calibrate_on_first_boot: true

On the first boot:

1. DAC code 0 is output.
2. ADC is measured.
3. DAC code 1 is output.
4. ADC is measured.
5. This continues through DAC code 255.
6. The 256-point LUT is stored in flash.

On later boots, the LUT is restored.

## Recalibration

The package exposes a Home Assistant button:

    Recalibrer ADC

Pressing this button generates a new 256-point LUT.

The new LUT replaces the previous LUT in persistent storage.

## Configuration

The GPIOs are configurable through ESPHome substitutions.

Example:

    substitutions:
      adc_cal_pin: "35"
      adc_cal_dac_pin: "25"

      adc_cal_samples: "16"
      adc_cal_settle_ms: "5"

Then include the package:

    packages:
      adc_calibration:
        url: https://github.com/TON_COMPTE/esphome-esp32-adc-calibrate
        files:
          - path: package.yaml
        ref: main
        refresh: 1d

## Calibration parameters

### samples

Number of ADC measurements averaged for every DAC code.

Default:

    16

Increasing this value reduces measurement noise but increases
calibration time.

### settle_ms

Delay after changing the DAC value before measuring the ADC.

Default:

    5 ms

Increase this value if the external analog circuit requires more
settling time.

## Calibration duration

There are 256 DAC values.

With:

    samples: 16
    settle_ms: 5

the calibration requires 4096 ADC conversions plus the settling
delays.

The exact duration depends on the ESP32 and the application.

## Output

The sensor publishes the equivalent DAC value:

    0.00 .. 255.00

For example:

    127.42

This value is the interpolated DAC code corresponding to the measured
12-bit ADC value.

If an integer DAC code is required:

    round(value)

can be used before calling `dacWrite()`.

## Limitations

This version targets the classic ESP32 with an internal DAC.

The DAC and ADC must be electrically connected for calibration.

The calibration characterizes the complete DAC-to-ADC path present
during calibration. Changing the analog circuitry can invalidate the LUT.

## License

MIT
