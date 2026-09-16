import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import sensor

from esphome.const import (
    CONF_ID,
)

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
CONF_SENSOR = "sensor"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID():
        cv.declare_id(ESP32ADCComponent),

    cv.Required(CONF_ADC_PIN):
        cv.int_range(min=0, max=39),

    cv.Required(CONF_DAC_PIN):
        cv.int_range(min=0, max=39),

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

    cv.Required(CONF_SENSOR):
        sensor.sensor_schema(
            accuracy_decimals=2,
        ),
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

    sens = await sensor.new_sensor(
        config[CONF_SENSOR]
    )

    cg.add(
        var.set_sensor(sens)
    )

    cg.add(
        var.set_adc_pin(
            config[CONF_ADC_PIN]
        )
    )

    cg.add(
        var.set_dac_pin(
            config[CONF_DAC_PIN]
        )
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
