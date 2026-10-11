#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#ifdef CONFIG_ARCH_POSIX
#include <nsi_main.h>
#endif

int main(void)
{
    printk("M202A Zephyr smoke test\n");
    for (int i = 0; i < 5; ++i) {
        printk("tick=%d uptime_ms=%lld\n", i, (long long)k_uptime_get());
        k_msleep(100);
    }
    printk("M202A SIMULATION PASS\n");
#ifdef CONFIG_ARCH_POSIX
 nsi_exit(0);
#endif
    return 0;
}
