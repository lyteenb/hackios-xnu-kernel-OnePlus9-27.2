// SPDX-License-Identifier: APSL-2.0
/*
 * OnePlus 9 (LE2110): Samsung S6SY792 on the QUPv3 SE4 I2C bus, address 0x48.
 *
 * This is the 16.5 bring-up's read-only protocol moved to the Darwin 27 board model. The
 * controller's wiring is the board's touch-i2c { }: SDA/SCL, interrupt, reset, analog enable,
 * supplies and coordinates. The bus is driven by hand with board/i2c_gpio.h.
 *
 * touchscreen=-1 or 2  only look: print the pins and read the identity if the bus answers
 * touchscreen=1       power, reset, identify, enable scanning and post touches
 *
 * No firmware is uploaded. The only writes to the controller are the volatile scan/event
 * commands 0x62 and 0x10 after the identity and firmware match this phone's baseline.
 */
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/board/input.h>
#include <sys/systm.h>

#include "kernel.h"
#include "board.h"
#include "log.h"

static vm_offset_t tlmm, rsc, cmd_db;
static bool leaving, busy, started, powered, bus_taken;
static int32_t mode;
static unsigned errors, reports, presses, releases, bad, active;
static uint32_t last_x[10], last_y[10];

HAKIOS_DEFINE_SAY(!__atomic_load_n(&leaving, __ATOMIC_SEQ_CST) && ml_get_interrupts_enabled())

#include "mmio.h"
#include "time.h"

static void
s6sy792_wait_us(uint32_t us)
{
	if (__atomic_load_n(&leaving, __ATOMIC_SEQ_CST)) {
		spin_us(us);
	} else {
		sleep_us(us);
	}
}

#define wait_ms(ms) hakios_wait_ms((ms), __atomic_load_n(&leaving, __ATOMIC_SEQ_CST))

#define sleep_us s6sy792_wait_us
#include "rpmh.h"
#undef sleep_us
#include "cmd_db.h"

#define HAKIOS_TLMM tlmm
#include "tlmm.h"
#include "i2c_gpio.h"

#define WIRED (&hakios_board()->touch_i2c)
#define PIN_SDA ((unsigned)WIRED->sda)
#define PIN_SCL ((unsigned)WIRED->scl)
#define PIN_INT ((unsigned)WIRED->interrupt)
#define PIN_RESET ((unsigned)WIRED->reset)
#define PIN_AVDD ((unsigned)WIRED->analog_enable)
#define HAS_AVDD (WIRED->analog_enable >= 0)
#define ADDRESS (WIRED->address)
#define MAX_X (WIRED->max_x)
#define MAX_Y (WIRED->max_y)
#define CONTACTS (WIRED->contacts)

#define VRM_VOLTAGE 0
#define VRM_ENABLE 4
#define VRM_MODE 8
#define VRM_MODE_LDO_HPM 7
#define IO_MILLIVOLTS 1800

static struct i2c_gpio i2c_bus;
static uint32_t io_supply_at, analog_supply_at;

static uint32_t
rpmh_address(const char *name)
{
	const struct cmd_db_header *db = (const struct cmd_db_header *)cmd_db;

	if (db == NULL || rsc == 0) {
		return 0;
	}
	if (!hakios_cmd_db_valid(db)) {
		return 0;
	}
	return hakios_cmd_db_address(db, hakios_board()->rpmh.cmd_db_size, name);
}

static bool
rpmh_write(const uint32_t *address, const uint32_t *data, unsigned count)
{
	bool locking = !__atomic_load_n(&leaving, __ATOMIC_SEQ_CST);
	const char *problem;

	if (locking) {
		hakios_rpmh_lock();
	}
	problem = hakios_rpmh_send_rsc(rsc, hakios_board()->rpmh.tcs_offset, address, data, count);
	if (locking) {
		hakios_rpmh_unlock();
	}
	if (problem != NULL) {
		say("s6sy792: RPMh: %s\n", problem);
	}
	return problem == NULL;
}

static bool
supply_on(const char *name, uint32_t millivolts, uint32_t *asked)
{
	uint32_t base = rpmh_address(name);
	uint32_t address[3] = { base + VRM_VOLTAGE, base + VRM_ENABLE, base + VRM_MODE };
	uint32_t data[3] = { millivolts, 1, VRM_MODE_LDO_HPM };

	if (base == 0 || (base >> 16 & 0xf) != 4) {
		say("s6sy792: no RPMh regulator %s (address 0x%x)\n", name, base);
		return false;
	}
	say("s6sy792: regulator %s at 0x%x: %u mV\n", name, base, millivolts);
	*asked = base;
	return rpmh_write(address, data, 3);
}

