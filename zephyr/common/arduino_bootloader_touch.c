/* Enter the Nano 33 BLE Sense Arduino bootloader on a 1200-baud USB touch.
 *
 * Arduino's native USB firmware uses a host request for 1200 baud as an
 * upload signal. The bootloader then checks nRF52840 GPREGRET after a software
 * reset. This adapter gives the course's Zephyr USB-console builds the same
 * behavior. It is compiled only for build-usb; double-tap RESET remains the
 * recovery path if the application or its USB console is unavailable.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <nrfx.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

#define ARDUINO_UPLOAD_BAUD 1200U
#define ARDUINO_BOOTLOADER_MAGIC 0xB0U

static void monitor_upload_request(void *a, void *b, void *c)
{
	const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	uint32_t baud;

	(void)a;
	(void)b;
	(void)c;
	while (!device_is_ready(console)) {
		k_msleep(100);
	}

	for (;;) {
		/* The Zephyr USB CDC driver retains the host's most recent virtual
		 * baud rate even after the host closes the serial port. No serial
		 * payload or Arduino runtime is involved.
		 */
		if (uart_line_ctrl_get(console, UART_LINE_CTRL_BAUD_RATE, &baud) == 0 &&
		    baud == ARDUINO_UPLOAD_BAUD) {
			/* Give the host time to close its handle before USB disappears. */
			k_msleep(300);
			NRF_POWER->GPREGRET = ARDUINO_BOOTLOADER_MAGIC;
			sys_reboot(SYS_REBOOT_COLD);
		}
		k_msleep(40);
	}
}

K_THREAD_DEFINE(bootloader_touch_thread, 768, monitor_upload_request,
		NULL, NULL, NULL, 10, 0, 0);
