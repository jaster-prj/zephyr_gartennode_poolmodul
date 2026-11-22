#include "native_sim_ds18b20.h"

#include <errno.h>

#include <zephyr/sys/util.h>

#if defined(CONFIG_BOARD_NATIVE_SIM)
#include <math.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/w1.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(native_sim_ds18b20, LOG_LEVEL_INF);

#define DS18B20_ROM_COMMAND 0xF0
#define DS18B20_MATCH_ROM 0x55
#define DS18B20_SKIP_ROM 0xCC
#define DS18B20_READ_ROM 0x33
#define DS18B20_CONVERT_T 0x44
#define DS18B20_READ_SCRATCHPAD 0xBE
#define DS18B20_WRITE_SCRATCHPAD 0x4E
#define DS18B20_READ_POWER 0xB4

enum emulator_phase {
	PHASE_ROM_COMMAND,
	PHASE_SEARCH,
	PHASE_MATCH_ROM,
	PHASE_FUNCTION_COMMAND,
	PHASE_WRITE_SCRATCHPAD,
	PHASE_READ_ROM,
	PHASE_READ_SCRATCHPAD,
	PHASE_READ_POWER,
};

struct ds18b20_model {
	enum emulator_phase phase;
	uint8_t rom[8];
	uint8_t scratchpad[9];
	uint8_t input_byte;
	uint8_t input_bit;
	uint8_t match_index;
	uint8_t search_bit;
	uint8_t search_slot;
	uint8_t read_index;
	uint8_t read_bit;
	bool selected;
};

static struct ds18b20_model model = {
	/* 28-000000000001, with a valid Maxim CRC-8. */
	.rom = { 0x28, 0x01, 0, 0, 0, 0, 0, 0x29 },
};
static atomic_t temperature_sixteenths = ATOMIC_INIT(22 * 16);
static const struct device *const uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart_emul));
static const struct device *const w1_dev = DEVICE_DT_GET(DT_NODELABEL(w1_0));

static void count_rom(struct w1_rom rom, void *user_data)
{
	size_t *count = user_data;

	if (rom.family == 0x28 && rom.crc == w1_crc8((const uint8_t *)&rom, 7)) {
		(*count)++;
	}
}

static void model_reset(void)
{
	model.phase = PHASE_ROM_COMMAND;
	model.input_byte = 0;
	model.input_bit = 0;
	model.match_index = 0;
	model.search_bit = 0;
	model.search_slot = 0;
	model.read_index = 0;
	model.read_bit = 0;
	model.selected = true;
}

static void prepare_scratchpad(void)
{
	int16_t raw = (int16_t)atomic_get(&temperature_sixteenths);

	model.scratchpad[0] = (uint8_t)raw;
	model.scratchpad[1] = (uint8_t)((uint16_t)raw >> 8);
	model.scratchpad[2] = 75;
	model.scratchpad[3] = 70;
	model.scratchpad[4] = 0x7F;
	model.scratchpad[5] = 0xFF;
	model.scratchpad[6] = 0x0C;
	model.scratchpad[7] = 0x10;
	model.scratchpad[8] = w1_crc8(model.scratchpad, 8);
}