static void
supply_off(const char *name, uint32_t *asked)
{
	uint32_t address, off = 0;

	if (*asked == 0) {
		return;
	}
	address = *asked + VRM_ENABLE;
	*asked = 0;
	say("s6sy792: regulator %s: vote withdrawn\n", name);
	(void)rpmh_write(&address, &off, 1);
}

#define KEPT_PINS 5
static unsigned kept_pins;
static unsigned kept_number[KEPT_PINS];
static uint32_t kept_cfg[KEPT_PINS], kept_io[KEPT_PINS];

static void
pins_record(void)
{
	kept_pins = 0;
	kept_number[kept_pins++] = PIN_SDA;
	kept_number[kept_pins++] = PIN_SCL;
	kept_number[kept_pins++] = PIN_INT;
	kept_number[kept_pins++] = PIN_RESET;
	if (HAS_AVDD) {
		kept_number[kept_pins++] = PIN_AVDD;
	}
	for (unsigned i = 0; i < kept_pins; i++) {
		kept_cfg[i] = rd(GPIO_CFG(kept_number[i]));
		kept_io[i] = rd(GPIO_IO(kept_number[i]));
	}
	__atomic_store_n(&powered, true, __ATOMIC_SEQ_CST);
}

static void
pins_restore(void)
{
	for (unsigned i = 0; i < kept_pins; i++) {
		wr(GPIO_IO(kept_number[i]), kept_io[i] & 2);
		wr(GPIO_CFG(kept_number[i]), kept_cfg[i]);
	}
}

static void
show_pin(unsigned pin, const char *what)
{
	uint32_t cfg = rd(GPIO_CFG(pin)), io = rd(GPIO_IO(pin));

	say("s6sy792: gpio%u (%s): function %u, pull %u, %u mA, %s, reads %u\n", pin, what, (cfg >> 2) & 15,
	    cfg & 3, (((cfg >> 6) & 7) + 1) * 2, (cfg & CFG_OUTPUT) ? (io & 2 ? "drives high" : "drives low") : "input",
	    io & 1);
}

static void
show_pins(void)
{
	say("s6sy792: pins as found\n");
	show_pin(PIN_SDA, "I2C data");
	show_pin(PIN_SCL, "I2C clock");
	show_pin(PIN_INT, "interrupt");
	show_pin(PIN_RESET, "reset");
	if (HAS_AVDD) {
		show_pin(PIN_AVDD, "analog supply switch");
	}
}

static bool
chip_read(uint8_t reg, uint8_t *bytes, unsigned count)
{
	bool ok;

	i2c_gpio_start(&i2c_bus);
	ok = i2c_gpio_write(&i2c_bus, (uint8_t)(ADDRESS << 1));
	if (ok) {
		ok = i2c_gpio_write(&i2c_bus, reg);
	}
	if (ok) {
		i2c_gpio_start(&i2c_bus);
		ok = i2c_gpio_write(&i2c_bus, (uint8_t)((ADDRESS << 1) | 1));
	}
	for (unsigned i = 0; ok && i < count; i++) {
		bytes[i] = i2c_gpio_read(&i2c_bus, i + 1 < count);
	}
	i2c_gpio_stop(&i2c_bus);
	if (!ok) {
		if (errors < 8) {
			say("s6sy792: read %#x failed; SDA=%u SCL=%u\n", reg, i2c_gpio_level(PIN_SDA),
			    i2c_gpio_level(PIN_SCL));
		}
		errors++;
	}
	return ok;
}

static bool
chip_command(uint8_t reg)
{
	bool ok;

	if (reg != 0x10 && reg != 0x11 && reg != 0x62) {
		return false;
	}
	i2c_gpio_start(&i2c_bus);
	ok = i2c_gpio_write(&i2c_bus, (uint8_t)(ADDRESS << 1));
	if (ok) {
		ok = i2c_gpio_write(&i2c_bus, reg);
	}
	i2c_gpio_stop(&i2c_bus);
	if (!ok) {
		errors++;
	}
	return ok;
}

