# BMI270 motion event (Sense Rev2)

This board-only example configures the Sense Rev2's BMI270 accelerometer for a 2 g range and 50 Hz output, then polls it every 20 ms. It copies 16 three-axis samples (192 bytes) into each queued window and passes the window from a sampler thread to a detector thread through a two-slot Zephyr message queue. The feature is mean squared *first difference* of acceleration, in `mg²`; this suppresses constant gravity and board orientation. After five quiet calibration windows, the detector reports a rise above both its adaptive baseline and a fixed floor, pauses sampling for three seconds, discards queued pre-event windows, then recalibrates. It is a simple change detector, **not** an activity classifier. The polling delay is requested, not a hard sampling deadline.

The course's shared [`board/nano33-sense-rev2.overlay`](../board/nano33-sense-rev2.overlay) replaces the absent Rev1 sensor description and declares the BMI270 at I²C address `0x68`. This app's tiny [`app.overlay`](app.overlay) enables that node. The magnetometer is intentionally not enabled for this first IMU test.

## Build and run

From the repository’s `zephyr/` folder:

| | macOS | Windows PowerShell |
|---|---|---|
| Build USB firmware | `./zephyr build-usb motion-event` | `.\zephyr.ps1 build-usb motion-event` |
| Find bootloader port after double-tapping RESET | `./zephyr ports` | `.\zephyr.ps1 ports` |
| Flash | `./zephyr flash-usb motion-event /dev/cu.usbmodemXXXX` | `.\zephyr.ps1 flash-usb motion-event COM5` |
| Find application port | `./zephyr ports` | `.\zephyr.ps1 ports` |
| Open console | `screen /dev/cu.usbmodemXXXX 115200` | `.\serial-monitor.ps1 COM6` |

Substitute the ports currently shown on your computer; the bootloader and application may enumerate differently. Close the console before flashing. The firmware waits for the USB serial monitor to assert DTR, so the trace starts when the console opens. On macOS, exit `screen` with Control-A, backslash, then `y`.

Expected trace: `Sensor ready`, five `MOTION` calibration windows, then more `MOTION` windows. The first triple in each line is in milli-g; at rest, its vector length should be near 1000 mg, and readings should change when the board moves. Move or tap the board after calibration to see `EVENT motion detected`, `COOLDOWN`, and `RESUME`. If the detector is too sensitive or insensitive on actual hardware, tune `MIN_ENERGY_MG_SQUARED` and `RISE_PERCENT` in `src/main.c` based on measured quiet/motion traces, not just the compile result.

## Verification status

On October 11, 2026, plain and USB-console firmware compiled with pinned Zephyr 4.4.2. The generated devicetree contains BMI270 `status = "okay"` at `0x68`; Rev1 LSM9DS1 and HTS221 nodes are disabled; Kconfig includes `CONFIG_BMI270=y`. On the instructor's Sense Rev2 connected to macOS, the USB firmware flashed and verified. The first live attempt returned all zeros until the app set a nonzero accelerometer sampling frequency, which powers the sensor in the Zephyr driver. The corrected app produced resting samples near 1 g, an `EVENT motion detected` at window 87 (`energy=167601`, `threshold=25000`), a three-second cooldown, and resumed sampling. A final 100-ms settling delay removed the first all-zero sample; after flashing the final image, windows 0–14 had resting energies around 16–46 mg² with a baseline near 20–26 mg². The final image's event path is unchanged from the successful event run.