static void command_received(uint8_t command)
{
	switch (model.phase) {
	case PHASE_ROM_COMMAND:
		switch (command) {
		case DS18B20_ROM_COMMAND:
			model.phase = PHASE_SEARCH;
			model.search_bit = 0;
			model.search_slot = 0;
			break;
		case DS18B20_MATCH_ROM:
			model.phase = PHASE_MATCH_ROM;
			model.match_index = 0;
			break;
		case DS18B20_SKIP_ROM:
			model.selected = true;
			model.phase = PHASE_FUNCTION_COMMAND;
			break;
		case DS18B20_READ_ROM:
			model.phase = PHASE_READ_ROM;
			model.read_index = 0;
			model.read_bit = 0;
			break;
		default:
			model.selected = false;
			break;
		}
		break;
	case PHASE_MATCH_ROM:
		if (command != model.rom[model.match_index++]) {
			model.selected = false;
		}
		if (model.match_index == sizeof(model.rom)) {
			model.phase = model.selected ? PHASE_FUNCTION_COMMAND : PHASE_ROM_COMMAND;
		}
		break;
	case PHASE_FUNCTION_COMMAND:
		switch (command) {
		case DS18B20_CONVERT_T:
			/* Conversion is instantaneous in the virtual sensor. */
			model.phase = PHASE_ROM_COMMAND;
			break;
		case DS18B20_READ_SCRATCHPAD:
			prepare_scratchpad();
			model.phase = PHASE_READ_SCRATCHPAD;
			model.read_index = 0;
			model.read_bit = 0;
			break;
		case DS18B20_WRITE_SCRATCHPAD:
			model.phase = PHASE_WRITE_SCRATCHPAD;
			model.match_index = 0;
			break;
		case DS18B20_READ_POWER:
			model.phase = PHASE_READ_POWER;
			model.read_index = 0;
			model.read_bit = 0;
			break;
		default:
			model.phase = PHASE_ROM_COMMAND;
			break;
		}
		break;
	case PHASE_WRITE_SCRATCHPAD:
		/* Accept TH, TL, and configuration bytes as a real DS18B20 does. */
		model.scratchpad[2 + model.match_index++] = command;
		if (model.match_index == 3) {
			model.phase = PHASE_FUNCTION_COMMAND;
			model.match_index = 0;
		}
		break;
	default:
		break;
	}
}

static uint8_t read_slot_response(void)
{
	uint8_t value;
	uint8_t result;

	/* A device with a non-matching MATCH ROM must not drive the bus. */
	if (!model.selected && model.phase != PHASE_SEARCH) {
		return 0xFF;
	}

	if (model.phase == PHASE_SEARCH) {
		uint8_t rom_bit = (model.rom[model.search_bit / 8] >>
				   (model.search_bit % 8)) & 1;
		result = model.search_slot == 0 ? rom_bit : !rom_bit;
		model.search_slot++;
		/* The third slot is the master's branch choice. */
		return result ? 0xFF : 0x00;
	}

	switch (model.phase) {
	case PHASE_READ_ROM:
		value = model.rom[model.read_index];
		break;
	case PHASE_READ_SCRATCHPAD:
		value = model.scratchpad[model.read_index];
		break;
	case PHASE_READ_POWER:
		value = 0xFF; /* External-powered device. */
		break;
	default:
		return 0xFF;
	}
	result = (value >> model.read_bit) & 1;
	if (++model.read_bit == 8) {
		model.read_bit = 0;
		if (model.phase == PHASE_READ_ROM && ++model.read_index == sizeof(model.rom)) {
			model.phase = PHASE_ROM_COMMAND;
		} else if (model.phase == PHASE_READ_SCRATCHPAD &&
			   ++model.read_index == sizeof(model.scratchpad)) {
			model.phase = PHASE_FUNCTION_COMMAND;
		} else if (model.phase == PHASE_READ_POWER) {
			model.phase = PHASE_FUNCTION_COMMAND;
		}
	}
	return result ? 0xFF : 0x00;
}

static uint8_t exchange_slot(uint8_t tx, uint32_t baudrate)
{
	bool released = tx == 0xFF;

	if (baudrate == 9600 && tx == 0xF0) {
		model_reset();
		return 0xE0; /* Reset echo modified by the presence pulse. */
	}
	/* Ignore function commands after another ROM was selected. */
	if (!model.selected && model.phase != PHASE_MATCH_ROM) {
		return 0xFF;
	}
	if (model.phase == PHASE_SEARCH) {
		if (model.search_slot == 2) {
			uint8_t expected = (model.rom[model.search_bit / 8] >>
					    (model.search_bit % 8)) & 1;

			if ((uint8_t)released != expected) {
				model.selected = false;
				model.phase = PHASE_ROM_COMMAND;
			}
			if (++model.search_bit == 64) {
				model.phase = model.selected ? PHASE_FUNCTION_COMMAND :
					PHASE_ROM_COMMAND;
			}
			model.search_slot = 0;
			return released ? 0xFF : 0x00;
		}
		return read_slot_response();
	}
	if (model.phase == PHASE_READ_ROM || model.phase == PHASE_READ_SCRATCHPAD ||
	    model.phase == PHASE_READ_POWER) {
		return read_slot_response();
	}

	if (released) {
		model.input_byte |= BIT(model.input_bit);
	}
	if (++model.input_bit == 8) {
		uint8_t byte = model.input_byte;

		model.input_byte = 0;
		model.input_bit = 0;
		command_received(byte);
	}
	return 0xFF;
}

