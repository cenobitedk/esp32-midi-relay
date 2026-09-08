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

This firmware does **not** use `ESPNowConnection::begin()` as-is. On Arduino-ESP32 3.x a disconnected STA can report ESP-NOW send success while the receiver is asleep, and ESP32-C3 Super Mini boards often fail to receive at centimetre range if TX power is left at ~19.5 dBm (the other radio's front-end saturates; many clones also have a ceramic antenna sitting next to the crystal).

| Topic | Behavior in this project |
|--------|--------------------------|
| Wi-Fi | Hidden open SoftAP **plus** STA. SoftAP pins the channel and keeps RX awake. ESP-NOW itself uses `WIFI_IF_STA`. |
| Addressing | Broadcast by default (`FF:FF:FF:FF:FF:FF`). Optional unicast via `ESPNOW_PEER_MAC` — use the other board's **STA MAC** printed at boot. |
| Channel | Fixed to `ESPNOW_CHANNEL` (default **1**). Both boards must match. |
| TX power | `ESPNOW_TX_POWER` in `src/config.h`, default **-1 dBm**. Raise after RX works if you need more range. |
| PHY | 802.11b 1 Mbps. |
| Payload | 2–3 MIDI bytes. Notes, CC, program change, channel pressure, pitch bend. |
| Not carried | SysEx (too long). Clock / Start / Stop (1-byte realtime is dropped on ESP-NOW receive). |
| Keepalive | 1 Hz 2-byte beacon (`F4 A5`) so you can confirm the radios hear each other without MIDI. |

On boot the USB serial monitor prints this board's STA MAC (use that for unicast) and AP MAC. Flash **both** boards from the same build. ESP-NOW does not normally deliver a board its own broadcasts, so the source check in the bridge is mainly so a received packet is not put back on the air.

`midiHandler.addTransport(&espNow)` is what wires receive into the queue. Do **not** copy `espNow.setMidiCallback(...)` from the jam example — that sketch does not use MIDIHandler, and replacing the callback would stop events from reaching the bridge.

If one board logs `UART -> NOW` but the other stays at `NOW->UART=0`:

1. Flash **both** boards with this firmware. Mixed builds will not link.
2. Boot lines must show the same `ESP-NOW channel` and `STA started=1  AP started=1`.
3. Watch `beacon` in the stats line. Both boards send a packet every second; if `beacon` stays 0, the radios are not linked yet (`tx_ok` on broadcast does **not** prove anyone heard it).
4. 1–2 cm is too close at high TX power. This build defaults to -1 dBm so that distance can work. After `beacon` climbs, you can move them apart and raise `ESPNOW_TX_POWER` (8 = 2 dBm, 34 = 8.5 dBm).
5. First successful packet logs `ESP-NOW first RX` with source MAC and RSSI. RSSI around 0 dBm still means “too close / too loud”; -40 to -70 dBm is healthy.

The stats line prints radio health:

- `tx_ok` / `tx_fail` — ESP-NOW send callback (broadcast `tx_ok` does **not** prove anyone heard it)
- `rx` — any ESP-NOW packet that reached this board
- `beacon` — 1 Hz keepalive from the other board
- `peers` — remote MACs learned from received packets
- `rssi` — last received packet (0 until the first one)

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
#define ESPNOW_TX_POWER -4  // WIFI_POWER_MINUS_1dBm; raise after RX works
```

Use two boards on the same channel. For unicast, set each board's `ESPNOW_PEER_MAC` to the **other** board's printed **STA MAC**.

If one board logs `UART -> NOW` but the other stays at `NOW->UART=0`, check that both boot lines show the same `ESP-NOW channel`, `WiFi STA started=1`, and `AP started=1`. Super Mini antennas are weak **and** overload at a couple of centimetres if TX power is high — this firmware starts at -1 dBm.

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
TX power: -4 (quarter-dBm; Super Mini needs this low at 1-2cm)
UART MIDI: RX=GPIO20 TX=GPIO21 @ 31250 baud
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
