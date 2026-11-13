#pragma once

#include <ESPNowConnection.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <cstring>

#include "config.h"

// ESPNowConnection::sendMidiMessage() always transmits to the broadcast
// address, even after addPeer(). This wrapper keeps the library transport
// for receive/parsing, and sends unicast when a peer MAC is configured.
class ESPNowMidi : public ESPNowConnection {
public:
    bool beginBroadcast(uint8_t channel) {
        useUnicast = false;
        return start(channel);
    }

    bool beginUnicast(uint8_t channel, const uint8_t mac[6]) {
        if (!start(channel)) {
            return false;
        }
        memcpy(peerMac, mac, 6);
        if (!addPeer(mac)) {
            return false;
        }
        useUnicast = true;
        return true;
    }

    bool sendMidiMessage(const uint8_t* data, size_t length) override {
        if (!isConnected() || data == nullptr || length == 0 || length > 3) {
            return false;
        }
        static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        const uint8_t* dest = useUnicast ? peerMac : kBroadcast;
        return esp_now_send(dest, data, length) == ESP_OK;
    }

    bool isUnicast() const { return useUnicast; }

    // True when the radio accepted ESPNOW_COUNTRY. Channel 12-13 stays
    // unavailable if this is false.
    bool radioCountryOk() const { return countryOk; }

    void getPeerMAC(uint8_t mac[6]) const { memcpy(mac, peerMac, 6); }

private:
    bool useUnicast = false;
    bool countryOk = false;
    uint8_t peerMac[6] = {};

    bool start(uint8_t channel) {
        // Arduino-ESP32 3.x: STA must be started before esp_now_init().
        WiFi.mode(WIFI_STA);
        const uint32_t deadline = millis() + 2000;
        while (!WiFi.STA.started() && (int32_t)(millis() - deadline) < 0) {
            delay(10);
        }

        // Country before channel. The chip default only transmits on 1-11,
        // and a later setChannel(13) is ignored until the country allows it.
        // ieee80211d off: these boards never join an access point, so the
        // country must not be replaced by one.
        countryOk = esp_wifi_set_country_code(ESPNOW_COUNTRY, false) == ESP_OK;

        if (channel >= 1 && channel <= 13) {
            WiFi.setChannel(channel, WIFI_SECOND_CHAN_NONE);
        }
        return begin(channel);
    }
};
