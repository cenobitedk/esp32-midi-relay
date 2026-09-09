#pragma once

// Hardware pins for a Seeed Studio XIAO ESP32C3.
// GPIO20/21 are UART0 (silk RX / TX). This sketch uses USB CDC for Serial,
// so those pins are available for MIDI DIN-5.
// https://www.espboards.dev/esp32/xiao-esp32c3/
#ifndef MIDI_RX_PIN
#define MIDI_RX_PIN 20  // D7 / RX; optocoupler output (MIDI IN)
#endif
#ifndef MIDI_TX_PIN
#define MIDI_TX_PIN 21  // D6 / TX; MIDI OUT driver (-1 to disable TX)
#endif

// Both ends of an ESP-NOW link must use the same 2.4 GHz channel (1-13).
#ifndef ESPNOW_CHANNEL
#define ESPNOW_CHANNEL 1
#endif

// Unicast peer MAC. Leave all zeros for broadcast (any board on ESPNOW_CHANNEL
// hears the MIDI). For a point-to-point link, set the other board's STA MAC
// printed at boot, e.g. 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
#ifndef ESPNOW_PEER_MAC
#define ESPNOW_PEER_MAC 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
#endif

// No onboard user LED (charge LED only). Wire an LED + resistor to D10
// (GPIO10, active high) and set this to 10, or leave -1 to disable.
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN -1
#endif
#ifndef STATUS_LED_ACTIVE_LOW
#define STATUS_LED_ACTIVE_LOW 0
#endif

// Log every bridged message on USB serial.
#ifndef MIDI_LOG_SERIAL
#define MIDI_LOG_SERIAL 1
#endif
