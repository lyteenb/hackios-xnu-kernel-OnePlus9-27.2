/* OnePlus 9's BQ28Z610 pack, over the resident Qualcomm I2CDxe. */
#define BQ28Z610_DEVICE_TYPE 0xffa5u
#define BQ28Z610_FULL        (1u << 5)
#define BQ28Z610_DISCHARGING (1u << 6)
#define BQ28Z610_CHARGE_ALARM ((1u << 12) | (1u << 14) | (1u << 15))

/* The I2CDxe revision 1.0 layout, used by the earlier OnePlus 9 port. */
typedef struct {
	uint32_t frequency_khz, address, mode, max_clock_stretch_us, core_config1, core_config2;
} bq_i2c_config;

typedef struct {
	uint64_t revision;
	uint32_t (EFIAPI *open)(uint32_t instance, void **handle);
	uint32_t (EFIAPI *read)(void *handle, const bq_i2c_config *config, uint16_t offset,
	    uint16_t offset_length, uint8_t *buffer, uint16_t length, uint32_t *read, uint32_t timeout_ms);
	uint32_t (EFIAPI *write)(void *handle, const bq_i2c_config *config, uint16_t offset,
	    uint16_t offset_length, const uint8_t *buffer, uint16_t length, uint32_t *written, uint32_t timeout_ms);
	void *transfer;
	uint32_t (EFIAPI *close)(void *handle);
} bq_i2c;

typedef struct {
	uint64_t revision;
	efi_status (EFIAPI *get_charger_presence)(uint8_t *present);
} bq_charger_ex;

static const bq_i2c_config bq_config = { .frequency_khz = 100, .address = 0x55, .max_clock_stretch_us = 10000 };
static bq_i2c *bq_bus;
static void *bq_gauge;
static bq_charger_ex *bq_external;

static int
bq_read_word(uint8_t reg, uint16_t *value)
{
	uint8_t bytes[2] = {0};
	uint32_t count = 0;

	if (bq_bus->read(bq_gauge, &bq_config, reg, 1, bytes, 2, &count, 50) != 0 || count != 2) {
		return 0;
	}
	*value = (uint16_t)bytes[0] | (uint16_t)bytes[1] << 8;
	return 1;
}

static void
bq28z610_stop(void)
{
	if (bq_gauge != NULL) {
		bq_bus->close(bq_gauge);
		bq_gauge = NULL;
	}
}

static int
bq28z610_start(void)
{
	static const efi_guid i2c_guid = { 0xb27ae8b1, 0x3e10, 0x4d07, { 0xab, 0x5c, 0xeb, 0x9a, 0x6d, 0xc6, 0xfa, 0x8f } };
	static const efi_guid charger_guid = QCOM_CHARGER_EX_GUID;
	static const uint8_t device_type[2] = { 1, 0 };
	uint16_t model = 0;
	uint32_t result, count;

	if (EFI_ERROR(BS->locate_protocol(&i2c_guid, NULL, (void **)&bq_bus)) || bq_bus == NULL ||
	    bq_bus->revision != 0x10000 || bq_bus->open == NULL || bq_bus->read == NULL ||
	    bq_bus->write == NULL || bq_bus->close == NULL) {
		print("battery: BQ28Z610 I2CDxe unavailable\n");
		return 0;
	}

	if (bq_bus->open(3, &bq_gauge) != 0 || bq_gauge == NULL) {
		bq28z610_stop();
		print("battery: BQ28Z610 I2CDxe instance 3 unavailable\n");
		return 0;
	}

	/* Query Control(DEVICE_TYPE) before selecting the telemetry registers. */
	for (unsigned attempt = 0; ; attempt++) {
		count = 0;
		result = bq_bus->write(bq_gauge, &bq_config, 0x00, 1, device_type, sizeof(device_type), &count, 50);
		if (result != 22 || attempt == 2) { /* I2C_ERROR_ADDR_NACK */
			break;
		}
		BS->stall(20000);
	}
	if (result == 0 && count == sizeof(device_type)) {
		BS->stall(1000);
		bq_read_word(0x00, &model);
	}
	if (model != BQ28Z610_DEVICE_TYPE ||
	    EFI_ERROR(BS->locate_protocol(&charger_guid, NULL, (void **)&bq_external)) ||
	    bq_external == NULL || (bq_external->revision != 0x10003 && bq_external->revision != 0x10004) ||
	    bq_external->get_charger_presence == NULL) {
		print("battery: BQ28Z610 probe stopped at model %x or no supported ChargerEx\n", (uint64_t)model);
		bq28z610_stop();
		return 0;
	}
	print("battery: BQ28Z610 model %x on I2CDxe instance 3; ChargerEx %x\n",
	    (uint64_t)model, bq_external->revision);
	return 1;
}

static void
bq28z610_poll(void)
{
	uint16_t soc, mv, temperature, current, status;
	uint8_t external = 0;
	uint32_t flags = 0;
	const uint64_t cache_start = (uint64_t)(uintptr_t)&charger_shared;
	const uint64_t cache_end = cache_start + sizeof(charger_shared);
	int valid = bq_read_word(0x2c, &soc) && soc <= 100 &&
	    bq_read_word(0x08, &mv) && mv >= 4500 && mv <= 10000 &&
	    bq_read_word(0x06, &temperature) && temperature >= 2331 && temperature <= 3731 &&
	    bq_read_word(0x14, &current) && bq_read_word(0x0a, &status) && (status & 0xf) == 0 &&
	    !EFI_ERROR(bq_external->get_charger_presence(&external)) && external <= 1;

	charger_shared.sequence++;
	clean_dcache(cache_start, cache_end);
	if (valid) {
		flags = LOADER_BATTERY_VALID | LOADER_BATTERY_PRESENT | LOADER_BATTERY_VOLTAGE_VALID |
		    LOADER_BATTERY_DIRECT_GAUGE;
		if (external) {
			flags |= LOADER_BATTERY_CHARGER_PRESENT;
			if (status & BQ28Z610_FULL) {
				flags |= LOADER_BATTERY_FULL;
			} else if (!(status & (BQ28Z610_DISCHARGING | BQ28Z610_CHARGE_ALARM))) {
				flags |= LOADER_BATTERY_CHARGING;
			}
		}
		charger_shared.soc_percent = soc;
		charger_shared.voltage_mv = mv;
		/* The shared cache uses positive current out of the battery. */
		charger_shared.current_ma = -(int32_t)(int16_t)current;
		charger_shared.temperature_c = ((int32_t)temperature * 10 - 27315) / 100;
	} else {
		charger_shared.failures++;
	}
	charger_shared.flags = flags;
	charger_shared.polls++;
	clean_dcache(cache_start, cache_end);
	charger_shared.sequence++;
	clean_dcache(cache_start, cache_end);
	if (charger_shared.polls == 1 || !valid) {
		print("battery: BQ28Z610 sample valid=%d SOC=%d%% pack=%d mV external=%d flags=%x\n",
		    (uint64_t)valid, (uint64_t)(int64_t)charger_shared.soc_percent,
		    (uint64_t)(int64_t)charger_shared.voltage_mv, (uint64_t)external, (uint64_t)flags);
	}
}
