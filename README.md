# UART DIN-5 ↔ ESP-NOW MIDI bridge (Seeed XIAO ESP32C3)

Firmware for a **Seeed Studio XIAO ESP32C3** that bridges standard 5-pin DIN MIDI to ESP-NOW using [ESP32_Host_MIDI](https://github.com/sauloverissimo/ESP32_Host_MIDI). Two boards running this sketch become a wireless MIDI cable.

Board profile: [XIAO ESP32C3](https://www.espboards.dev/esp32/xiao-esp32c3/) (`board = seeed_xiao_esp32c3`). 21 × 17.8 mm, native USB-C, 4 MB QIO flash.

**Attach the included u.FL antenna before powering the board.** The C3 XIAO has no onboard antenna. Without it, ESP-NOW transmit can look fine while the other board receives nothing.

**Do not use an ESP32-C3 Super Mini.** Those clones have a broken RF layout (ceramic antenna against the crystal). This firmware targets the XIAO only.

## How transports actually work

The library does **not** route MIDI by itself.

1. `UARTConnection` and `ESPNowConnection` are both `MIDITransport`s.
2. `midiHandler.addTransport(...)` registers them. `midiHandler.task()` polls each one.
3. Incoming bytes are parsed into **one shared event queue**. Every event has `event.source` pointing at the transport that received it.
4. `sendMidiMessage()` only **transmits**. It does not enqueue the message again.

The bridge therefore:

```
if (event.source == UART)     send only to ESP-NOW
if (event.source == ESP-NOW)  send only to UART
otherwise                     drop
```

```
DIN-5 IN  --> UART0 (GPIO20 / D7 RX) --> queue --> ESP-NOW radio
DIN-5 OUT <-- UART0 (GPIO21 / D6 TX) <-- queue <-- ESP-NOW radio
```

That is the same pattern as the library's MIDI-Router example. The jam example is simpler because it never uses MIDIHandler: it calls `espNow.setMidiCallback(...)` itself. Do **not** copy that here — `addTransport()` already installs the callback that fills the queue.

## ESP-NOW details

Receive and parsing come from the library's `ESPNowConnection`. `src/ESPNowMidi.h` only:

- waits for Arduino-ESP32 3.x STA to start before `begin()`
- sends to a unicast MAC when `ESPNOW_PEER_MAC` is set (upstream `sendMidiMessage()` always broadcasts)

| Topic | Behavior |
|--------|----------|
| Addressing | Broadcast by default. Optional unicast via `ESPNOW_PEER_MAC` (the other board's MAC printed at boot). |
| Channel | `ESPNOW_CHANNEL` (default **1**). Both boards must match. |
| Payload | 2–3 MIDI bytes. Notes, CC, program change, channel pressure, pitch bend. |
| Not carried | SysEx (too long). Clock / Start / Stop (1-byte realtime is dropped on ESP-NOW receive). |

## Hardware

Default pins (`src/config.h`): **GPIO 20 (D7 / RX) = MIDI IN**, **GPIO 21 (D6 / TX) = MIDI OUT**. Debug logs use native USB CDC (`Serial` on USB-C). The ESP32-C3 has no UART2; MIDI runs on UART0 (`Serial0`) so the USB console stays free.

Seeed recommends D6 as an output (it is U0TXD and dumps bootloader text at reset). That matches MIDI OUT. MIDI IN on D7 (U0RXD) is the labeled RX pin. A few garbage bytes may appear on MIDI OUT at boot — ignore them, or mute the downstream device until the bridge prints its banner.

There is **no user LED** (only a charge indicator). Activity pulses are off unless you wire an LED + ~150 Ω to **D10 (GPIO 10)** and set `STATUS_LED_PIN` to `10`.

### MIDI IN (optocoupler required)

- DIN pin 4 → 220 Ω → optocoupler LED anode
- DIN pin 5 → LED cathode
- Optocoupler output → GPIO 20 (D7 / RX)
- Follow the coupler datasheet for VCC / pull-up (3.3 V on the ESP32 side)

### MIDI OUT

- XIAO **5V** (USB) → 220 Ω → DIN pin 4
- GPIO 21 (D6 / TX) → 220 Ω → DIN pin 5
- DIN pin 2 → GND (cable shield)

3.3 V out works with a lot of modern gear; 5 V is closer to the MIDI spec.

If the board brownout-resets when Wi-Fi transmits, use a short USB cable straight into the computer (not a hub).

## Build and flash

### PlatformIO

```bash
pio run -e seeed-xiao-esp32c3 -t upload
pio device monitor
```

Need Arduino-ESP32 3.x ([pioarduino](https://github.com/pioarduino/platform-espressif32)). Upload is **921600** over native USB. The board JSON already enables USB CDC on boot; `platformio.ini` repeats those flags.

### If upload fails

1. **Data cable, not charge-only.** Prefer a short cable straight into the computer, not a hub.
2. **Close the serial monitor** before Upload.
3. **Port never appears.** Click **RESET** once while plugged in. Still missing: hold **BOOT**, plug in USB, then release BOOT to enter the ROM bootloader.
4. **Upload times out.** Hold **BOOT**, click Upload, keep holding until writing starts, then release. If that still times out: hold **BOOT**, tap **RESET**, keep holding BOOT.
5. Drop `upload_speed` to **460800** if 921600 is flaky.

### Arduino IDE

1. Boards Manager: **esp32** by Espressif, 3.0 or newer.
2. Board: **XIAO_ESP32C3**.
3. **USB CDC On Boot: Enabled**.
4. Flash Size: **4MB**. Flash Mode: **QIO**. Upload Speed: **921600**.
5. Library Manager: install **ESP32_Host_MIDI**.
6. Copy `src/main.cpp`, `src/config.h`, and `src/ESPNowMidi.h` into a sketch folder (rename `main.cpp` to `your_sketch.ino`).

## Configure

```cpp
#define MIDI_RX_PIN 20
#define MIDI_TX_PIN 21
#define ESPNOW_CHANNEL 1
#define ESPNOW_PEER_MAC 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // broadcast
```

Use two boards on the same channel. For unicast, set each board's `ESPNOW_PEER_MAC` to the **other** board's printed MAC.

## Serial log

```
UART DIN-5 <-> ESP-NOW MIDI bridge
ESP-NOW mode: broadcast
This board MAC AA:BB:CC:DD:EE:FF
ESP-NOW channel: 1
UART MIDI: RX=GPIO20 TX=GPIO21 @ 31250 baud (UART0)
Transports:
  [0] UART connected=1
  [1] ESP-NOW connected=1
UART -> NOW NoteOn ch=1 C4 vel=100
NOW -> UART NoteOff ch=1 C4 vel=0
stats  UART->NOW=12  NOW->UART=12
```

## License

MIT
