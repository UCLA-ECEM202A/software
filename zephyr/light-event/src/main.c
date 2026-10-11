/* Real-sensor light event pipeline for Nano 33 BLE Sense Rev2.
 *
 * Follow the data through sampler() -> window_q -> detector(). The sampler
 * collects complete windows of APDS9960 clear-channel counts (not calibrated
 * lux). The detector compares changes within a window against recent quiet
 * windows. After an event, the two threads coordinate a three-second pause.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define WINDOW_SAMPLES 8
#define SAMPLE_PERIOD_MS 200
#define COOLDOWN_MS 3000
#define BASELINE_WINDOWS 3
/* Require energy > 2.5 times the baseline, subject to an absolute floor. */
#define RISE_PERCENT 150
#define MIN_ENERGY_COUNTS_SQUARED 2500U

/* One queued item contains the actual sensor window, not merely a descriptor. */
struct light_window {
	int64_t first_ms;
	int64_t last_ms;
	uint32_t sequence;
	uint16_t clear[WINDOW_SAMPLES];
};

/* k_msgq_put() copies the whole struct into one of two fixed-size slots. */
K_MSGQ_DEFINE(window_q, sizeof(struct light_window), 2, 4);
/* Protect paused and queue insertion/purge as one decision across threads. */
K_MUTEX_DEFINE(gate_lock);
/* Both statically created threads wait until main() checks the sensor. */
K_SEM_DEFINE(start_gate, 0, 2);
/* A paused sampler sleeps here instead of polling for the cooldown to end. */
K_SEM_DEFINE(resume_sampling, 0, 1);

/* DT_NODELABEL names the APDS9960 in the board's devicetree description. */
static const struct device *const light_sensor = DEVICE_DT_GET(DT_NODELABEL(apds9960));
static bool paused;
static uint32_t dropped_windows;

static bool sampling_paused(void)
{
	bool value;
	k_mutex_lock(&gate_lock, K_FOREVER);
	value = paused;
	k_mutex_unlock(&gate_lock);
	return value;
}

static void wait_until_resumed(void)
{
	/* Recheck paused after waking: another event could have started. */
	while (sampling_paused()) {
		k_sem_take(&resume_sampling, K_FOREVER);
	}
}

/* Mean squared change between adjacent readings, in raw count-squared units.
 * A steady bright room and a steady dark room both have low change energy.
 */
static uint32_t window_energy(const struct light_window *window)
{
	uint64_t sum = 0;
	for (int i = 1; i < WINDOW_SAMPLES; i++) {
		int64_t delta = (int64_t)window->clear[i] - window->clear[i - 1];
		sum += (uint64_t)(delta * delta);
	}
	return (uint32_t)(sum / (WINDOW_SAMPLES - 1));
}

static void sampler(void *a, void *b, void *c)
{
	uint32_t sequence = 0;
	/* A static thread can run before main(); main releases it after setup. */
	k_sem_take(&start_gate, K_FOREVER);

	while (true) {
		struct light_window window = { .sequence = sequence++ };
		bool interrupted = false;

		wait_until_resumed();
		/* This delay requests a cadence; it does not guarantee exact timing. */
		for (int i = 0; i < WINDOW_SAMPLES; i++) {
			if (sampling_paused()) {
				interrupted = true;
				break;
			}
			/* Fetch a new device sample, then extract the clear-light channel. */
			int rc = sensor_sample_fetch(light_sensor);
			struct sensor_value value;
			if (rc == 0) {
				rc = sensor_channel_get(light_sensor, SENSOR_CHAN_LIGHT, &value);
			}
			if (rc != 0) {
				printk("SENSOR read failed: %d; retrying\n", rc);
				interrupted = true;
				k_msleep(500);
				break;
			}
			if (sampling_paused()) {
				interrupted = true;
				break;
			}
			window.clear[i] = (uint16_t)value.val1;
			/* Uptime is milliseconds since boot, not wall-clock time. */
			int64_t now = k_uptime_get();
			if (i == 0) {
				window.first_ms = now;
			}
			window.last_ms = now;
			k_msleep(SAMPLE_PERIOD_MS);
		}
		if (interrupted) {
			continue; /* A partial window is never sent to the detector. */
		}

		/* A full queue drops this new window rather than blocking the sampler.
		 * Holding the gate prevents a simultaneous event from purging the queue
		 * and then having this pre-event window inserted behind the purge.
		 */
		k_mutex_lock(&gate_lock, K_FOREVER);
		if (!paused && k_msgq_put(&window_q, &window, K_NO_WAIT) != 0) {
			dropped_windows++;
			printk("DROP window=%u queue full total=%u\n",
			       window.sequence, dropped_windows);
		}
		k_mutex_unlock(&gate_lock);
	}
}

