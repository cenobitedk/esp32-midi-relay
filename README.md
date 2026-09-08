# UART DIN-5 ↔ ESP-NOW MIDI bridge (DOIT ESP32 DevKit V1)

Firmware for a **DOIT ESP32 DevKit V1** (ESP32-WROOM-32, 30-pin) that bridges standard 5-pin DIN MIDI to ESP-NOW using [ESP32_Host_MIDI](https://github.com/sauloverissimo/ESP32_Host_MIDI). Two boards running this sketch become a wireless MIDI cable.

Board profile: [esp32doit-devkit-v1](https://www.espboards.dev/esp32/esp32doit-devkit-v1/). Generic 30-pin DevKit V1 clones with a WROOM-32 and a PCB antenna are the same layout.

## How transports actually work

The library does **not** route MIDI by itself.

1. `UARTConnection` and the ESP-NOW wrapper are both `MIDITransport`s.
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
DIN-5 IN  --> UART2 (GPIO16) --> queue --> ESP-NOW radio
DIN-5 OUT <-- UART2 (GPIO17) <-- queue <-- ESP-NOW radio
```

## ESP-NOW details

Arduino-ESP32 3.x needs Wi-Fi fully started before `esp_now_init()`. This firmware uses **WIFI_AP_STA**: a hidden SoftAP pins the channel and keeps RX awake; ESP-NOW itself uses `WIFI_IF_STA`.

| Topic | Behavior in this project |
|--------|--------------------------|
| Addressing | Broadcast by default (`FF:FF:FF:FF:FF:FF`). Optional unicast via `ESPNOW_PEER_MAC` — use the other board's **STA MAC** printed at boot. |
| Channel | Fixed to `ESPNOW_CHANNEL` (default **1**). Both boards must match. |
| TX power | `ESPNOW_TX_POWER` in `src/config.h`, default **19.5 dBm** (WROOM-32 PCB antenna). |
| Payload | 2–3 MIDI bytes. Notes, CC, program change, channel pressure, pitch bend. |
| Not carried | SysEx (too long). Clock / Start / Stop (1-byte realtime is dropped on ESP-NOW receive). |
| Keepalive | 1 Hz 2-byte beacon (`F4 A5`) so you can confirm the radios hear each other without MIDI. |

Flash **both** boards from the same build. Watch `beacon` in the stats line: if it stays 0, the radios are not linked yet (`tx_ok` on broadcast does **not** prove anyone heard it).

`midiHandler.addTransport(&espNow)` wires receive into the queue. Do **not** copy `espNow.setMidiCallback(...)` from the jam example.

## Hardware

Default pins (`src/config.h`): **GPIO 16 (RX2) = MIDI IN**, **GPIO 17 (TX2) = MIDI OUT**. Debug logs use UART0 through the onboard USB-serial chip (`Serial` on Micro-USB). Do **not** wire MIDI to GPIO1/3 — those pins are the USB console.

The onboard blue LED is **GPIO 2** (active high) and flashes on each bridged message.

### MIDI IN (optocoupler required)

Standard current-loop input. Example with a 6N138 / PC900V / H11L1:

- DIN pin 4 → 220 Ω → optocoupler LED anode
- DIN pin 5 → LED cathode
- Optocoupler output → GPIO 16 (RX2)
- Follow the coupler datasheet for VCC / pull-up (3.3 V on the ESP32 side)

### MIDI OUT

- ESP32 5 V (`VIN` / USB) → 220 Ω → DIN pin 4
- GPIO 17 (TX2) → 220 Ω → DIN pin 5
- DIN pin 2 → GND (cable shield)

5 V out is closer to the MIDI spec; 3.3 V works with a lot of modern gear.

If the board brownout-resets when Wi-Fi transmits, use a short USB cable straight into the computer (not a hub).

## Build and flash

### PlatformIO

```bash
pio run -e esp32doit-devkit-v1 -t upload
pio device monitor
```

Need Arduino-ESP32 3.x. This `platformio.ini` pulls [pioarduino](https://github.com/pioarduino/platform-espressif32) and uses `board = esp32dev` as on the [DevKit V1 page](https://www.espboards.dev/esp32/esp32doit-devkit-v1/).

If the port does not appear, install a CP210x or CH340 driver depending on the USB-serial chip next to Micro-USB. Hold **BOOT** while plugging in if needed.

### Arduino IDE

1. Boards Manager: **esp32** by Espressif, 3.0 or newer.
2. Board: **ESP32 Dev Module**.
3. Flash Size: **4MB**. Flash Mode: **DIO**. Upload Speed: **921600**.
4. Library Manager: install **ESP32_Host_MIDI**.
5. Copy `src/main.cpp`, `src/config.h`, and `src/ESPNowMidi.h` into a sketch folder (rename `main.cpp` to `your_sketch.ino`).

## Configure

Edit `src/config.h`:

```cpp
#define MIDI_RX_PIN 16
#define MIDI_TX_PIN 17
#define ESPNOW_CHANNEL 1
#define ESPNOW_PEER_MAC 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // broadcast
#define ESPNOW_TX_POWER 78  // WIFI_POWER_19_5dBm
```

Use two boards on the same channel. For unicast, set each board's `ESPNOW_PEER_MAC` to the **other** board's printed **STA MAC**.

The stats line prints radio health:

- `tx_ok` / `tx_fail` — ESP-NOW send callback (broadcast `tx_ok` does **not** prove anyone heard it)
- `rx` — any ESP-NOW packet that reached this board
- `beacon` — 1 Hz keepalive from the other board. If this stays 0, the radios are not linked yet
- `peers` — remote MACs learned from received packets
- `rssi` — last received packet

## Serial log

```
UART DIN-5 <-> ESP-NOW MIDI bridge
ESP-NOW mode: broadcast
ESP-NOW version 2  if=STA  channel 1
This board STA MAC (use this for ESPNOW_PEER_MAC) AA:BB:CC:DD:EE:FF
This board AP MAC AA:BB:CC:DD:EE:00
WiFi STA started=1  AP started=1  ESP-NOW channel: 1 (configured 1)
TX power: 78 (quarter-dBm)
UART MIDI: RX=GPIO16 TX=GPIO17 @ 31250 baud (UART2)
Transports:
  [0] UART connected=1
  [1] ESP-NOW connected=1
ESP-NOW first RX  src=AA:BB:CC:DD:EE:11  len=2  rssi=-48 dBm
UART -> NOW NoteOn ch=1 C4 vel=100
NOW -> UART NoteOff ch=1 C4 vel=0
stats  UART->NOW=12  NOW->UART=12  ch=1  tx_ok=12  tx_fail=0  rx=24  beacon=12  peers=1  rssi=-48
```

## License

MIT
