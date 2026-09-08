#pragma once

// Hardware pins for an ESP32-C3 Super Mini.
// GPIO20/21 are UART0 by default (silk RX/TX). This sketch uses USB CDC
// for Serial, so those pins are available for MIDI DIN-5.
#ifndef MIDI_RX_PIN
#define MIDI_RX_PIN 20   // labeled RX; optocoupler output (MIDI IN)
#endif
#ifndef MIDI_TX_PIN
#define MIDI_TX_PIN 21   // labeled TX; MIDI OUT driver (-1 to disable TX)
#endif

// Both ends of an ESP-NOW link must use the same 2.4 GHz channel (1-13).
// Channel 0 would leave whatever WiFi STA picked at boot, which is a
// common reason two boards "don't see" each other.
#ifndef ESPNOW_CHANNEL
#define ESPNOW_CHANNEL 1
#endif

// Unicast peer MAC. Leave all zeros for broadcast (any board on ESPNOW_CHANNEL
// hears the MIDI). For a point-to-point link, set the other board's STA MAC
// printed at boot, e.g. 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
#ifndef ESPNOW_PEER_MAC
#define ESPNOW_PEER_MAC 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
#endif

// ESP-NOW TX power in 0.25 dBm units (wifi_power_t). Super Mini clones often
// fail to receive at centimetre range with the default ~19.5 dBm — the other
// board's LNA saturates. -4 is WIFI_POWER_MINUS_1dBm. Raise to 8 (2 dBm) or
// 34 (8.5 dBm) if you need more range after RX is working.
#ifndef ESPNOW_TX_POWER
#define ESPNOW_TX_POWER -4
#endif

// Super Mini user LED is GPIO8, active low. Set to -1 to disable.
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 8
#endif
#ifndef STATUS_LED_ACTIVE_LOW
#define STATUS_LED_ACTIVE_LOW 1
#endif

// Log every bridged message on USB serial.
#ifndef MIDI_LOG_SERIAL
#define MIDI_LOG_SERIAL 1
#endif
