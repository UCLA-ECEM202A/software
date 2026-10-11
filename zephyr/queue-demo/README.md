# Synthetic bounded-queue application

This application has one producer, one Zephyr worker thread, and a two-entry `k_msgq`. The producer requests a new window record every 20 ms; the worker requests a 35-ms service delay. The producer uses an explicit **drop-new** policy on a full queue. The worker reports decision age and whether it exceeds a hypothetical 60-ms freshness deadline.

Run from the repository’s `zephyr/` folder:

```sh
./zephyr sim queue-demo       # macOS
./zephyr sim-clean queue-demo # pristine macOS build
```

Windows PowerShell uses `.\zephyr.ps1 sim queue-demo` or `.\zephyr.ps1 sim-clean queue-demo`. To cross-build Nano firmware, use `build queue-demo` with the matching launcher. For a visible hardware trace, use the separate `build-usb queue-demo` and `flash-usb queue-demo PORT` actions, then open the application USB serial port. The USB build waits for DTR before producing records. See the [USB-console procedure](../README.md#usb-console-first-nano-run). The instructor has now verified USB output from the Nano on macOS; the revised Windows path remains untested.

**Verified October 8, 2026:** In the course's pinned Zephyr 4.4.2 `native_sim/native/64` image, the app exited with `M202A QUEUE DEMO PASS` and reported `accepted=9 processed=9 dropped=3 stale=7` for 12 synthetic records. It cross-compiled for `arduino_nano_33_ble/nrf52840/sense`, producing a 21,012-byte firmware image in that test. The target build is not a flash/run test on the Sense Rev2.

The application requests sleeps of 20 and 35 ms with `CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000`. The observed simulator trace advanced producer releases by 21 ms, reflecting timeout rounding to the next tick. Discuss the **observed simulated trace** rather than idealized 20/35-ms wall-time execution. For a live class demonstration, predict which arrivals will be dropped, then change `SERVICE_MS` to 10, rebuild, and compare. The queued `struct window_ref` contains only a sequence number and timestamp—**no sensor buffer or reference to one**. A real sensor pipeline would also need an explicit buffer-ownership and reuse rule.

**Instructor-reported Nano run, October 11, 2026:** The Sense Rev2 USB-console build on macOS printed `[Connected]`, then the complete `CONFIG`, `ARRIVE`, and `DECIDE` trace, followed by `SUMMARY accepted=9 processed=9 dropped=3 stale=7` and `M202A QUEUE DEMO PASS`. The first reported arrival was at uptime 1529 ms. This is a physical run and console check, not a calibrated timing or energy measurement. It does not validate the older plain board build's UART output or the new Windows launcher.