static bool
power_on(void)
{
	pins_record();
	gpio_output(PIN_RESET, false, PULL_NONE, 2);
	gpio_configure(PIN_INT, 0, PULL_NONE, 2, false);
	if (HAS_AVDD) {
		gpio_output(PIN_AVDD, false, PULL_NONE, 2);
	}
	/* Power the controller and release reset before asking the bus to be idle:
	 * an unpowered S6SY792 can hold SDA/SCL low.  (The 16.5 driver powered
	 * first; taking the bus first made a second boot refuse it.) */
	if (WIRED->io_supply != NULL && !supply_on(WIRED->io_supply, IO_MILLIVOLTS, &io_supply_at)) {
		return false;
	}
	if (WIRED->analog_supply != NULL &&
	    !supply_on(WIRED->analog_supply, WIRED->analog_mv, &analog_supply_at)) {
		return false;
	}
	if (HAS_AVDD) {
		gpio_output(PIN_AVDD, true, PULL_NONE, 2);
	}
	wait_ms(5);
	gpio_set(PIN_RESET, true);
	wait_ms(100);
	if (!i2c_gpio_take(&i2c_bus)) {
		unsigned clocks = 0;

		if (i2c_gpio_unwedge(&i2c_bus, &clocks)) {
			say("s6sy792: I2C bus recovered after %u clocks\n", clocks);
		} else {
			say("s6sy792: I2C bus is not at rest after power-up\n");
			return false;
		}
	}
	bus_taken = true;
	say("s6sy792: bus SDA=%u SCL=%u IRQ=%u reset=%u\n", i2c_gpio_level(PIN_SDA), i2c_gpio_level(PIN_SCL),
	    gpio_get(PIN_INT), gpio_get(PIN_RESET));
	return true;
}

static void
power_off(void)
{
	if (!__atomic_load_n(&powered, __ATOMIC_SEQ_CST)) {
		return;
	}
	gpio_output(PIN_RESET, false, PULL_NONE, 2);
	if (WIRED->analog_supply != NULL) {
		supply_off(WIRED->analog_supply, &analog_supply_at);
	}
	if (WIRED->io_supply != NULL) {
		supply_off(WIRED->io_supply, &io_supply_at);
	}
	if (HAS_AVDD) {
		gpio_output(PIN_AVDD, false, PULL_NONE, 2);
	}
	if (bus_taken) {
		i2c_gpio_stop(&i2c_bus);
		i2c_gpio_give_back(&i2c_bus);
		bus_taken = false;
	}
	pins_restore();
	__atomic_store_n(&powered, false, __ATOMIC_SEQ_CST);
	say("s6sy792: powered off and pins restored\n");
}

static void
release_all(void)
{
	for (unsigned i = 0; i < CONTACTS; i++) {
		if (active & (1u << i)) {
			hakios_touch_post(HAKIOS_TOUCH_UP, i, last_x[i], last_y[i]);
			releases++;
		}
	}
	active = 0;
}

static void
event_received(const uint8_t event[8])
{
	unsigned id, action, x, y, finger, mask;

	if (event[0] == 0) {
		return;
	}
	reports++;
	if ((event[0] & 3) == 1) {
		unsigned type = (event[0] >> 2) & 15;

		if ((type == 1 && (event[1] == 1 || event[1] == 2)) ||
		    (type == 2 && event[1] == 0 && event[2] == 0x20)) {
			say("s6sy792: controller status %02x %02x %02x; release contacts\n", event[0], event[1], event[2]);
			release_all();
			(void)chip_command(0x62);
			(void)chip_command(0x10);
		}
		return;
	}
	if ((event[0] & 3) != 0) {
		return;
	}
	id = (event[0] >> 2) & 15;
	action = event[0] >> 6;
	x = ((unsigned)event[1] << 4) | (event[3] >> 4);
	y = ((unsigned)event[2] << 4) | (event[3] & 15);
	if (id < 1 || id > CONTACTS || action < 1 || x >= MAX_X || y >= MAX_Y) {
		bad++;
		return;
	}
	finger = id - 1;
	mask = 1u << finger;
	x = (x * 65535u + (MAX_X - 1) / 2) / (MAX_X - 1);
	y = (y * 65535u + (MAX_Y - 1) / 2) / (MAX_Y - 1);
	if (action == 3) {
		if (active & mask) {
			hakios_touch_post(HAKIOS_TOUCH_UP, finger, last_x[finger], last_y[finger]);
			active &= ~mask;
			releases++;
		}
	} else {
		uint32_t type = (active & mask) ? HAKIOS_TOUCH_MOVE : HAKIOS_TOUCH_DOWN;

		last_x[finger] = x;
		last_y[finger] = y;
		if (!(active & mask)) {
			presses++;
		}
		active |= mask;
		hakios_touch_post(type, finger, x, y);
	}
}

