/* BMI270 motion event pipeline for Nano 33 BLE Sense Rev2.
 *
 * Read sampler() -> window_q -> detector() to follow a window from the IMU
 * to an event. The queue owns a complete copy of each three-axis window.
 * The detector uses changes between samples, not a learned activity model.
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

#define WINDOW_SAMPLES 16
#define SAMPLE_PERIOD_MS 20
#define COOLDOWN_MS 3000
#define BASELINE_WINDOWS 5
/* Require energy > 2.5 times the baseline, subject to an absolute floor. */
#define RISE_PERCENT 150
#define MIN_ENERGY_MG_SQUARED 25000U

/* Store milli-g as integers so the queue has a fixed size and no heap use. */
struct accel_sample {
	int32_t x_mg;
	int32_t y_mg;
	int32_t z_mg;
};

struct motion_window {
	int64_t first_ms;
	int64_t last_ms;
	uint32_t sequence;
	struct accel_sample sample[WINDOW_SAMPLES];
};

/* Two fixed-size queue slots hold copies of whole windows, not pointers to
 * a sampler stack variable that would disappear on the next iteration.
 */
K_MSGQ_DEFINE(window_q, sizeof(struct motion_window), 2, 4);
/* Serialize paused with queue insertion/purge across the two threads. */
K_MUTEX_DEFINE(gate_lock);
/* Threads are created statically but wait until main() configures the IMU. */
K_SEM_DEFINE(start_gate, 0, 2);
K_SEM_DEFINE(resume_sampling, 0, 1);

/* The app overlay enables bmi270; the shared Rev2 overlay declares it. */
static const struct device *const imu = DEVICE_DT_GET(DT_NODELABEL(bmi270));
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
	/* A semaphore wakeup is a cue to recheck state, not proof it stayed clear. */
	while (sampling_paused()) {
		k_sem_take(&resume_sampling, K_FOREVER);
	}
}

/* Average squared 3-D acceleration change between adjacent samples, in
 * mg^2. A stationary board has about 1 g of acceleration due to gravity,
 * but differencing removes that constant and the board's static orientation.
 */
static uint64_t window_energy(const struct motion_window *window)
{
	uint64_t sum = 0;
	for (int i = 1; i < WINDOW_SAMPLES; i++) {
		int64_t dx = (int64_t)window->sample[i].x_mg - window->sample[i - 1].x_mg;
		int64_t dy = (int64_t)window->sample[i].y_mg - window->sample[i - 1].y_mg;
		int64_t dz = (int64_t)window->sample[i].z_mg - window->sample[i - 1].z_mg;
		sum += (uint64_t)(dx * dx + dy * dy + dz * dz);
	}
	return sum / (WINDOW_SAMPLES - 1);
}

