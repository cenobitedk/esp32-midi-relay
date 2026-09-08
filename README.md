# UART DIN-5 ↔ ESP-NOW MIDI bridge (ESP32-C3 Super Mini)

Firmware for an **ESP32-C3 Super Mini** that bridges standard 5-pin DIN MIDI to ESP-NOW using [ESP32_Host_MIDI](https://github.com/sauloverissimo/ESP32_Host_MIDI). Two boards running this sketch become a wireless MIDI cable. One board can also talk to any other ESP32 that sends or receives MIDI over ESP-NOW with the same library.

## How transports actually work

The library does **not** route MIDI by itself. That is the part the docs leave easy to miss.

1. `UARTConnection` and `ESPNowConnection` are both `MIDITransport`s.
2. `midiHandler.addTransport(...)` registers them. `midiHandler.task()` polls each one.
3. Incoming bytes are parsed into **one shared event queue**. Every event has `event.source` pointing at the transport that received it.
4. `sendNoteOn` / `sendRaw` / `transport->sendMidiMessage()` only **transmit**. They do not enqueue the message again.

If you call `midiHandler.sendNoteOn(...)` with **no target**, the handler tries transports in registration order and stops at the first that accepts. UART is first here, so a note that arrived on ESP-NOW would go straight back out the DIN port… and a note that arrived on UART would also go back out UART. That is the echo you do not want.

The bridge therefore:

```
if (event.source == UART)     send only to ESP-NOW
if (event.source == ESP-NOW)  send only to UART
otherwise                     drop
```

That is the same pattern as the library's `MIDI-Router` example (`if (event.source == &synth) continue`). Sending to the other transport never puts the packet back on the source, so UART IN cannot loop to UART OUT and ESP-NOW cannot rebroadcast what it just heard.

```
DIN-5 IN  --> UARTConnection --> queue (source=&uartMIDI) --> ESP-NOW radio
DIN-5 OUT <-- UARTConnection <-- queue (source=&espNow)   <-- ESP-NOW radio
```

## ESP-NOW details

`ESPNowConnection::begin(channel)` starts Wi-Fi in station mode **without joining an access point**, then starts ESP-NOW. There is no pairing handshake.

| Topic | Behavior in this project |
|--------|--------------------------|
| Addressing | Broadcast by default (`FF:FF:FF:FF:FF:FF`). Optional unicast via `ESPNOW_PEER_MAC` in `src/config.h`. |
| Channel | Fixed to `ESPNOW_CHANNEL` (default **1**). Both boards must match. |
| Payload | 2–3 MIDI bytes. Notes, CC, program change, channel pressure, pitch bend. |
| Not carried | SysEx (too long). Clock / Start / Stop (1-byte realtime is dropped on ESP-NOW receive). |
| Library quirk | Upstream `sendMidiMessage()` always broadcasts, even after `addPeer()`. This repo wraps it in `src/ESPNowMidi.h` so unicast actually sends to the peer MAC. |

On boot the USB serial monitor prints this board's MAC. Paste that MAC into the other board's `ESPNOW_PEER_MAC` if you want a point-to-point link instead of a mesh broadcast.

ESP-NOW does not normally deliver a board its own broadcasts, so the source check is mainly for **two-ended bridges**: without it, board B would take a packet from A and put it back on the air, and every extra node would multiply traffic.

## Hardware

Default pins (`src/config.h`): **GPIO 20 (RX) = MIDI IN**, **GPIO 21 (TX) = MIDI OUT**. Debug logs use native USB CDC (`Serial`). MIDI runs on `Serial0` (UART0), which is the native mapping for the Super Mini's labeled RX/TX pins.

### MIDI IN (optocoupler required)

Standard current-loop input. Example with a 6N138 / PC900V / H11L1:

- DIN pin 4 → 220 Ω → optocoupler LED anode
- DIN pin 5 → LED cathode
- Optocoupler output → GPIO 20 (RX)
- Follow the coupler datasheet for VCC / pull-up (3.3 V on the ESP32 side)

### MIDI OUT

- ESP32 3.3 V (or 5 V from the Mini's 5 V pin, preferred) → 220 Ω → DIN pin 4
- GPIO 21 (TX) → 220 Ω → DIN pin 5
- DIN pin 2 → GND (cable shield)

3.3 V out works with a lot of modern gear; 5 V is closer to the MIDI spec.

ESP32-C3 Super Mini user LED is GPIO 8 (active low) and flashes on each bridged message.

## Build and flash

### PlatformIO

```bash
pio run -t upload
pio device monitor
```

Need the Arduino-ESP32 3.x core (the library's ESP-NOW callbacks depend on it). This `platformio.ini` pulls [pioarduino](https://github.com/pioarduino/platform-espressif32) and uses the [ESP32-C3 Super Mini](https://www.espboards.dev/esp32/esp32-c3-super-mini/) board profile (`esp32-c3-devkitm-1`).

### Arduino IDE

1. Boards Manager: **esp32** by Espressif, 3.0 or newer.
2. Board: **ESP32C3 Dev Module**.
3. **USB CDC On Boot: Enabled**.
4. Flash Size: **4MB**. Super Mini clones usually need Flash Mode **DIO**.
5. Library Manager: install **ESP32_Host_MIDI**.
6. Copy `src/main.cpp`, `src/config.h`, and `src/ESPNowMidi.h` into a sketch folder (rename `main.cpp` to `your_sketch.ino`).

## Configure

Edit `src/config.h`:

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
ESP-NOW channel: 1 (configured 1)
UART MIDI: RX=GPIO20 TX=GPIO21 @ 31250 baud
Transports:
  [0] UART connected=1
  [1] ESP-NOW connected=1
UART -> NOW NoteOn ch=1 C4 vel=100
NOW -> UART NoteOff ch=1 C4 vel=0
stats  UART->NOW=12  NOW->UART=12
```

## License

MIT