static bool
start_chip(void)
{
	uint8_t id[3] = { 0 }, boot = 0, integrity = 0, version[4] = { 0 };
	bool identified, boot_ok, version_ok, integrity_ok;
	static const uint8_t expected[] = { 0x28, 0x24, 0x01, 0x21 };

	if (!power_on()) {
		return false;
	}
	identified = chip_read(0x22, id, sizeof(id));
	say("s6sy792: device ID read %u: %02x %02x %02x\n", identified, id[0], id[1], id[2]);
	wait_ms(20);
	boot_ok = identified && chip_read(0x55, &boot, 1);
	say("s6sy792: boot status read %u: %02x\n", boot_ok, boot);
	wait_ms(20);
	version_ok = boot_ok && chip_read(0xa5, version, sizeof(version));
	say("s6sy792: firmware read %u: %02x%02x%02x%02x\n", version_ok, version[0], version[1], version[2], version[3]);
	wait_ms(20);
	integrity_ok = version_ok && chip_read(0x21, &integrity, 1);
	if (!identified || !boot_ok || !version_ok || !integrity_ok) {
		say("s6sy792: identification failed; scan not enabled\n");
		return false;
	}
	say("s6sy792: ID %02x %02x %02x boot %02x integrity %02x firmware %02x%02x%02x%02x\n", id[0], id[1], id[2],
	    boot, integrity, version[0], version[1], version[2], version[3]);
	if (boot != 0x20 || !(integrity & 0x80) || memcmp(version, expected, sizeof(expected))) {
		say("s6sy792: identification differs from this device's baseline; no scan command\n");
		return false;
	}
	/* The controller stretches the clock while it prepares its scan engine. */
	bool scan = chip_command(0x62);
	say("s6sy792: scan command 0x62: %u\n", scan);
	wait_ms(20);
	if (!scan) {
		scan = chip_command(0x62);
		say("s6sy792: scan command 0x62 retry: %u\n", scan);
		wait_ms(20);
	}
	bool report = scan && chip_command(0x10);
	say("s6sy792: report command 0x10: %u\n", report);
	if (!report) {
		wait_ms(20);
		report = chip_command(0x10);
		say("s6sy792: report command 0x10 retry: %u\n", report);
	}
	return scan && report;
}

static void
poll_chip(void)
{
	unsigned failures = 0;

	say("s6sy792: physical touch ready, %u x %u, %u contacts; polling IRQ every 4 ms\n", MAX_X, MAX_Y, CONTACTS);
	while (!__atomic_load_n(&leaving, __ATOMIC_SEQ_CST)) {
		if (!gpio_get(PIN_INT)) {
			uint8_t events[32 * 8] = { 0 };
			bool ok;

			__atomic_store_n(&busy, true, __ATOMIC_SEQ_CST);
			ok = chip_read(0x60, events, 8);
			if (!ok) {
				__atomic_store_n(&busy, false, __ATOMIC_SEQ_CST);
				if (++failures >= 4) {
					release_all();
					break;
				}
			} else {
				unsigned remaining = events[7] & 63;

				failures = 0;
				if (remaining > 31) {
					bad++;
					release_all();
					(void)chip_command(0x62);
				} else if (remaining != 0 && !chip_read(0x61, events + 8, remaining * 8)) {
					release_all();
					(void)chip_command(0x62);
				} else {
					for (unsigned i = 0; i <= remaining; i++) {
						event_received(events + 8 * i);
					}
				}
				__atomic_store_n(&busy, false, __ATOMIC_SEQ_CST);
			}
		}
		wait_ms(4);
	}
	release_all();
}

