/* Loud-sound event pipeline for the Nano 33 BLE Sense Rev2 PDM microphone.
 *
 * The microphone emits PDM bits; Zephyr's DMIC driver decimates them to PCM.
 * Follow capture() -> window_q -> detect() to see one PCM block become an
 * event. This app compares sound energy with its recent ambient level; it is
 * not a calibrated sound-pressure meter or a speech classifier.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define PCM_RATE_HZ 16000
#define PCM_WIDTH_BITS 16
/* 16 kHz * 50 ms = 800 samples = 1,600 bytes per mono window. */
#define WINDOW_MS 50
#define WINDOW_SAMPLES (PCM_RATE_HZ * WINDOW_MS / 1000)
#define BLOCK_BYTES (WINDOW_SAMPLES * sizeof(int16_t))
#define DMA_BLOCKS 4
#define READ_TIMEOUT_MS 1000
/* PDM decimation starts with a large filter transient, also after restart. */
#define SETTLING_BLOCKS 10
#define BASELINE_WINDOWS 20
#define RISE_FACTOR 4U             /* Four times energy = twice RMS amplitude. */
/* An absolute floor stops tiny changes in a nearly silent room triggering. */
#define MIN_ENERGY_PCM_SQUARED 10000U
#define COOLDOWN_MS 3000

/* The PDM peripheral writes PCM into this fixed slab. Every successful
 * dmic_read() hands one block to capture(); capture() must free it after use.
 */
K_MEM_SLAB_DEFINE_STATIC(audio_slab, BLOCK_BYTES, DMA_BLOCKS, 4);

struct audio_window {
	int64_t end_ms;
	uint32_t sequence;
	int16_t pcm[WINDOW_SAMPLES];
};

/* The queue copies complete windows into two fixed-size slots. It does not
 * retain a pointer to a DMA block that the PDM driver will reuse.
 */
K_MSGQ_DEFINE(window_q, sizeof(struct audio_window), 2, 4);
/* This gate makes pause and queue insertion/purge one atomic decision. */
K_MUTEX_DEFINE(gate_lock);
/* Both statically created threads wait for main() to configure the DMIC. */
K_SEM_DEFINE(start_gate, 0, 2);
/* During cooldown the capture thread blocks here after stopping the DMIC. */
K_SEM_DEFINE(resume_capture, 0, 1);

/* The pinned Sense board definition names its PDM peripheral dmic_dev. */
static const struct device *const microphone = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));
static bool paused;
static uint32_t dropped_windows;

static bool capture_paused(void)
{
	bool value;
	k_mutex_lock(&gate_lock, K_FOREVER);
	value = paused;
	k_mutex_unlock(&gate_lock);
	return value;
}

/* This integer approximation of E[x^2] - E[x]^2 measures the mean-square
 * AC component. Removing the mean rejects a DC offset; the result is squared
 * PCM counts, not dB SPL. Peak is a diagnostic, not the event feature.
 */
static uint64_t window_energy(const struct audio_window *window, uint16_t *peak)
{
	int64_t sum = 0;
	uint64_t sum_squares = 0;
	uint32_t maximum = 0;
	for (int i = 0; i < WINDOW_SAMPLES; i++) {
		int32_t sample = window->pcm[i];
		uint32_t magnitude = sample < 0 ? (uint32_t)-sample : (uint32_t)sample;
		if (magnitude > maximum) {
			maximum = magnitude;
		}
		sum += sample;
		sum_squares += (int64_t)sample * sample;
	}
	*peak = (uint16_t)maximum;
	int64_t mean = sum / WINDOW_SAMPLES;
	return sum_squares / WINDOW_SAMPLES - (uint64_t)(mean * mean);
}

static void capture(void *a, void *b, void *c)
{
	uint32_t sequence = 0;
	int settling = SETTLING_BLOCKS;
	/* A static thread can run before main(); wait for DMIC setup. */
	k_sem_take(&start_gate, K_FOREVER);

	while (true) {
		void *block = NULL;
		size_t size = 0;
		/* The driver gives us ownership of one slab block on success. */
		int rc = dmic_read(microphone, 0, &block, &size, READ_TIMEOUT_MS);
		if (rc != 0) {
			printk("AUDIO read failed: %d; retrying\n", rc);
			k_msleep(100);
			continue;
		}
		if (size != BLOCK_BYTES) {
			printk("AUDIO unexpected block size: %u bytes\n", (unsigned int)size);
			k_mem_slab_free(&audio_slab, block);
			continue;
		}
		/* A PDM start produces misleading high-energy blocks. Free and skip
		 * them, both at boot and after each cooldown restart.
		 */
		if (settling > 0) {
			k_mem_slab_free(&audio_slab, block);
			settling--;
			if (settling == 0) {
				printk("AUDIO settled; beginning ambient calibration\n");
			}
			continue;
		}

		/* Copy before freeing: the PDM driver may overwrite this slab block
		 * as soon as it is returned. end_ms is uptime, not wall-clock time.
		 */
		struct audio_window window = { .end_ms = k_uptime_get(), .sequence = sequence++ };
		memcpy(window.pcm, block, BLOCK_BYTES);
		k_mem_slab_free(&audio_slab, block);

		/* Never block capture on a full queue. The gate prevents an old
		 * window from being inserted just after detect() purges the queue.
		 */
		k_mutex_lock(&gate_lock, K_FOREVER);
		if (!paused && k_msgq_put(&window_q, &window, K_NO_WAIT) != 0) {
			dropped_windows++;
			printk("DROP audio window=%u queue full total=%u\n",
			       window.sequence, dropped_windows);
		}
		bool stop = paused;
		k_mutex_unlock(&gate_lock);

		/* Only this thread calls DMIC start/stop. The detector changes
		 * paused; this thread owns the hardware state transition.
		 */
		if (stop) {
			rc = dmic_trigger(microphone, DMIC_TRIGGER_STOP);
			printk("AUDIO capture stopped: %d\n", rc);
			while (capture_paused()) {
				k_sem_take(&resume_capture, K_FOREVER);
			}
			rc = dmic_trigger(microphone, DMIC_TRIGGER_START);
			printk("AUDIO capture restarted: %d\n", rc);
			if (rc == 0) {
				settling = SETTLING_BLOCKS;
			} else {
				k_msleep(500);
			}
		}
	}
}

