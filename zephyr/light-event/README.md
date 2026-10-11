# APDS9960 light-event pipeline (Sense Rev2)

This board-only teaching example reads **real** APDS9960 ambient-light clear-channel samples through Zephyr's sensor API. The driver reports raw 16-bit counts, **not calibrated lux**. A sampler thread takes eight readings per window, timestamps the first and last samples, and copies the entire 40-byte window into a two-entry `k_msgq` (80 bytes of queue payload storage). A detector thread receives the window and computes the mean squared difference between successive samples. This is a simple **change-energy** feature: a light transition inside a window raises it; steady illumination does not. It is not a neural model or a human-motion classifier.

The detector learns an initial baseline from three windows, then updates it slowly on non-event windows. It reports `EVENT light change detected` when energy exceeds both an absolute floor of 2,500 raw-counts-squared and a threshold 150% above the baseline. These are demonstration defaults, not calibrated thresholds. The `LIGHT` line prints each window's raw range, actual elapsed sample span, energy, and baseline so the instructor can tune them for room lighting.

After an event, the detector marks sampling paused, purges any already queued windows, sleeps for three seconds with `k_msleep()`, then resumes sampling and recalibrates the baseline. The sampler blocks while paused and discards a partial window if cooldown starts during acquisition. This is **application-level message suppression**, not a measured low-power mode: both threads retain their stacks, the USB console stays active, and no sensor or MCU power-state transition is requested. The first sample after resume belongs to a fresh window.

## Run on the Nano

From the repository’s `zephyr/` folder on macOS:

```sh
./zephyr build-usb light-event
./zephyr ports                         # after double-tapping RESET
./zephyr flash-usb light-event /dev/cu.usbmodemXXXX
./zephyr ports                         # find the application port
screen /dev/cu.usbmodemXXXX 115200
```

On Windows PowerShell, use `.\zephyr.ps1 build-usb light-event`, `.\zephyr.ps1 ports`, `.\zephyr.ps1 flash-usb light-event COM5`, then `.\serial-monitor.ps1 COM6`, using the port names actually reported. The revised Windows launcher and monitor still need a Windows hardware check. The existing distributed starter ZIP has not been changed.

Open the serial monitor after flashing; the app waits for DTR before it starts. Leave the sensor under steady room light for its first three windows, then cover/uncover the sensor or move a light source across it. Expect `LIGHT` records; a sufficiently abrupt change produces `EVENT`, `COOLDOWN`, and, three seconds later, `RESUME`. A single RESET restarts the experiment without flashing.

If the console says `APDS9960 not ready`, the sensor path needs board-specific debugging. If it repeatedly says `SENSOR read failed`, note the error code. If readings are steady or saturated at 0 or 65535, vary lighting and inspect raw `clear=` ranges before changing thresholds. The app was built with pinned Zephyr 4.4.2 and **physically verified by the instructor on a Sense Rev2 with macOS** on October 11, 2026. The revised Windows path still needs a hardware check.

In that run, the first steady-light windows were near 185 raw clear counts. Covering/uncovering produced change energies of 1496 and 1952, below the configured 2500 absolute floor. Window 15 reached energy 2593 and printed `EVENT light change detected`, followed by `COOLDOWN 3000 ms` and `RESUME`. Window 16 did not appear in the detector trace because a partial or queued window can be discarded when cooldown starts; the next complete processed window was 17. The observed `span=1428 ms` covers the **seven gaps between eight sample timestamps**, not the entire window cycle.

## Parameters and M03 teaching hooks

| Constant in `src/main.c` | Default | Meaning |
|---|---:|---|
| `WINDOW_SAMPLES` | 8 | Samples copied into each queue item |
| `SAMPLE_PERIOD_MS` | 200 | Sleep after each read; actual sample spacing also includes driver fetch time |
| `RISE_PERCENT` | 150 | Energy must rise 150% above learned baseline |
| `MIN_ENERGY_COUNTS_SQUARED` | 2500 | Absolute floor to avoid noise-triggering at a near-zero baseline |
| `COOLDOWN_MS` | 3000 | Time during which acquisition and messages are suppressed |

The compiled firmware uses about 53.4 KB flash and 18.6 KB RAM by the linker region summary. The two explicit worker stacks are 2 KiB each, and queue storage is 80 bytes; the total RAM figure also includes the main stack, kernel, USB, I²C and sensor-driver state. This makes the app useful for discussing **what has to fit**, queue copy versus descriptors, blocking I/O, observed versus requested sampling periods, dropped windows, and the limits of treating `k_msleep()` as power management. Changing the window length changes queue-item size and sample span; changing sensor fetch or USB printing can change timing even if the requested sleeps stay the same.

For a later multimodal variant, keep each sensor acquisition adapter separate and carry a sensor ID plus timestamp in a common event record. Merge windows by event time with an explicit lateness policy. IMU motion requires a Sense Rev2 BMI270 devicetree/driver path; microphone audio needs PDM buffering and a more careful RAM/rate budget. Add them only after this single-sensor pipeline is stable on the board.