static void detector(void *a, void *b, void *c)
{
	/* Baseline belongs only to the detector thread: no lock is needed for it. */
	uint64_t baseline = 0;
	int warmup = 0;
	k_sem_take(&start_gate, K_FOREVER);

	while (true) {
		struct light_window window;
		k_msgq_get(&window_q, &window, K_FOREVER);
		uint32_t energy = window_energy(&window);
		uint16_t minimum = UINT16_MAX;
		uint16_t maximum = 0;
		for (int i = 0; i < WINDOW_SAMPLES; i++) {
			if (window.clear[i] < minimum) minimum = window.clear[i];
			if (window.clear[i] > maximum) maximum = window.clear[i];
		}
		printk("LIGHT window=%u span=%lld ms clear=%u..%u energy=%u baseline=%llu\n",
		       window.sequence, (long long)(window.last_ms - window.first_ms),
		       minimum, maximum, energy, (unsigned long long)baseline);

		/* Start with a simple mean so the first few windows establish ambient
		 * change energy before any event decision is made.
		 */
		if (warmup < BASELINE_WINDOWS) {
			baseline = (baseline * warmup + energy) / (warmup + 1);
			warmup++;
			continue;
		}

		/* The floor avoids an almost-zero baseline triggering on tiny noise. */
		uint64_t threshold = baseline * (100U + RISE_PERCENT) / 100U;
		if (threshold < MIN_ENERGY_COUNTS_SQUARED) {
			threshold = MIN_ENERGY_COUNTS_SQUARED;
		}
		if (energy > threshold) {
			printk("EVENT light change detected window=%u energy=%u threshold=%llu\n",
			       window.sequence, energy, (unsigned long long)threshold);
			/* Pause new windows and discard queued pre-event windows. The sampler
			 * checks this flag at sample boundaries and before queue insertion.
			 */
			k_mutex_lock(&gate_lock, K_FOREVER);
			paused = true;
			k_msgq_purge(&window_q); /* Do not process pre-event windows on resume. */
			k_mutex_unlock(&gate_lock);
			printk("COOLDOWN %d ms: sampling paused, queued windows discarded\n", COOLDOWN_MS);
			k_msleep(COOLDOWN_MS);
			/* Lighting may have changed during cooldown, so learn a new baseline. */
			baseline = 0;
			warmup = 0;
			k_mutex_lock(&gate_lock, K_FOREVER);
			paused = false;
			k_mutex_unlock(&gate_lock);
			k_sem_give(&resume_sampling);
			printk("RESUME sampling; recalibrating baseline\n");
		} else {
			/* Exponential moving average: 1/8 new window, 7/8 history. */
			baseline = (baseline * 7U + energy) / 8U;
		}
	}
}

/* Each thread has its own statically allocated stack. */
K_THREAD_DEFINE(sampler_id, 2048, sampler, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(detector_id, 2048, detector, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
#if defined(CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT)
	/* USB builds wait for a terminal's DTR signal so the startup trace is
	 * visible even when the serial port appears after firmware boot.
	 */
	const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	uint32_t dtr = 0;
	if (!device_is_ready(console)) {
		return 1;
	}
	while (uart_line_ctrl_get(console, UART_LINE_CTRL_DTR, &dtr) != 0 || !dtr) {
		k_msleep(100);
	}
	k_msleep(100);
#endif
	printk("APDS9960 light-event demo: %d samples/window, queue=2, cooldown=%d ms\n",
	       WINDOW_SAMPLES, COOLDOWN_MS);
	if (!device_is_ready(light_sensor)) {
		printk("APDS9960 not ready; check the Sense Rev2 sensor path\n");
		return 1;
	}
	printk("Sensor ready. Keep lighting steady for calibration, then vary light over APDS9960.\n");
	/* One permit for each waiting thread. */
	k_sem_give(&start_gate);
	k_sem_give(&start_gate);
	return 0;
}