static void detect(void *a, void *b, void *c)
{
	/* Baseline belongs only to this thread, so it requires no mutex. */
	uint64_t baseline = 0;
	int warmup = 0;
	k_sem_take(&start_gate, K_FOREVER);

	while (true) {
		struct audio_window window;
		k_msgq_get(&window_q, &window, K_FOREVER);
		uint16_t peak = 0;
		uint64_t energy = window_energy(&window, &peak);
		printk("AUDIO window=%u end=%lld ms energy=%llu baseline=%llu peak=%u\n",
		       window.sequence, (long long)window.end_ms,
		       (unsigned long long)energy, (unsigned long long)baseline, peak);

		/* Learn ambient energy from the first 20 *settled* windows before
		 * deciding that any sound is unusually loud.
		 */
		if (warmup < BASELINE_WINDOWS) {
			baseline = (baseline * warmup + energy) / (warmup + 1);
			warmup++;
			continue;
		}

		/* A sound must exceed both the relative ambient threshold and the
		 * absolute floor. Multiplying energy by four doubles RMS amplitude.
		 */
		uint64_t threshold = baseline * RISE_FACTOR;
		if (threshold < MIN_ENERGY_PCM_SQUARED) {
			threshold = MIN_ENERGY_PCM_SQUARED;
		}
		if (energy > threshold) {
			printk("EVENT loud sound window=%u energy=%llu threshold=%llu\n",
			       window.sequence, (unsigned long long)energy,
			       (unsigned long long)threshold);
			/* Discard windows captured before this event; otherwise they could
			 * cause duplicate alerts immediately after cooldown.
			 */
			k_mutex_lock(&gate_lock, K_FOREVER);
			paused = true;
			k_msgq_purge(&window_q);
			k_mutex_unlock(&gate_lock);
			printk("COOLDOWN %d ms: capture stopping; queued windows discarded\n",
			       COOLDOWN_MS);
			k_msleep(COOLDOWN_MS);
			/* The room may have changed during cooldown; recalibrate after
			 * capture() restarts the microphone and skips settling blocks.
			 */
			baseline = 0;
			warmup = 0;
			k_mutex_lock(&gate_lock, K_FOREVER);
			paused = false;
			k_mutex_unlock(&gate_lock);
			k_sem_give(&resume_capture);
			printk("COOLDOWN complete; requesting capture restart and recalibration\n");
		} else {
			/* Exponential moving average: 1/16 new, 15/16 history. */
			baseline = (baseline * 15U + energy) / 16U;
		}
	}
}

/* Each thread has its own 4-KB static stack; a window lives on each stack. */
K_THREAD_DEFINE(capture_id, 4096, capture, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(detect_id, 4096, detect, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
#if defined(CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT)
	/* USB builds wait for terminal DTR so the student sees the startup trace. */
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
	printk("PDM acoustic-event demo: %d Hz, %d-bit mono, %d ms/window, queue=2\n",
	       PCM_RATE_HZ, PCM_WIDTH_BITS, WINDOW_MS);
	if (!device_is_ready(microphone)) {
		printk("PDM microphone not ready; check Sense Rev2 board description\n");
		return 1;
	}

	/* One 16-bit mono PCM stream; the right lane matches the Sense Rev2
	 * microphone's L/R wiring. CONFIG_REGULATOR powers the microphone rail.
	 */
	struct pcm_stream_cfg stream = {
		.pcm_rate = PCM_RATE_HZ,
		.pcm_width = PCM_WIDTH_BITS,
		.block_size = BLOCK_BYTES,
		.mem_slab = &audio_slab,
	};
	struct dmic_cfg config = {
		.io = {
			.min_pdm_clk_freq = 1000000,
			.max_pdm_clk_freq = 3500000,
			.min_pdm_clk_dc = 40,
			.max_pdm_clk_dc = 60,
		},
		.streams = &stream,
		.channel = {
			.req_num_streams = 1,
			.req_num_chan = 1,
			.req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_RIGHT),
		},
	};
	int rc = dmic_configure(microphone, &config);
	if (rc != 0) {
		printk("PDM configure failed: %d\n", rc);
		return 1;
	}
	rc = dmic_trigger(microphone, DMIC_TRIGGER_START);
	if (rc != 0) {
		printk("PDM start failed: %d\n", rc);
		return 1;
	}
	printk("Microphone ready. Keep ambient steady for %d windows, then clap nearby.\n",
	       BASELINE_WINDOWS);
	/* Release one permit to each thread only after the PDM driver starts. */
	k_sem_give(&start_gate);
	k_sem_give(&start_gate);
	return 0;
}