static void
s6sy792_thread(__unused void *parameter, __unused wait_result_t result)
{
	const struct hakios_board *b = hakios_board();

	if (__atomic_load_n(&leaving, __ATOMIC_SEQ_CST)) {
		thread_terminate(current_thread());
		return;
	}
	tlmm = ml_io_map(b->pins.tlmm, TLMM_SIZE);
	if (tlmm == 0) {
		say("s6sy792: pins could not be mapped\n");
		thread_terminate(current_thread());
		return;
	}
	i2c_bus.sda = PIN_SDA;
	i2c_bus.scl = PIN_SCL;
	show_pins();
	if (mode < 0 || mode == 2) {
		if (i2c_gpio_take(&i2c_bus)) {
			uint8_t id[3] = { 0 };
			bool ok;

			bus_taken = true;
			__atomic_store_n(&busy, true, __ATOMIC_SEQ_CST);
			ok = chip_read(0x22, id, sizeof(id));
			__atomic_store_n(&busy, false, __ATOMIC_SEQ_CST);
			say("s6sy792: inspection ID read %u: %02x %02x %02x\n", ok, id[0], id[1], id[2]);
			i2c_gpio_give_back(&i2c_bus);
			bus_taken = false;
		} else {
			say("s6sy792: inspection: I2C bus is not at rest\n");
		}
		thread_terminate(current_thread());
		return;
	}
	if (WIRED->io_supply != NULL || WIRED->analog_supply != NULL) {
		rsc = ml_io_map(b->rpmh.rsc, 0x10000);
		cmd_db = ml_io_map_wcomb(b->rpmh.cmd_db, b->rpmh.cmd_db_size);
		if (rsc == 0 || cmd_db == 0) {
			say("s6sy792: RPMh or its command database could not be mapped\n");
			thread_terminate(current_thread());
			return;
		}
	}
	__atomic_store_n(&busy, true, __ATOMIC_SEQ_CST);
	if (!start_chip()) {
		__atomic_store_n(&busy, false, __ATOMIC_SEQ_CST);
		power_off();
		thread_terminate(current_thread());
		return;
	}
	__atomic_store_n(&busy, false, __ATOMIC_SEQ_CST);
	poll_chip();
	power_off();
	thread_terminate(current_thread());
}

void
hakios_s6sy792_init(void)
{
	const struct hakios_board *b = hakios_board();
	thread_t thread;

	if (!PE_parse_boot_argn("touchscreen", &mode, sizeof(mode)) || mode == 0) {
		return;
	}
	if (b == NULL || b->pins.tlmm == 0 || !hakios_board_has(HAKIOS_HAS_TOUCH_S6SY792)) {
		return;
	}
	if (b->touch_i2c.sda == b->touch_i2c.scl || b->touch_i2c.address == 0 ||
	    b->touch_i2c.interrupt == b->touch_i2c.reset) {
		say("s6sy792: disabled: board does not say how the controller is wired (touch-i2c { })\n");
		return;
	}
	if ((b->touch_i2c.io_supply != NULL || b->touch_i2c.analog_supply != NULL) &&
	    (b->rpmh.cmd_db == 0 || b->rpmh.rsc == 0)) {
		say("s6sy792: disabled: touch-i2c { } names a supply, and the SoC file does not say where RPMh is\n");
		return;
	}
	if (b->touch_i2c.analog_supply != NULL && b->touch_i2c.analog_mv == 0) {
		say("s6sy792: disabled: analog supply has no voltage\n");
		return;
	}
	if (b->touch_i2c.contacts == 0 || b->touch_i2c.contacts > 10 || b->touch_i2c.max_x == 0 ||
	    b->touch_i2c.max_y == 0) {
		say("s6sy792: disabled: contacts or coordinates are not usable\n");
		return;
	}
	__atomic_store_n(&started, true, __ATOMIC_SEQ_CST);
	if (kernel_thread_start(s6sy792_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	} else {
		__atomic_store_n(&started, false, __ATOMIC_SEQ_CST);
		say("s6sy792: cannot start the touch screen thread\n");
	}
}

void
hakios_s6sy792_quiesce(void)
{
	if (!__atomic_load_n(&started, __ATOMIC_SEQ_CST)) {
		return;
	}
	__atomic_store_n(&leaving, true, __ATOMIC_SEQ_CST);
	for (unsigned i = 0; i < 100 && __atomic_load_n(&busy, __ATOMIC_SEQ_CST); i++) {
		spin_us(1000);
	}
	if (bus_taken) {
		i2c_gpio_stop(&i2c_bus);
		i2c_gpio_give_back(&i2c_bus);
		bus_taken = false;
	}
	power_off();
	__atomic_store_n(&started, false, __ATOMIC_SEQ_CST);
}
