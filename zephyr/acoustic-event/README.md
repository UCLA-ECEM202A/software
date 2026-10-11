# Acoustic event (Nano 33 BLE Sense Rev2)

This board-only app listens through the Sense Rev2's MP34DT06JTR digital microphone. Zephyr's PDM peripheral converts the stream into 16-bit, 16-kHz mono PCM. A capture thread receives 50-ms blocks (800 samples, 1,600 bytes), copies each complete block into a two-slot Zephyr message queue, and returns the original DMA block to a four-block memory slab. A detector thread removes the block's DC offset and computes mean-square sample energy. It reports a loud-sound event when energy exceeds both four times the moving ambient baseline (roughly twice the RMS amplitude) and a fixed floor. The first ten capture blocks are discarded to let the PDM filter settle; the next 20 blocks establish the ambient baseline. On an event it purges old windows, stops capture for three seconds, then restarts and recalibrates.

This is a **relative loudness detector**, not a calibrated sound-level meter, speech recognizer, or acoustic classifier. `energy` is in squared PCM counts, not dB SPL. Raw PCM exists only in bounded RAM buffers; the app prints summary values and event messages, not audio samples. The fixed threshold may need tuning for a particular room or board.

The pinned Nano board definition already supplies the microphone power rail and PDM pins. The app explicitly enables Zephyr's fixed-regulator subsystem so that rail is actually powered. The Sense Rev2 wiring ties the microphone's L/R input high, so this app requests the right PDM channel. The shared course Rev2 overlay is still applied by both launchers; no acoustic-specific overlay is needed.

## Build and run

From the repository’s `zephyr/` folder:

| | macOS | Windows PowerShell |
|---|---|---|
| Build USB firmware | `./zephyr build-usb acoustic-event` | `.\zephyr.ps1 build-usb acoustic-event` |
| Find bootloader port after double-tapping RESET | `./zephyr ports` | `.\zephyr.ps1 ports` |
| Flash | `./zephyr flash-usb acoustic-event /dev/cu.usbmodemXXXX` | `.\zephyr.ps1 flash-usb acoustic-event COM5` |
| Find application port | `./zephyr ports` | `.\zephyr.ps1 ports` |
| Open console | `screen /dev/cu.usbmodemXXXX 115200` | `.\serial-monitor.ps1 COM6` |

Substitute the actual port; the bootloader and application ports may differ. The app waits until the serial monitor asserts DTR before starting capture. Keep the room relatively steady during the settling and calibration interval (about 1.5 seconds), then clap or make another short loud sound near the microphone. Expect `AUDIO settled`, `AUDIO` lines, `EVENT loud sound`, `COOLDOWN`, and a capture restart.

The USB build uses about 51 KB flash and 32 KB RAM in the linker region summary, including two 4-KB application thread stacks, two queued PCM windows, four PDM DMA blocks, the kernel, and USB console. The region summary is a static build estimate, not a measured peak stack usage.

## Verification status

On October 11, 2026, plain and USB-console firmware cross-built with pinned Zephyr 4.4.2. The USB firmware flashed and verified on the instructor's Sense Rev2/macOS setup. The first physical run produced silence because `CONFIG_REGULATOR` was absent; adding it powered the board's microphone rail. A deliberate clap then produced `EVENT loud sound` (`energy=21320458`, `threshold=10000`), followed by `AUDIO capture stopped: 0` and `AUDIO capture restarted: 0`. The initial PDM filter output and each restart produced a large transient that polluted calibration, so the final firmware discards ten 50-ms blocks after every start. On the final flashed image, calibration began after `AUDIO settled`, ambient readings were around hundreds to low thousands of squared PCM counts, and an event, three-second cooldown, restart, and fresh settled calibration were observed. The revised PowerShell launcher and USB monitor have not yet been tested on Windows.

## Sources

- [Arduino Sense Rev2 hardware overview](https://docs.arduino.cc/hardware/nano-33-ble-sense-rev2/)
- [Arduino Sense Rev2 pinout](https://docs.arduino.cc/resources/pinouts/ABX00069-full-pinout.pdf)
- [Zephyr digital microphone sample](https://docs.zephyrproject.org/latest/samples/drivers/audio/dmic/README.html)
- [ST MP34DT06J microphone datasheet](https://www.st.com/resource/en/datasheet/mp34dt06j.pdf)