static void uart_tx_ready(const struct device *dev, size_t size, void *user_data)
{
	uint8_t tx[64];
	uint8_t rx[64];
	struct uart_config config;
	uint32_t count;

	ARG_UNUSED(user_data);
	if (uart_config_get(dev, &config) != 0) {
		return;
	}
	count = uart_emul_get_tx_data(dev, tx, MIN(size, sizeof(tx)));
	for (uint32_t i = 0; i < count; i++) {
		rx[i] = exchange_slot(tx[i], config.baudrate);
	}
	(void)uart_emul_put_rx_data(dev, rx, count);
}

int native_sim_ds18b20_init(void)
{
	size_t devices_found = 0;
	struct w1_slave_config peripheral = {0};
	uint8_t scratchpad[9];
	uint8_t config[3] = { 75, 70, 0x7F };
	int16_t expected_temperature = (int16_t)atomic_get(&temperature_sixteenths);
	int found;

	if (!device_is_ready(uart_dev)) {
		return -ENODEV;
	}
	model_reset();
	uart_emul_callback_tx_data_ready_set(uart_dev, uart_tx_ready, NULL);
	if (!device_is_ready(w1_dev)) {
		return -ENODEV;
	}
	found = w1_search_rom(w1_dev, count_rom, &devices_found);
	if (found != 1 || devices_found != 1) {
		LOG_ERR("Virtual W1 bus search returned %d device(s), valid DS18B20 count %u",
			found, (unsigned int)devices_found);
		return -EIO;
	}
	memcpy(&peripheral.rom, model.rom, sizeof(model.rom));
	if (w1_reset_select(w1_dev, &peripheral) != 0 ||
	    w1_write_byte(w1_dev, DS18B20_WRITE_SCRATCHPAD) != 0 ||
	    w1_write_block(w1_dev, config, sizeof(config)) != 0 ||
	    w1_reset_select(w1_dev, &peripheral) != 0 ||
	    w1_write_byte(w1_dev, DS18B20_CONVERT_T) != 0 ||
	    w1_reset_select(w1_dev, &peripheral) != 0 ||
	    w1_write_byte(w1_dev, DS18B20_READ_SCRATCHPAD) != 0 ||
	    w1_read_block(w1_dev, scratchpad, sizeof(scratchpad)) != 0 ||
	    scratchpad[8] != w1_crc8(scratchpad, 8) ||
	    (int16_t)((uint16_t)scratchpad[1] << 8 | scratchpad[0]) !=
		    expected_temperature) {
		LOG_ERR("Virtual DS18B20 scratchpad request failed CRC/protocol validation");
		return -EIO;
	}
	LOG_INF("Virtual DS18B20 is responding on W1 (ROM 0x2801000000000029)");
	return 0;
}

int native_sim_ds18b20_set_rom(uint64_t rom_id)
{
	struct w1_rom rom;

	w1_uint64_to_rom(rom_id, &rom);
	if (rom.family != 0x28 || rom.crc != w1_crc8((const uint8_t *)&rom, 7)) {
		return -EINVAL;
	}
	if (w1_lock_bus(w1_dev) != 0) {
		return -EIO;
	}
	if (memcmp(model.rom, &rom, sizeof(model.rom)) == 0) {
		(void)w1_unlock_bus(w1_dev);
		return 0;
	}
	memcpy(model.rom, &rom, sizeof(model.rom));
	model_reset();
	(void)w1_unlock_bus(w1_dev);
	return 0;
}

void native_sim_ds18b20_set_temperature(double temperature_c)
{
	if (!isfinite(temperature_c)) {
		return;
	}
	temperature_c = CLAMP(temperature_c, -55.0, 125.0);
	atomic_set(&temperature_sixteenths, (atomic_val_t)lround(temperature_c * 16.0));
}
#else
int native_sim_ds18b20_init(void)
{
	return -ENOTSUP;
}

void native_sim_ds18b20_set_temperature(double temperature_c)
{
	ARG_UNUSED(temperature_c);
}

int native_sim_ds18b20_set_rom(uint64_t rom_id)
{
	ARG_UNUSED(rom_id);
	return -ENOTSUP;
}

#endif
