#pragma once

#include <ESPNowConnection.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <atomic>
#include <cstring>

// Arduino-ESP32 3.x default ESP-NOW PHY (same as libraries/ESP_NOW).
#ifndef MIDI_RELAY_ESPNOW_RATE
#define MIDI_RELAY_ESPNOW_RATE \
    { .phymode = WIFI_PHY_MODE_11G, .rate = WIFI_PHY_RATE_1M_L, .ersu = false, .dcm = false }
#endif

// ESPNowConnection::sendMidiMessage() always transmits to the broadcast
// address, even after addPeer(). This wrapper:
//   - pins the radio with a hidden SoftAP so STA cannot hop channels
//   - waits for Arduino 3.x STA/AP to start before esp_now_init()
//   - learns sender MACs (C3/ESP-NOW v2 is unreliable with unknown peers)
//   - sends a 2-byte beacon so RX can be verified without MIDI
class ESPNowMidi : public ESPNowConnection {
public:
    bool beginBroadcast(uint8_t channel) {
        useUnicast = false;
        return start(channel, nullptr);
    }

    bool beginUnicast(uint8_t channel, const uint8_t mac[6]) {
        if (!start(channel, mac)) {
            return false;
        }
        memcpy(peerMac, mac, 6);
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

    void task() override {
        rememberPendingPeer();
        Packet pkt;
        while (dequeue(pkt)) {
            dispatchMidiData(pkt.data, pkt.length);
        }
    }

    void sendBeacon() {
        if (!isConnected()) {
            return;
        }
        static const uint8_t kBeacon[2] = {kBeacon0, kBeacon1};
        static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        esp_now_send(kBroadcast, kBeacon, sizeof(kBeacon));
    }

    bool isUnicast() const { return useUnicast; }
    void getPeerMAC(uint8_t mac[6]) const { memcpy(mac, peerMac, 6); }

    uint32_t txOkCount() const { return txOk.load(); }
    uint32_t txFailCount() const { return txFail.load(); }
    uint32_t rxCount() const { return rxTotal.load(); }
    uint32_t beaconCount() const { return rxBeacon.load(); }
    uint32_t peerCount() const { return learnedPeers.load(); }

private:
    static constexpr uint8_t kBeacon0 = 0xF4;
    static constexpr uint8_t kBeacon1 = 0xA5;
    static constexpr int kQueueSize = 64;

    struct Packet {
        uint8_t data[4];
        size_t length;
    };

    bool useUnicast = false;
    uint8_t peerMac[6] = {};
    uint8_t radioChannel = 1;

    Packet rxQueue[kQueueSize] = {};
    volatile int qHead = 0;
    volatile int qTail = 0;
    portMUX_TYPE qMux = portMUX_INITIALIZER_UNLOCKED;

    uint8_t pendingSrc[6] = {};
    volatile bool havePendingSrc = false;

    static ESPNowMidi* self;
    static std::atomic<uint32_t> txOk;
    static std::atomic<uint32_t> txFail;
    static std::atomic<uint32_t> rxTotal;
    static std::atomic<uint32_t> rxBeacon;
    static std::atomic<uint32_t> learnedPeers;

    bool start(uint8_t channel, const uint8_t* unicastMac) {
        self = this;
        radioChannel = channel;
        if (!prepareRadio(channel)) {
            return false;
        }
        if (!begin(channel)) {
            return false;
        }
        lockChannel(channel);
        fixBroadcastPeer(channel);
        if (unicastMac != nullptr && !rememberPeer(unicastMac)) {
            return false;
        }
        hookCallbacks();
        return true;
    }

    static bool prepareRadio(uint8_t channel) {
        WiFi.persistent(false);
        WiFi.mode(WIFI_AP_STA);

        wifi_country_t country = {};
        memcpy(country.cc, "US", 2);
        country.schan = 1;
        country.nchan = 13;
        country.max_tx_power = 84;
        country.policy = WIFI_COUNTRY_POLICY_MANUAL;
        esp_wifi_set_country(&country);

        if (channel >= 1 && channel <= 13) {
            WiFi.setChannel(channel);
        }

        uint8_t mac[6] = {};
        WiFi.macAddress(mac);
        char ssid[20];
        snprintf(ssid, sizeof(ssid), "mr-%02X%02X%02X", mac[3], mac[4], mac[5]);
        // Hidden AP pins the C3 on this channel so STA cannot scan/hop.
        if (!WiFi.softAP(ssid, nullptr, channel, 1, 1)) {
            return false;
        }

        const uint32_t deadline = millis() + 2000;
        while ((!WiFi.STA.started() || !WiFi.AP.started()) &&
               (int32_t)(millis() - deadline) < 0) {
            delay(10);
        }
        if (!WiFi.STA.started() || !WiFi.AP.started()) {
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

    void fixBroadcastPeer(uint8_t channel) {
        uint8_t bcast[6];
        memset(bcast, 0xFF, 6);
        esp_now_peer_info_t peer = {};
        if (esp_now_get_peer(bcast, &peer) != ESP_OK) {
            return;
        }
        peer.channel = channel;
        peer.encrypt = false;
        peer.ifidx = WIFI_IF_STA;
        esp_now_mod_peer(&peer);
        applyRate(bcast);
    }

    bool rememberPeer(const uint8_t mac[6]) {
        if (mac == nullptr || esp_now_is_peer_exist(mac)) {
            return true;
        }
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac, 6);
        peer.channel = radioChannel;
        peer.encrypt = false;
        peer.ifidx = WIFI_IF_STA;
        const esp_err_t err = esp_now_add_peer(&peer);
        if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
            return false;
        }
        applyRate(mac);
        learnedPeers.fetch_add(1);
        return true;
    }

    static void applyRate(const uint8_t mac[6]) {
        esp_now_rate_config_t rate = MIDI_RELAY_ESPNOW_RATE;
        esp_now_set_peer_rate_config(mac, &rate);
    }

    void hookCallbacks() {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
        esp_now_register_send_cb(onSend);
#else
        esp_now_register_send_cb(onSendLegacy);
#endif
        esp_now_register_recv_cb(onRecv);
    }

    void rememberPendingPeer() {
        if (!havePendingSrc) {
            return;
        }
        uint8_t mac[6];
        memcpy(mac, pendingSrc, 6);
        havePendingSrc = false;
        rememberPeer(mac);
    }

    bool enqueue(const uint8_t* data, size_t length) {
        portENTER_CRITICAL(&qMux);
        const int next = (qHead + 1) % kQueueSize;
        if (next == qTail) {
            portEXIT_CRITICAL(&qMux);
            return false;
        }
        const size_t n = length > sizeof(rxQueue[0].data) ? sizeof(rxQueue[0].data) : length;
        memcpy(rxQueue[qHead].data, data, n);
        rxQueue[qHead].length = n;
        qHead = next;
        portEXIT_CRITICAL(&qMux);
        return true;
    }

    bool dequeue(Packet& pkt) {
        portENTER_CRITICAL(&qMux);
        if (qTail == qHead) {
            portEXIT_CRITICAL(&qMux);
            return false;
        }
        pkt = rxQueue[qTail];
        qTail = (qTail + 1) % kQueueSize;
        portEXIT_CRITICAL(&qMux);
        return true;
    }

    static bool isBeacon(const uint8_t* data, int len) {
        return len == 2 && data[0] == kBeacon0 && data[1] == kBeacon1;
    }

#if ESP_ARDUINO_VERSION_MAJOR >= 3
    static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
        if (self == nullptr || data == nullptr || len < 2) {
            return;
        }
        rxTotal.fetch_add(1);
        if (info != nullptr && info->src_addr != nullptr) {
            memcpy(self->pendingSrc, info->src_addr, 6);
            self->havePendingSrc = true;
        }
        if (isBeacon(data, len)) {
            rxBeacon.fetch_add(1);
            return;
        }
        if (len > 3) {
            return;
        }
        self->enqueue(data, static_cast<size_t>(len));
    }
#else
    static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
        if (self == nullptr || data == nullptr || len < 2) {
            return;
        }
        rxTotal.fetch_add(1);
        if (mac != nullptr) {
            memcpy(self->pendingSrc, mac, 6);
            self->havePendingSrc = true;
        }
        if (isBeacon(data, len)) {
            rxBeacon.fetch_add(1);
            return;
        }
        if (len > 3) {
            return;
        }
        self->enqueue(data, static_cast<size_t>(len));
    }
#endif

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

ESPNowMidi* ESPNowMidi::self = nullptr;
std::atomic<uint32_t> ESPNowMidi::txOk{0};
std::atomic<uint32_t> ESPNowMidi::txFail{0};
std::atomic<uint32_t> ESPNowMidi::rxTotal{0};
std::atomic<uint32_t> ESPNowMidi::rxBeacon{0};
std::atomic<uint32_t> ESPNowMidi::learnedPeers{0};
