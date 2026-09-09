#pragma once

#include <ESPNowConnection.h>
#include <WiFi.h>
#include <esp_now.h>
#include <cstring>

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

    void getPeerMAC(uint8_t mac[6]) const { memcpy(mac, peerMac, 6); }

private:
    bool useUnicast = false;
    uint8_t peerMac[6] = {};

    bool start(uint8_t channel) {
        // Arduino-ESP32 3.x: STA must be started before esp_now_init().
        WiFi.mode(WIFI_STA);
        if (channel >= 1 && channel <= 13) {
            WiFi.setChannel(channel, WIFI_SECOND_CHAN_NONE);
        }
        const uint32_t deadline = millis() + 2000;
        while (!WiFi.STA.started() && (int32_t)(millis() - deadline) < 0) {
            delay(10);
        }
        return begin(channel);
    }
};
