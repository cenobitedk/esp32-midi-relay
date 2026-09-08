#pragma once

// Hardware pins for a DOIT ESP32 DevKit V1 (ESP32-WROOM-32, 30-pin).
// UART0 (GPIO1 TX / GPIO3 RX) is wired to the USB-serial chip — leave it
// for Serial debug. MIDI uses UART2 on the silk RX2/TX2 pins.
// Board: https://www.espboards.dev/esp32/esp32doit-devkit-v1/
#ifndef MIDI_RX_PIN
#define MIDI_RX_PIN 16  // RX2; optocoupler output (MIDI IN)
#endif
#ifndef MIDI_TX_PIN
#define MIDI_TX_PIN 17  // TX2; MIDI OUT driver (-1 to disable TX)
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

// ESP-NOW TX power in 0.25 dBm units (wifi_power_t). 78 is
// WIFI_POWER_19_5dBm — normal for the WROOM-32 PCB antenna.
#ifndef ESPNOW_TX_POWER
#define ESPNOW_TX_POWER 78
#endif

// Onboard blue LED is GPIO2, active high. Set to -1 to disable.
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 2
#endif
#ifndef STATUS_LED_ACTIVE_LOW
#define STATUS_LED_ACTIVE_LOW 0
#endif

// Log every bridged message on USB serial.
#ifndef MIDI_LOG_SERIAL
#define MIDI_LOG_SERIAL 1
#endif
