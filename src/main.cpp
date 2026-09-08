#include <Arduino.h>
#include <ESP32_Host_MIDI.h>
#include <UARTConnection.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "config.h"
#include "ESPNowMidi.h"

// ---------------------------------------------------------------------------
// UART DIN-5  <----bridge---->  ESP-NOW
//
// ESP32_Host_MIDI does not route MIDI between transports. Each transport
// only delivers into midiHandler's event queue, tagged with event.source.
// This sketch forwards every new event to the *other* transport so a
// message that arrived on UART never goes back out UART, and a message
// that arrived on ESP-NOW never goes back out ESP-NOW.
// ---------------------------------------------------------------------------

UARTConnection uartMIDI;
ESPNowMidi espNow;

static int lastEventIndex = -1;
static uint32_t uartToNowCount = 0;
static uint32_t nowToUartCount = 0;
static uint32_t lastStatusMs = 0;
static uint32_t lastBeaconMs = 0;
static uint32_t ledOffMs = 0;

static bool peerMacConfigured(const uint8_t mac[6]) {
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0) {
            return true;
        }
    }
    return false;
}

static void printMac(const char* label, const uint8_t mac[6]) {
    Serial.printf("%s %02X:%02X:%02X:%02X:%02X:%02X\n",
                  label, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void setLed(bool on) {
#if STATUS_LED_PIN >= 0
#if STATUS_LED_ACTIVE_LOW
    digitalWrite(STATUS_LED_PIN, on ? LOW : HIGH);
#else
    digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
#endif
#else
    (void)on;
#endif
}

static void pulseLed() {
#if STATUS_LED_PIN >= 0
    setLed(true);
    ledOffMs = millis() + 25;
#endif
}

// Rebuild a MIDI 1.0 channel message from a parsed queue event.
// ESP-NOW in this library only carries 2-3 byte packets (no SysEx, no
// 1-byte realtime such as Clock/Start/Stop).
static bool encodeChannelMessage(const MIDIEventData& ev, uint8_t* msg, size_t* len) {
    const uint8_t status = static_cast<uint8_t>(ev.statusCode) | (ev.channel0 & 0x0F);

    switch (ev.statusCode) {
        case MIDI_NOTE_ON:
        case MIDI_NOTE_OFF:
        case MIDI_POLY_PRESSURE:
        case MIDI_CONTROL_CHANGE:
            msg[0] = status;
            msg[1] = ev.noteNumber;
            msg[2] = ev.velocity7;
            *len = 3;
            return true;

        case MIDI_PROGRAM_CHANGE:
            msg[0] = status;
            msg[1] = ev.noteNumber;
            *len = 2;
            return true;

        case MIDI_CHANNEL_PRESSURE:
            // MIDIHandler stores aftertouch in velocity7, not noteNumber.
            msg[0] = status;
            msg[1] = ev.velocity7;
            *len = 2;
            return true;

        case MIDI_PITCH_BEND:
            msg[0] = status;
            msg[1] = static_cast<uint8_t>(ev.pitchBend14 & 0x7F);
            msg[2] = static_cast<uint8_t>((ev.pitchBend14 >> 7) & 0x7F);
            *len = 3;
            return true;

        default:
            return false;
    }
}

static MIDITransport* destinationFor(const MIDIEventData& ev) {
    if (ev.source == &uartMIDI) {
        return &espNow;
    }
    if (ev.source == &espNow) {
        return &uartMIDI;
    }
    return nullptr;
}

static void logEvent(const MIDIEventData& ev, const char* arrow) {
#if MIDI_LOG_SERIAL
    const char* src = ev.source ? ev.source->name() : "?";
    char noteBuf[8];

    switch (ev.statusCode) {
        case MIDI_NOTE_ON:
        case MIDI_NOTE_OFF:
            MIDIHandler::noteWithOctave(ev.noteNumber, noteBuf, sizeof(noteBuf));
            Serial.printf("%s %s ch=%u %s vel=%u\n",
                          arrow, MIDIHandler::statusName(ev.statusCode),
                          ev.channel0 + 1, noteBuf, ev.velocity7);
            break;
        case MIDI_CONTROL_CHANGE:
            Serial.printf("%s CC ch=%u cc=%u val=%u\n",
                          arrow, ev.channel0 + 1, ev.noteNumber, ev.velocity7);
            break;
        case MIDI_PROGRAM_CHANGE:
            Serial.printf("%s PC ch=%u prog=%u\n",
                          arrow, ev.channel0 + 1, ev.noteNumber);
            break;
        case MIDI_PITCH_BEND:
            Serial.printf("%s PitchBend ch=%u pb=%u\n",
                          arrow, ev.channel0 + 1, ev.pitchBend14);
            break;
        default:
            Serial.printf("%s %s from %s\n",
                          arrow, MIDIHandler::statusName(ev.statusCode), src);
            break;
    }
#else
    (void)ev;
    (void)arrow;
#endif
}

void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println();
    Serial.println("UART DIN-5 <-> ESP-NOW MIDI bridge");

#if STATUS_LED_PIN >= 0
    pinMode(STATUS_LED_PIN, OUTPUT);
    setLed(false);
#endif

    // GPIO20/21 are UART0 on the C3 Super Mini (silk RX/TX). Serial is USB
    // CDC (the USB-C port), so MIDI uses Serial0 and debug logs stay on USB.
    if (!uartMIDI.begin(Serial0, MIDI_RX_PIN, MIDI_TX_PIN)) {
        Serial.println("UART MIDI begin() failed");
    }
    midiHandler.addTransport(&uartMIDI);

    const uint8_t peerMac[6] = {ESPNOW_PEER_MAC};
    bool ok = false;
    if (peerMacConfigured(peerMac)) {
        ok = espNow.beginUnicast(ESPNOW_CHANNEL, peerMac);
        printMac("ESP-NOW unicast peer", peerMac);
    } else {
        ok = espNow.beginBroadcast(ESPNOW_CHANNEL);
        Serial.println("ESP-NOW mode: broadcast");
    }
    if (!ok) {
        Serial.println("ESP-NOW begin() failed — check radio init");
    }
    // midiHandler.addTransport() already calls setMidiCallback() so incoming
    // ESP-NOW bytes are parsed into the shared queue. The jam example sets
    // that callback itself because it does not use MIDIHandler at all.
    midiHandler.addTransport(&espNow);

    MIDIHandlerConfig cfg;
    cfg.maxEvents = 50;
    midiHandler.begin(cfg);

    uint8_t localMac[6] = {};
    espNow.getLocalMAC(localMac);
    printMac("This board MAC", localMac);

    uint8_t channel = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&channel, &second);
    Serial.printf("WiFi STA started=%d  AP started=%d  ESP-NOW channel: %u (configured %u)\n",
                  WiFi.STA.started(), WiFi.AP.started(), channel, ESPNOW_CHANNEL);
    if (channel != ESPNOW_CHANNEL) {
        Serial.println("WARNING: radio channel does not match ESPNOW_CHANNEL — boards will not see each other");
    }
    Serial.printf("UART MIDI: RX=GPIO%d TX=GPIO%d @ 31250 baud\n",
                  MIDI_RX_PIN, MIDI_TX_PIN);

    Serial.println("Transports:");
    for (int i = 0; i < midiHandler.getTransportCount(); i++) {
        MIDITransport* t = midiHandler.getTransport(i);
        Serial.printf("  [%d] %s connected=%d\n", i, t->name(), t->isConnected());
    }
    Serial.println("Bridge running. UART <-> ESP-NOW, no echo to source.");
}