static void sampler(void *a, void *b, void *c)
{
	uint32_t sequence = 0;
	/* K_THREAD_DEFINE can run this before main(); wait for IMU configuration. */
	k_sem_take(&start_gate, K_FOREVER);

	while (true) {
		struct motion_window window = { .sequence = sequence++ };
		bool interrupted = false;

		wait_until_resumed();
		/* The requested 20-ms delay is not a hard sampling deadline; the
		 * timestamp span below exposes actual elapsed time for a window.
		 */
		for (int i = 0; i < WINDOW_SAMPLES; i++) {
			if (sampling_paused()) {
				interrupted = true;
				break;
			}
			/* Fetch one IMU sample, then read its X/Y/Z acceleration values. */
			int rc = sensor_sample_fetch(imu);
			struct sensor_value xyz[3];
			if (rc == 0) {
				rc = sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, xyz);
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
			/* Zephyr returns m/s^2; convert to milli-g for integer features. */
			window.sample[i] = (struct accel_sample){
				.x_mg = sensor_ms2_to_mg(&xyz[0]),
				.y_mg = sensor_ms2_to_mg(&xyz[1]),
				.z_mg = sensor_ms2_to_mg(&xyz[2]),
			};
			/* Uptime is milliseconds since boot, not wall-clock time. */
			int64_t now = k_uptime_get();
			if (i == 0) {
				window.first_ms = now;
			}
			window.last_ms = now;
			k_msleep(SAMPLE_PERIOD_MS);
		}
		if (interrupted) {
			/* Never classify a window interrupted by cooldown/read error. */
			continue;
		}

		/* Do not block acquisition if both queue slots are full. Holding the
		 * gate also prevents a pre-event window entering after a queue purge.
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
	/* Only this thread reads/writes baseline, so it needs no mutex. */
	uint64_t baseline = 0;
	int warmup = 0;
	k_sem_take(&start_gate, K_FOREVER);

	while (true) {
		struct motion_window window;
		k_msgq_get(&window_q, &window, K_FOREVER);
		uint64_t energy = window_energy(&window);
		printk("MOTION window=%u span=%lld ms energy=%llu baseline=%llu first=(%d,%d,%d) mg\n",
		       window.sequence, (long long)(window.last_ms - window.first_ms),
		       (unsigned long long)energy, (unsigned long long)baseline,
		       window.sample[0].x_mg, window.sample[0].y_mg, window.sample[0].z_mg);

		/* Establish a quiet reference from five windows before detecting. */
		if (warmup < BASELINE_WINDOWS) {
			baseline = (baseline * warmup + energy) / (warmup + 1);
			warmup++;
			continue;
		}

		/* The absolute floor prevents tiny sensor noise from triggering when
		 * the learned baseline is close to zero.
		 */
		uint64_t threshold = baseline * (100U + RISE_PERCENT) / 100U;
		if (threshold < MIN_ENERGY_MG_SQUARED) {
			threshold = MIN_ENERGY_MG_SQUARED;
		}
		if (energy > threshold) {
			printk("EVENT motion detected window=%u energy=%llu threshold=%llu\n",
			       window.sequence, (unsigned long long)energy,
			       (unsigned long long)threshold);
			/* Stop new windows and purge any captured before this event. The
			 * sampler checks paused at sample boundaries and before enqueueing.
			 */
			k_mutex_lock(&gate_lock, K_FOREVER);
			paused = true;
			k_msgq_purge(&window_q);
			k_mutex_unlock(&gate_lock);
			printk("COOLDOWN %d ms: sampling paused, queued windows discarded\n", COOLDOWN_MS);
			k_msleep(COOLDOWN_MS);
			/* Relearn the reference because the board may have moved. */
			baseline = 0;
			warmup = 0;
			k_mutex_lock(&gate_lock, K_FOREVER);
			paused = false;
			k_mutex_unlock(&gate_lock);
			k_sem_give(&resume_sampling);
			printk("RESUME sampling; recalibrating baseline\n");
		} else {
			/* Exponential moving average: 1/8 new, 7/8 history. */
			baseline = (baseline * 7U + energy) / 8U;
		}
	}
}

/* Each statically created thread has a separate 2-KB stack. */
K_THREAD_DEFINE(sampler_id, 2048, sampler, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(detector_id, 2048, detector, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
#if defined(CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT)
	/* Wait for the USB terminal to assert DTR so it sees the startup trace. */
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
	printk("BMI270 motion-event demo: %d samples/window, requested %d ms/sample, queue=2, cooldown=%d ms\n",
	       WINDOW_SAMPLES, SAMPLE_PERIOD_MS, COOLDOWN_MS);
	if (!device_is_ready(imu)) {
		printk("BMI270 not ready; check Sense Rev2 board overlay and sensor power\n");
		return 1;
	}
	/* BMI270 driver initialization does not start measurements. Setting a
	 * nonzero output data rate (ODR) also enables accelerometer power.
	 */
	struct sensor_value full_scale = { .val1 = 2, .val2 = 0 };
	struct sensor_value sampling_frequency = { .val1 = 50, .val2 = 0 };
	int rc = sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ,
				 SENSOR_ATTR_FULL_SCALE, &full_scale);
	if (rc == 0) {
		rc = sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ,
				     SENSOR_ATTR_SAMPLING_FREQUENCY, &sampling_frequency);
	}
	if (rc != 0) {
		printk("BMI270 configuration failed: %d\n", rc);
		return 1;
	}
	printk("BMI270 accelerometer enabled: 2 g range, 50 Hz output rate\n");
	/* Allow the first conversion after standby to settle before calibration. */
	k_msleep(100);
	printk("Sensor ready. Keep board still for %d windows, then gently move it.\n",
	       BASELINE_WINDOWS);
	/* Release one permit to each waiting thread. */
	k_sem_give(&start_gate);
	k_sem_give(&start_gate);
	return 0;
}
