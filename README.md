# RTI Display Controller using a Arduino Mega 2560

Standalone controller for a Volvo RTI motorized display, built for a sim rig (no car, no CAN bus). Drives the display's position/brightness over serial, and adds physical controls: a toggle switch that switches a relay to cut/restore +12V ignition power to the display (open/close), and a rotary encoder (with built-in pushbutton) to adjust brightness and cycle video mode. Status LEDs show what's currently selected.

Based on and ported from [TymEK49/RTI_control](https://github.com/TymEK49/RTI_control).

## What it does

- Continuously streams the RTI display's mode + brightness state over serial (required — the display needs this repeated periodically to stay up and lit).
- **Toggle switch → relay**  flips a relay that switches the display's +12V ignition wire. Relay ON = display powered (open, shows last-used video mode). Relay OFF = ignition cut, display fully unpowered (closed). This replaced an earlier software-only "off" approach (sending an OFF code over serial) that proved unreliable cutting real power is the dependable way to close it.
- **Rotary encoder** rotate to adjust brightness up/down; press the built-in button to cycle video mode (RGB → PAL → NTSC) while open.
- **LEDs** give at-a-glance feedback: whether the display is open, which video mode is active, and the selected brightness level (shown as the physical brightness of an LED).
- Also still accepts real button codes from the RTI connector itself, and typed single-character test commands over USB Serial both inherited from the original project.
- Remembers mode, brightness, and "enter/back" favorite brightness levels across power cycles via EEPROM.
- Default video mode is **PAL** (EU).

## Hardware

- Arduino Mega 2560 (needs 3+ hardware serial ports worth of separation, Mega has 4, UNO only has 1)
- Volvo RTI display unit
- SPDT toggle switch
- Relay module (5V-coil, driven from the Mega, switching the display's +12V ignition wire through its NO contacts) use a proper relay *module* with a built-in transistor/flyback diode, not a bare relay coil
- Rotary encoder with integrated pushbutton (KY-040 style or similar)
- 5x LEDs + current-limiting resistors (220–330Ω typical)

See `wiring.pdf` for the full pinout and wiring diagram reference.

## Files

| File | Purpose |
|---|---|
| `RTI_control_MEGA.ino` | Main sketch — upload this to the Mega |
| `wiring.pdf` | Full pin reference and wiring notes |

## Quick pin reference

| Function | Mega Pin |
|---|---|
| RTI display TX1 → display RX | 18 |
| Open/Close switch | 5 |
| Encoder CLK | 2 |
| Encoder DT | 3 |
| Encoder pushbutton (SW) | 4 |
| Ignition relay (signal) | 11 |
| Status LED (open/closed) | 6 |
| Mode LED — RGB | 7 |
| Mode LED — PAL | 8 |
| Mode LED — NTSC | 9 |
| Brightness LED (PWM) | 10 |

Full details, including which leg of each component goes to GND and debounce/direction notes, are in `wiring.pdf`.

## Setup

1. Wire everything per `wiring.pdf`.
2. Open `RTI_control_MEGA.ino` in the Arduino IDE.
3. Select **Board: Arduino Mega or Mega 2560** and the correct port.
4. Upload.
5. Open the Serial Monitor at 115200 baud if you want to send test commands or watch debug output.

## Tuning notes

A few constants at the top of the sketch may need adjusting for your specific hardware — I can't verify these against your actual parts, so treat them as starting points:

- `ENCODER_STEPS_PER_CLICK` — set to 4 (common for most encoders). If one physical click moves brightness by more than one level, change this to 2.
- Switch/encoder direction — if the toggle switch or encoder behave backwards from what you expect, see the "Reversed behavior" section in `wiring.pdf` for the one-line fixes.
- `RELAY_ACTIVE_HIGH` — set to `true` by default (relay energizes on a HIGH signal). Many cheap relay modules are active-LOW instead — if the relay is on when it should be off, flip this to `false`.
- `SWITCH_DEBOUNCE_MILS` / `ENC_BTN_DEBOUNCE_MILS` — debounce timings, adjust if you get double-triggers or missed presses with your specific switch/encoder.

## EEPROM map

| Address | Stores |
|---|---|
| 0 | Current display mode |
| 1 | Current brightness level |
| 2 | "Enter" favorite brightness level |
| 3 | "Back" favorite brightness level |
| 4 | Last video mode used while open (restored when switch flips back to open) |

## Credits

Original RTI serial protocol and EEPROM logic from [TymEK49/RTI_control](https://github.com/TymEK49/RTI_control). Mega port, switch/encoder controls, and LED feedback added on top.
