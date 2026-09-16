import esphome.codegen as cg
import esphome.config_validation as cv

from esphome import pins
from esphome.const import CONF_ID

esp32_adc_calibrate_ns = cg.esphome_ns.namespace(
    "esp32_adc_calibrate"
)

ESP32ADCComponent = esp32_adc_calibrate_ns.class_(
    "ESP32ADCComponent",
    cg.PollingComponent,
)

CONF_ADC_PIN = "adc_pin"
CONF_DAC_PIN = "dac_pin"
CONF_SAMPLES = "samples"
CONF_SETTLE_MS = "settle_ms"
CONF_RESTORE_FROM_FLASH = "restore_from_flash"
CONF_CALIBRATE_ON_FIRST_BOOT = "calibrate_on_first_boot"


def _normalize_gpio_pin(value):
    """
    Accept GPIO pin notation such as GPIO32 / GPIO25.

    ESPHome's pin schemas expect a pin configuration. Some ESPHome
    versions do not accept the GPIOxx shorthand directly through
    internal_gpio_*_pin_schema, so normalize GPIOxx to:

        { "number": xx }

    before passing the value to ESPHome's normal pin validation.
    """

    if isinstance(value, str):
        value = value.strip()

        if value.upper().startswith("GPIO"):
            number = value[4:].strip()

            if not number:
                raise cv.Invalid(
                    f"Invalid GPIO pin '{value}': missing GPIO number"
                )

            return {
                "number": cv.int_(number),
            }

    if isinstance(value, dict):
        value = value.copy()

        if "number" in value and isinstance(value["number"], str):
            number = value["number"].strip()

            if number.upper().startswith("GPIO"):
                number = number[4:].strip()

                if not number:
                    raise cv.Invalid(
                        "Invalid GPIO pin: missing GPIO number"
                    )

                value["number"] = cv.int_(number)

        return value

    return value


GPIO_INPUT_PIN_SCHEMA = cv.All(
    _normalize_gpio_pin,
    pins.internal_gpio_input_pin_schema,
)

GPIO_OUTPUT_PIN_SCHEMA = cv.All(
    _normalize_gpio_pin,
    pins.internal_gpio_output_pin_schema,
)


CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID():
        cv.declare_id(ESP32ADCComponent),

    cv.Required(CONF_ADC_PIN):
        GPIO_INPUT_PIN_SCHEMA,

    cv.Required(CONF_DAC_PIN):
        GPIO_OUTPUT_PIN_SCHEMA,

    cv.Optional(
        CONF_SAMPLES,
        default=16,
    ):
        cv.int_range(min=1, max=128),

    cv.Optional(
        CONF_SETTLE_MS,
        default=5,
    ):
        cv.int_range(min=1, max=100),

    cv.Optional(
        CONF_RESTORE_FROM_FLASH,
        default=True,
    ):
        cv.boolean,

    cv.Optional(
        CONF_CALIBRATE_ON_FIRST_BOOT,
        default=True,
    ):
        cv.boolean,

}).extend(
    cv.polling_component_schema("1s")
)


async def to_code(config):
    var = cg.new_Pvariable(
        config[CONF_ID]
    )

    await cg.register_component(
        var,
        config,
    )

    adc_pin = await cg.gpio_pin_expression(
        config[CONF_ADC_PIN]
    )

    dac_pin = await cg.gpio_pin_expression(
        config[CONF_DAC_PIN]
    )

    cg.add(
        var.set_adc_pin(adc_pin)
    )

    cg.add(
        var.set_dac_pin(dac_pin)
    )

    cg.add(
        var.set_samples(
            config[CONF_SAMPLES]
        )
    )

    cg.add(
        var.set_settle_ms(
            config[CONF_SETTLE_MS]
        )
    )

    cg.add(
        var.set_restore_from_flash(
            config[CONF_RESTORE_FROM_FLASH]
        )
    )

    cg.add(
        var.set_calibrate_on_first_boot(
            config[CONF_CALIBRATE_ON_FIRST_BOOT]
        )
    )
