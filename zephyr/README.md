# Course Zephyr applications

This `software/zephyr/` folder is the maintained source for the ECE M202A / CS M213A Zephyr 4.4.2 toolchain wrapper and course Zephyr applications. Each application has its own `CMakeLists.txt`, `prj.conf`, and `src/`. The shared [app registry](apps.csv) records target capabilities and their verification status. Both launchers read this registry and expose the same actions on macOS and Windows; `just` is not required.

The existing `handouts/Zephyr Setup/m202a-zephyr/` and starter ZIP are an **older distribution snapshot** with the former command names. The instructor reports that students used that package successfully on Windows. It remains intact so existing student links do not silently change. Refresh that package and its live Google Doc before distributing these new commands. This repository folder is the maintained source; the Drive package is an older distribution snapshot.

Old-to-new command mapping: `sim-smoke` → `sim smoke`; `sim-har` → `sim har`; `sim-har-clean` → `sim-clean har`; `nano-build` → `build blinky`; `nano-flash PORT` → `flash blinky PORT`.

| App | `native_sim` | Nano build | Nano run status |
|---|---|---|---|
| `smoke` | Verified: five ticks and `M202A SIMULATION PASS` | Not configured | — |
| `har` | Verified: stored HAR examples from M02 | Not configured | — |
| `blinky` | Not configured | Verified | **Verified on the instructor's Sense Rev2:** red LED blinks |
| `queue-demo` | Verified: `M202A QUEUE DEMO PASS` | Plain and USB-console cross-builds verified | **USB-console run verified by instructor on Sense Rev2/macOS** |
| `light-event` | Not configured | Plain and USB-console builds verified | **Sensor readings, event, and cooldown verified by instructor on Sense Rev2/macOS** |
| `motion-event` | Not configured | Plain and USB-console builds verified | **BMI270 readings, event, cooldown, and resume verified on instructor's Sense Rev2/macOS** |
| `acoustic-event` | Not configured | Plain and USB-console builds verified | **Microphone readings, event, cooldown, and restart verified on instructor's Sense Rev2/macOS** |

In `apps.csv`, `native_sim`, `nano_build`, and `nano_flash` are `yes`/`no` **capability gates** for the launchers. `sim_run_test`, `nano_build_test`, and `nano_run_test` are independent evidence fields: `verified`, `untested`, or `not_applicable`. In particular, `nano_flash=yes` means a firmware image can be passed to the flash command; it does not claim that its behavior has been verified on a board. When adding an app, create its folder and one registry row; mark each test `untested` until it passes. Record the test command, board, and observed behavior in the app README when claiming a physical run.

`queue-demo` is synthetic: it queues sequence/timestamp records, requests a 20-ms producer period and 35-ms worker service delay, and has **no sensor samples, classifier, or IMU driver**. Its `native_sim` time is simulated. On October 11, the instructor reported a complete USB-console trace from the Nano with `accepted=9 processed=9 dropped=3 stale=7` and `M202A QUEUE DEMO PASS`. This verifies that board execution and USB output worked in that setup; it does not measure the requested sleep periods or establish a real sensor path.

The new [`light-event`](light-event/README.md) app is the next hardware step: it copies actual APDS9960 light-reading windows through a bounded Zephyr queue to a change-energy detector, then pauses sampling and suppresses alerts for three seconds. On October 11, the instructor reported a Sense Rev2 trace with changing light readings, an event at window 15, three-second cooldown, and resumed sampling. It is board-only because `native_sim` has no APDS9960 device; a later simulation exercise should inject recorded sensor windows through a separate adapter rather than pretend the simulator has this hardware.

The [`motion-event`](motion-event/README.md) app uses the same two-thread, bounded-window pattern with the Rev2 BMI270 accelerometer. Its detector measures changes in three-axis acceleration, rather than light. On October 11, the instructor's Sense Rev2 produced plausible resting readings, motion events, a three-second cooldown, and resumed sampling. A 100-ms settling delay keeps the first calibration window from including a zero-valued startup conversion.