void loop() {
    midiHandler.task();

    if (millis() - lastBeaconMs >= 1000) {
        lastBeaconMs = millis();
        espNow.sendBeacon();
    }

    const auto& queue = midiHandler.getQueue();
    for (const auto& ev : queue) {
        if (ev.index <= lastEventIndex) {
            continue;
        }
        lastEventIndex = ev.index;

        MIDITransport* dest = destinationFor(ev);
        if (dest == nullptr) {
            continue;
        }

        uint8_t msg[3];
        size_t len = 0;
        if (!encodeChannelMessage(ev, msg, &len)) {
            continue;
        }

        if (!dest->sendMidiMessage(msg, len)) {
#if MIDI_LOG_SERIAL
            Serial.printf("send failed -> %s\n", dest->name());
#endif
            continue;
        }

        if (dest == &espNow) {
            uartToNowCount++;
            logEvent(ev, "UART -> NOW");
        } else {
            nowToUartCount++;
            logEvent(ev, "NOW -> UART");
        }
        pulseLed();
    }

#if STATUS_LED_PIN >= 0
    if (ledOffMs != 0 && (int32_t)(millis() - ledOffMs) >= 0) {
        setLed(false);
        ledOffMs = 0;
    }
#endif

    if (millis() - lastStatusMs >= 5000) {
        lastStatusMs = millis();
        uint8_t liveChannel = 0;
        wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
        esp_wifi_get_channel(&liveChannel, &second);
        Serial.printf(
            "stats  UART->NOW=%lu  NOW->UART=%lu  ch=%u  tx_ok=%lu  tx_fail=%lu  rx=%lu  beacon=%lu  peers=%lu\n",
            (unsigned long)uartToNowCount,
            (unsigned long)nowToUartCount,
            (unsigned)liveChannel,
            (unsigned long)espNow.txOkCount(),
            (unsigned long)espNow.txFailCount(),
            (unsigned long)espNow.rxCount(),
            (unsigned long)espNow.beaconCount(),
            (unsigned long)espNow.peerCount());
    }
}
