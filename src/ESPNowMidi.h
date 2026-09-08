#pragma once

#include <ESPNowConnection.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <atomic>
#include <cstring>

// ESPNowConnection::sendMidiMessage() always transmits to the broadcast
// address, even after addPeer(). This wrapper keeps the library transport
// for receive/parsing, and sends unicast when a peer MAC is configured.
//
// Arduino-ESP32 3.x needs the STA interface fully started (and the channel
// locked) before esp_now_init(), or send returns OK while the other board
// never receives. That is the official ESP-NOW Broadcast example sequence:
//   WiFi.mode(WIFI_STA); WiFi.setChannel(n); while (!WiFi.STA.started()) ...
class ESPNowMidi : public ESPNowConnection {
public:
    bool beginBroadcast(uint8_t channel) {
        useUnicast = false;
        if (!prepareRadio(channel)) {
            return false;
        }
        if (!begin(channel)) {
            return false;
        }
        lockChannel(channel);
        hookSendStatus();
        return true;
    }

    bool beginUnicast(uint8_t channel, const uint8_t mac[6]) {
        if (!prepareRadio(channel)) {
            return false;
        }
        if (!begin(channel)) {
            return false;
        }
        memcpy(peerMac, mac, 6);
        if (!addPeer(mac)) {
            return false;
        }
        useUnicast = true;
        lockChannel(channel);
        hookSendStatus();
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

    uint32_t txOkCount() const { return txOk.load(); }
    uint32_t txFailCount() const { return txFail.load(); }

private:
    bool useUnicast = false;
    uint8_t peerMac[6] = {};
    static std::atomic<uint32_t> txOk;
    static std::atomic<uint32_t> txFail;

    static bool prepareRadio(uint8_t channel) {
        WiFi.persistent(false);
        WiFi.mode(WIFI_STA);
        if (channel >= 1 && channel <= 13) {
            WiFi.setChannel(channel);
        }

        const uint32_t deadline = millis() + 2000;
        while (!WiFi.STA.started() && (int32_t)(millis() - deadline) < 0) {
            delay(10);
        }
        if (!WiFi.STA.started()) {
            return false;
        }

        WiFi.setSleep(false);
        WiFi.setTxPower(WIFI_POWER_19_5dBm);
        lockChannel(channel);
        return true;
    }

    static void lockChannel(uint8_t channel) {
        if (channel < 1 || channel > 13) {
            return;
        }
        esp_wifi_set_ps(WIFI_PS_NONE);
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    }

    static void hookSendStatus() {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
        esp_now_register_send_cb(onSend);
#else
        esp_now_register_send_cb(onSendLegacy);
#endif
    }

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
    static void onSend(const wifi_tx_info_t* info, esp_now_send_status_t status) {
        (void)info;
        if (status == ESP_NOW_SEND_SUCCESS) {
            txOk.fetch_add(1);
        } else {
            txFail.fetch_add(1);
        }
    }
#else
    static void onSendLegacy(const uint8_t* mac, esp_now_send_status_t status) {
        (void)mac;
        if (status == ESP_NOW_SEND_SUCCESS) {
            txOk.fetch_add(1);
        } else {
            txFail.fetch_add(1);
        }
    }
#endif
};

std::atomic<uint32_t> ESPNowMidi::txOk{0};
std::atomic<uint32_t> ESPNowMidi::txFail{0};