The [`acoustic-event`](acoustic-event/README.md) app applies the same pattern to 50-ms PCM blocks from the Sense Rev2 PDM microphone. It compares DC-removed sample energy with an adaptive ambient baseline and pauses capture after a loud-sound event. On October 11, the instructor's Sense Rev2 produced live audio-energy readings, loud-sound events, three-second cooldowns, and successful capture restarts. The app explicitly enables the microphone power regulator and discards ten blocks after each PDM start so filter transients do not enter calibration.

## Common actions

Open a terminal **in this folder**. Start Docker Desktop or OrbStack. On macOS use `./zephyr`; on Windows PowerShell use `.\zephyr.ps1`. The actions and app names are the same:

| Action | macOS example | Windows PowerShell example |
|---|---|---|
| Build pinned container | `./zephyr setup` | `.\zephyr.ps1 setup` |
| List apps and targets | `./zephyr apps` | `.\zephyr.ps1 apps` |
| Build and run simulator | `./zephyr sim queue-demo` | `.\zephyr.ps1 sim queue-demo` |
| Pristine simulator build and run | `./zephyr sim-clean queue-demo` | `.\zephyr.ps1 sim-clean queue-demo` |
| Build Nano firmware | `./zephyr build blinky` | `.\zephyr.ps1 build blinky` |
| Build Nano firmware with USB serial console | `./zephyr build-usb queue-demo` | `.\zephyr.ps1 build-usb queue-demo` |
| List serial ports | `./zephyr ports` | `.\zephyr.ps1 ports` |
| Flash Nano firmware | `./zephyr flash blinky /dev/cu.usbmodemXXXX` | `.\zephyr.ps1 flash blinky COM5` |
| Flash USB-console firmware | `./zephyr flash-usb queue-demo /dev/cu.usbmodemXXXX` | `.\zephyr.ps1 flash-usb queue-demo COM5` |
| Reflash a running USB-console app without pressing RESET | `./zephyr flash-usb-auto queue-demo` | `.\zephyr.ps1 flash-usb-auto queue-demo` |

For PowerShell, if script execution is disabled, run it with an appropriate per-session policy or invoke it from a PowerShell session where local scripts are allowed. The instructor's macOS launch and build paths are tested. Students have successfully used the **older distributed PowerShell package on Windows**; the **new action-plus-app PowerShell launcher** in this folder has not yet been run on Windows. A short Windows check of `apps`, `sim smoke`, `sim queue-demo`, and `build blinky` is appropriate before replacing the working student package. USB flashing remains host-side.

`sim APP` reuses the app's build directory. `sim-clean APP` requests `west build -p always` and regenerates it. `build APP` creates a pristine Nano build and copies `output/APP.bin` plus `output/APP.config`. All build directories stay in the Docker volume `m202a-zephyr-build`; the source folder is mounted into the container and the wrappers do not write build files into an app folder. The wrapper's actions are course conveniences around `west`, not Zephyr subcommands. They do not install a local macOS or Windows Zephyr toolchain.

`build-usb APP` makes a **separate** board build using Zephyr 4.4.2's `cdc-acm-console` snippet and writes `output/APP-usb.bin` plus `output/APP-usb.config`. `flash-usb` accepts only this USB-console image. The plain `build`/`flash` path remains available for the already verified LED example. USB CDC adds firmware and RAM overhead; the queue demo's USB build used about 45.7 KB flash and 15.8 KB RAM in the compiler's region summary, versus about 21 KB and 6.8 KB for its plain board build. These are build estimates, not measured peak usage.

## USB console: first Nano run

The following steps use `queue-demo`. Its USB build waits until a serial monitor asserts DTR before starting the short trace, so opening the monitor after flashing will not miss the messages. `printk()` then goes to the USB serial port. This path uses the Nano's built-in USB connection and needs no external UART adapter or Python package.

