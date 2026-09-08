#pragma once

#include <ESPNowConnection.h>
#include <esp_now.h>
#include <cstring>

// ESPNowConnection::sendMidiMessage() always transmits to the broadcast
// address, even after addPeer(). This wrapper keeps the library transport
// for receive/parsing, and sends unicast when a peer MAC is configured.
class ESPNowMidi : public ESPNowConnection {
public:
    bool beginBroadcast(uint8_t channel) {
        useUnicast = false;
        return begin(channel);
    }

    bool beginUnicast(uint8_t channel, const uint8_t mac[6]) {
        if (!begin(channel)) {
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
};
