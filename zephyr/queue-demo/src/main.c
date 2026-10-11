/* ECE M202A / CS M213A: bounded queue and decision freshness demo.
 * Synthetic window references keep the example independent of sensor drivers.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#if defined(CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT)
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#endif

#if defined(CONFIG_ARCH_POSIX)
#include <nsi_main.h>
#endif

#define WINDOW_COUNT 12
#define ARRIVAL_MS 20
#define SERVICE_MS 35
#define FRESHNESS_DEADLINE_MS 60

struct window_ref {
	int32_t sequence;
	int64_t first_sample_ms;
};

/* The queue copies small descriptors. It does not own sensor-data buffers. */
K_MSGQ_DEFINE(window_q, sizeof(struct window_ref), 2, 8);
K_SEM_DEFINE(worker_finished, 0, 1);

static int processed;
static int stale;

static void inference_worker(void *a, void *b, void *c)
{
	struct window_ref next;

	while (k_msgq_get(&window_q, &next, K_FOREVER) == 0) {
		if (next.sequence == -1) {
			break;
		}

		/* Model execution is represented by a fixed service time. */
		k_msleep(SERVICE_MS);
		int64_t age_ms = k_uptime_get() - next.first_sample_ms;
		bool is_stale = age_ms > FRESHNESS_DEADLINE_MS;
		processed++;
		stale += is_stale;
		printk("DECIDE window=%d age=%lld ms %s\n", next.sequence,
		       (long long)age_ms, is_stale ? "STALE" : "fresh");
	}
	k_sem_give(&worker_finished);
}

K_THREAD_DEFINE(worker_id, 2048, inference_worker, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
	int dropped = 0;
	struct window_ref stop = { .sequence = -1, .first_sample_ms = 0 };

#if defined(CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT)
	/* A USB terminal opened after flashing must not miss this short trace. */
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

	printk("CONFIG arrivals=%d ms service=%d ms queue=2 deadline=%d ms\n",
	       ARRIVAL_MS, SERVICE_MS, FRESHNESS_DEADLINE_MS);
	for (int i = 0; i < WINDOW_COUNT; i++) {
		struct window_ref window = {
			.sequence = i,
			.first_sample_ms = k_uptime_get(),
		};
		if (k_msgq_put(&window_q, &window, K_NO_WAIT) == 0) {
			printk("ARRIVE window=%d at=%lld ms queued\n", i,
			       (long long)window.first_sample_ms);
		} else {
			dropped++;
			printk("ARRIVE window=%d at=%lld ms DROP_NEW queue_full\n", i,
			       (long long)window.first_sample_ms);
		}
		k_msleep(ARRIVAL_MS);
	}

	/* A blocking sentinel guarantees that all accepted windows are drained. */
	k_msgq_put(&window_q, &stop, K_FOREVER);
	k_sem_take(&worker_finished, K_FOREVER);
	printk("SUMMARY accepted=%d processed=%d dropped=%d stale=%d\n",
	       WINDOW_COUNT - dropped, processed, dropped, stale);
	int ok = (processed + dropped == WINDOW_COUNT && dropped > 0) ? 0 : 1;
	printk("M202A QUEUE DEMO %s\n", ok ? "FAIL" : "PASS");
#if defined(CONFIG_ARCH_POSIX)
	nsi_exit(ok);
#endif
	return ok;
}