1. Start Docker Desktop or OrbStack and build: macOS `./zephyr build-usb queue-demo`; Windows PowerShell `.\zephyr.ps1 build-usb queue-demo`.
2. Connect the Nano with a USB **data** cable. Double-tap RESET. The pulsing bootloader LED indicates upload mode. Run `ports`; note the bootloader port (`/dev/cu.usbmodem...` on macOS, `COM...` on Windows).
3. Flash that port: macOS `./zephyr flash-usb queue-demo /dev/cu.usbmodemXXXX`; Windows `.\zephyr.ps1 flash-usb queue-demo COM5`. Substitute the port actually listed on your computer. The monitor must be closed during flashing.
4. Run `ports` **again**. The application may enumerate as a different serial port from the bootloader. Open that new/current port as below. The queue demo begins when the monitor opens and asserts DTR. Expect `CONFIG ...`, `ARRIVE ...`, `DECIDE ...`, `SUMMARY ...`, and `M202A QUEUE DEMO PASS`. Counts and ages on real hardware need not match the simulator.

On **macOS**, open the application port with `screen /dev/cu.usbmodemXXXX 115200`. To exit `screen`, press Control-A, then backslash, then confirm with `y`. If the application port does not appear, unplug/replug once and run `./zephyr ports` again. Use `ls /dev/cu.usbmodem*` only when you know one is present; a shell glob can error when none exists.

On **Windows PowerShell**, open the application port with `.\serial-monitor.ps1 COM6` (substitute the port shown by `.\zephyr.ps1 ports`). This small included monitor uses Windows' built-in .NET serial API, sets DTR, and exits with Control-C. If local PowerShell scripts are blocked, use the same per-session execution-policy setting that allowed the course's existing `zephyr.ps1`, or run the scripts from a permitted PowerShell session. If the port changes or is busy, close other serial monitors and check Device Manager under **Ports (COM & LPT)**.

The upload port and application port are different USB modes even if the operating system happens to assign the same name. If the application is not running or the USB console fails to enumerate, double-tap RESET to recover the bootloader and flash the known working `blinky` image. The instructor has verified the USB-console queue trace on the Nano with macOS; the revised PowerShell build/flash/monitor path still awaits a Windows test.

## Automatic reflash after the first USB-console upload

`build-usb` now includes an Arduino-compatible 1200-baud upload trigger. After the **first** USB-console image has been loaded using the manual bootloader procedure above, later USB-console images can be loaded without touching RESET. Close the serial monitor, find the *running application's* port with `ports`, and run `flash-usb-auto APP`. If more than one serial port is present, supply `APPLICATION_PORT` explicitly. For example:

```text
./zephyr build-usb motion-event
./zephyr ports
./zephyr flash-usb-auto motion-event
```

On Windows, use `.\zephyr.ps1 build-usb motion-event`, `.\zephyr.ps1 ports`, and `.\zephyr.ps1 flash-usb-auto motion-event`. If more than one serial port is present, append the application port shown on your computer, such as `/dev/cu.usbmodemXXXX` or `COM5`; do not supply the bootloader port to `flash-usb-auto`. The command briefly opens that port at 1200 baud, waits for the Nano's bootloader port, uploads the new image, verifies it, and restarts the board. Then find the new application port and run or monitor the app. You can repeat this build–load–run cycle with another USB-console app. The port name may change after every reset.

The installed app must itself contain the 1200-baud trigger, so a board still running an older image needs one manual double-tap and `flash-usb` first. A plain `build` image has no USB-console trigger. If automatic entry times out, close any serial monitor, double-tap RESET, and use `flash-usb APP BOOTLOADER_PORT`. On October 11, 2026, the macOS path was verified on the instructor's Nano: a 1200-baud request entered the bootloader without a button press, BOSSA verified a queue-demo reflash, the queue demo ran to `M202A QUEUE DEMO PASS`, and a subsequent automatic switch to `light-event` uploaded and started. The PowerShell path still awaits a Windows check.

