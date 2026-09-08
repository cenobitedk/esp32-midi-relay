#pragma once

// Hardware pins for an ESP32-S3 Super Mini.
// GPIO43/44 are UART0 (silk TX/RX). This sketch uses USB CDC for Serial,
// so those pins are available for MIDI DIN-5. Do not use GPIO19/20 — they
// are USB D-/D+ on the S3.
#ifndef MIDI_RX_PIN
#define MIDI_RX_PIN 44   // labeled RX; optocoupler output (MIDI IN)
#endif
#ifndef MIDI_TX_PIN
#define MIDI_TX_PIN 43   // labeled TX; MIDI OUT driver (-1 to disable TX)
#endif

// Both ends of an ESP-NOW link must use the same 2.4 GHz channel (1-13).
// Channel 0 would leave whatever WiFi STA picked at boot, which is a
// common reason two boards "don't see" each other.
#ifndef ESPNOW_CHANNEL
#define ESPNOW_CHANNEL 1
#endif

// Unicast peer MAC. Leave all zeros for broadcast (any board on ESPNOW_CHANNEL
// hears the MIDI). For a point-to-point link, set the other board's MAC
// printed at boot, e.g. 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
#ifndef ESPNOW_PEER_MAC
#define ESPNOW_PEER_MAC 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
#endif

// Super Mini red LED + WS2812 share GPIO48, active high. Set to -1 to disable.
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 48
#endif
#ifndef STATUS_LED_ACTIVE_LOW
#define STATUS_LED_ACTIVE_LOW 0
#endif

// Log every bridged message on USB serial.
#ifndef MIDI_LOG_SERIAL
#define MIDI_LOG_SERIAL 1
#endif