## Board build and flash

The pinned Zephyr **4.4.2** image uses board target `arduino_nano_33_ble/nrf52840/sense`. This target allowed the LED application to build, upload, and blink on the instructor's **Nano 33 BLE Sense Rev2**. Newer Zephyr documentation uses revision-qualified targets; do not paste a target from current online docs into this pinned image without checking it.

Connect the board with a USB data cable, double-tap RESET to enter the bootloader, and use `ports` to identify the bootloader serial port. Close any serial monitor first. Flashing replaces the current application. The launchers check that firmware was built and linked at `0x10000`, then call the **Arduino fork of BOSSA without an extra offset**; the bootloader adds that base itself. A successful upload and verification is separate from observing application behavior.

On macOS, `bin/bossac` was built for Apple Silicon with `./build-bossac-mac` from Arduino BOSSA `nrf` commit `37600d1635f830cc0cdf7c56e586a021272c4e35`. On Windows, the new PowerShell launcher looks for Arduino's `bossac.exe` under the default Arduino15 location; install Arduino Mbed OS Nano Boards or pass `-Bossac` with the full executable path. The instructor's report confirms Windows use of the older package but does not specify which flash actions students exercised, so this revised `flash APP PORT` action still needs a separate Windows check. The bootloader port may disappear after the application starts; double-tap RESET to restore it for the next upload.

## Shared Sense Rev2 board overlay

The Sense **Rev2** changed its inertial sensors from the original board's LSM9DS1 to **BMI270 plus BMM150**, and its temperature/humidity sensor from HTS221 to HS3003. The pinned Zephyr 4.4.2 board target still describes the Rev1 hardware. Every Nano build now loads [`board/nano33-sense-rev2.overlay`](board/nano33-sense-rev2.overlay) **first**. It disables absent Rev1 LSM9DS1 and HTS221 devices, declares a BMI270 at `0x68` (disabled unless an app needs it), and points the `accel0` alias there. An app-specific `app.overlay`, if present, loads second; the USB-console snippet loads after that. Thus `motion-event` enables BMI270 while `light-event` and `queue-demo` do not initialize it. APDS9960 and LPS22HB remain from the pinned board description; the shared overlay does not yet add HS3003 or BMM150.

The shared overlay is an interim course adaptation, not a complete Rev2 board port. The `motion-event` physical run verifies BMI270 initialization and live samples on the instructor's board. The light-event app no longer carries its own duplicate Rev1 sensor workaround. Both macOS and PowerShell launchers pass the same shared overlay path into the container. The revised PowerShell launcher has not yet been run on Windows. Current upstream Zephyr documentation describes a revision-qualified `@rev2` target, but that target is not present in this pinned image.

## Toolchain status and sources

The Dockerfile pins Ubuntu 24.04 by digest, Zephyr 4.4.2, west 1.5.0, and the Zephyr SDK selected by that Zephyr release. Python/Ubuntu package dependencies are not all independently locked. The instructor's Mac test confirmed the simulator and LED program; the original LED upload was corrected after finding that an additional BOSSA `-o 0x10000` offset was wrong for this bootloader. The corrected upload verified and the instructor observed the red LED blinking one second on/off. `queue-demo` was verified in `native_sim`, cross-built to the pinned Nano target, and subsequently run with USB-console output on the instructor's Nano.

- [Zephyr 4.4.2 release](https://github.com/zephyrproject-rtos/zephyr/releases/tag/v4.4.2)
- [Zephyr Nano 33 BLE documentation](https://docs.zephyrproject.org/latest/boards/arduino/nano_33_ble/doc/index.html) (current revision-qualified syntax may differ)
- [Zephyr native simulator](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html)
- [Arduino Nano 33 BLE Sense Rev2 specifications](https://docs.arduino.cc/resources/datasheets/ABX00069-datasheet.pdf)
- [Arduino BOSSA fork](https://github.com/arduino/BOSSA/tree/nrf)
