#pragma once

#include <MIDITransport.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <atomic>
#include <cstring>

#include "config.h"

#ifndef ESPNOW_TX_POWER
#define ESPNOW_TX_POWER -4
#endif

// ESP32-C3 Super Mini ESP-NOW:
//   - Disconnected STA can TX "success" while RX is asleep. A hidden SoftAP
//     keeps the radio up; ESP-NOW itself stays on WIFI_IF_STA (Arduino 3.x).
//   - Cheap Super Mini antennas + high TX power saturate RX at a few cm.
//     Default power is WIFI_POWER_MINUS_1dBm (see ESPNOW_TX_POWER).
class ESPNowMidi : public MIDITransport {
public:
    const char* name() const override { return "ESP-NOW"; }
    bool isConnected() const override { return initialized; }

    bool beginBroadcast(uint8_t channel) {
        useUnicast = false;
        return start(channel, nullptr);
    }

    bool beginUnicast(uint8_t channel, const uint8_t mac[6]) {
        if (!start(channel, nullptr)) {
            return false;
        }
        memcpy(peerMac, mac, 6);
        if (!rememberPeer(mac)) {
            Serial.println("ESP-NOW add unicast peer failed");
            return false;
        }
        useUnicast = true;
        return true;
    }

    bool sendMidiMessage(const uint8_t* data, size_t length) override {
        if (!initialized || data == nullptr || length == 0 || length > 3) {
            return false;
        }
        const uint8_t* dest = useUnicast ? peerMac : kBroadcast;
        return esp_now_send(dest, data, length) == ESP_OK;
    }

    void task() override {
        rememberPendingPeer();
        if (haveFirstRx && !printedFirstRx) {
            printedFirstRx = true;
            Serial.printf("ESP-NOW first RX  src=%02X:%02X:%02X:%02X:%02X:%02X  len=%d  rssi=%d dBm\n",
                          firstRxMac[0], firstRxMac[1], firstRxMac[2],
                          firstRxMac[3], firstRxMac[4], firstRxMac[5],
                          firstRxLen, firstRxRssi);
        }
        Packet pkt;
        while (dequeue(pkt)) {
            dispatchMidiData(pkt.data, pkt.length);
        }
    }

    void sendBeacon() {
        if (!initialized) {
            return;
        }
        static const uint8_t kBeacon[2] = {kBeacon0, kBeacon1};
        esp_now_send(kBroadcast, kBeacon, sizeof(kBeacon));
    }

    bool isUnicast() const { return useUnicast; }

    // ESP-NOW runs on the STA interface, so unicast peers must use this MAC.
    void getLocalMAC(uint8_t mac[6]) const {
        WiFi.macAddress(mac);
    }

    void getApMAC(uint8_t mac[6]) const {
        WiFi.softAPmacAddress(mac);
    }

    void getPeerMAC(uint8_t mac[6]) const { memcpy(mac, peerMac, 6); }

    int8_t lastRssi() const { return lastRxRssi; }
    uint32_t txOkCount() const { return txOk.load(); }
    uint32_t txFailCount() const { return txFail.load(); }
    uint32_t rxCount() const { return rxTotal.load(); }
    uint32_t beaconCount() const { return rxBeacon.load(); }
    uint32_t peerCount() const { return learnedPeers.load(); }

private:
    static constexpr uint8_t kBeacon0 = 0xF4;
    static constexpr uint8_t kBeacon1 = 0xA5;
    static constexpr int kQueueSize = 64;
    static constexpr uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    struct Packet {
        uint8_t data[4];
        size_t length;
    };

    bool initialized = false;
    bool useUnicast = false;
    uint8_t peerMac[6] = {};
    uint8_t radioChannel = 1;

    Packet rxQueue[kQueueSize] = {};
    volatile int qHead = 0;
    volatile int qTail = 0;
    portMUX_TYPE qMux = portMUX_INITIALIZER_UNLOCKED;

    uint8_t pendingSrc[6] = {};
    volatile bool havePendingSrc = false;

    uint8_t firstRxMac[6] = {};
    volatile int firstRxLen = 0;
    volatile int8_t firstRxRssi = 0;
    volatile int8_t lastRxRssi = 0;
    volatile bool haveFirstRx = false;
    bool printedFirstRx = false;

    static ESPNowMidi* self;
    static std::atomic<uint32_t> txOk;
    static std::atomic<uint32_t> txFail;
    static std::atomic<uint32_t> rxTotal;
    static std::atomic<uint32_t> rxBeacon;
    static std::atomic<uint32_t> learnedPeers;

    static bool waitStarted(uint32_t timeoutMs) {
        const uint32_t deadline = millis() + timeoutMs;
        while ((int32_t)(millis() - deadline) < 0) {
            if (WiFi.STA.started() && WiFi.AP.started()) {
                return true;
            }
            delay(10);
        }
        return WiFi.STA.started() && WiFi.AP.started();
    }

    static void lockChannel(uint8_t channel) {
        if (channel < 1 || channel > 13) {
            return;
        }
        WiFi.setChannel(channel, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    }

    static void applyRate(const uint8_t* mac) {
        // 1 Mbps 802.11b is the most reliable PHY on Super Mini ceramics.
        esp_now_rate_config_t rate = {};
        rate.phymode = WIFI_PHY_MODE_11B;
        rate.rate = WIFI_PHY_RATE_1M_L;
        rate.ersu = false;
        rate.dcm = false;
        const esp_err_t err = esp_now_set_peer_rate_config(mac, &rate);
        if (err != ESP_OK) {
            Serial.printf("esp_now_set_peer_rate_config: %s\n", esp_err_to_name(err));
        }
    }

    bool start(uint8_t channel, const uint8_t* /*unused*/) {
        self = this;
        radioChannel = channel;

        WiFi.persistent(false);
        WiFi.disconnect(true, true);
        delay(50);

        // AP+STA: SoftAP holds the channel and keeps RX awake; ESP-NOW TX/RX
        // uses the STA interface, matching Arduino 3.x broadcast examples.
        WiFi.mode(WIFI_AP_STA);

        wifi_country_t country = {};
        memcpy(country.cc, "US", 2);
        country.schan = 1;
        country.nchan = 13;
        country.max_tx_power = 84;
        country.policy = WIFI_COUNTRY_POLICY_MANUAL;
        esp_wifi_set_country(&country);

        lockChannel(channel);

        uint8_t staMac[6] = {};
        WiFi.macAddress(staMac);
        char ssid[20];
        snprintf(ssid, sizeof(ssid), "mr-%02X%02X%02X", staMac[3], staMac[4], staMac[5]);

        if (!WiFi.softAP(ssid, nullptr, channel, 1, 4, false, WIFI_AUTH_OPEN)) {
            Serial.println("ESP-NOW SoftAP start failed");
            return false;
        }

        // SoftAP beacons at close range also slam the other Super Mini's LNA.
        wifi_config_t apCfg = {};
        if (esp_wifi_get_config(WIFI_IF_AP, &apCfg) == ESP_OK) {
            apCfg.ap.beacon_interval = 1000;
            esp_wifi_set_config(WIFI_IF_AP, &apCfg);
        }

        if (!waitStarted(2000)) {
            Serial.printf("ESP-NOW WiFi not started  STA=%d AP=%d\n",
                          WiFi.STA.started(), WiFi.AP.started());
            return false;
        }

        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);
        lockChannel(channel);

        if (!WiFi.setTxPower(static_cast<wifi_power_t>(ESPNOW_TX_POWER))) {
            Serial.println("ESP-NOW setTxPower failed");
        }

        esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B);
        esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B);
#ifdef WIFI_PHY_RATE_1M_L
        esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_1M_L);
#endif

        const esp_err_t initErr = esp_now_init();
        if (initErr != ESP_OK) {
            Serial.printf("esp_now_init: %s\n", esp_err_to_name(initErr));
            return false;
        }

        uint32_t version = 0;
        esp_now_get_version(&version);
        Serial.printf("ESP-NOW version %u  if=STA  channel %u\n", version, channel);

        if (esp_now_register_recv_cb(onRecv) != ESP_OK) {
            Serial.println("esp_now_register_recv_cb failed");
            return false;
        }
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
        if (esp_now_register_send_cb(onSend) != ESP_OK) {
            Serial.println("esp_now_register_send_cb failed");
            return false;
        }
#else
        if (esp_now_register_send_cb(onSendLegacy) != ESP_OK) {
            Serial.println("esp_now_register_send_cb failed");
            return false;
        }
#endif

        // channel 0 = current home channel. A stale non-zero channel is a
        // silent TX-success / RX-nothing failure on IDF 5.x.
        esp_now_peer_info_t bcast = {};
        memcpy(bcast.peer_addr, kBroadcast, 6);
        bcast.channel = 0;
        bcast.encrypt = false;
        bcast.ifidx = WIFI_IF_STA;
        const esp_err_t peerErr = esp_now_add_peer(&bcast);
        if (peerErr != ESP_OK && peerErr != ESP_ERR_ESPNOW_EXIST) {
            Serial.printf("esp_now_add_peer broadcast: %s\n", esp_err_to_name(peerErr));
            return false;
        }
        applyRate(kBroadcast);

        lockChannel(channel);
        initialized = true;
        dispatchConnected();
        return true;
    }

    bool rememberPeer(const uint8_t mac[6]) {
        if (mac == nullptr || esp_now_is_peer_exist(mac)) {
            return true;
        }
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac, 6);
        peer.channel = 0;
        peer.encrypt = false;
        peer.ifidx = WIFI_IF_STA;
        const esp_err_t err = esp_now_add_peer(&peer);
        if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
            Serial.printf("esp_now_add_peer: %s\n", esp_err_to_name(err));
            return false;
        }
        applyRate(mac);
        learnedPeers.fetch_add(1);
        return true;
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
#else
    static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
#endif
        if (self == nullptr || data == nullptr || len <= 0) {
            return;
        }
        rxTotal.fetch_add(1);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        if (info != nullptr && info->src_addr != nullptr) {
            memcpy(self->pendingSrc, info->src_addr, 6);
            self->havePendingSrc = true;
            if (info->rx_ctrl != nullptr) {
                self->lastRxRssi = info->rx_ctrl->rssi;
            }
            if (!self->haveFirstRx) {
                memcpy(self->firstRxMac, info->src_addr, 6);
                self->firstRxLen = len;
                self->firstRxRssi = self->lastRxRssi;
                self->haveFirstRx = true;
            }
        }
#else
        if (mac != nullptr) {
            memcpy(self->pendingSrc, mac, 6);
            self->havePendingSrc = true;
            if (!self->haveFirstRx) {
                memcpy(self->firstRxMac, mac, 6);
                self->firstRxLen = len;
                self->haveFirstRx = true;
            }
        }
#endif
        if (isBeacon(data, len)) {
            rxBeacon.fetch_add(1);
            return;
        }
        if (len < 2 || len > 3) {
            return;
        }
        self->enqueue(data, static_cast<size_t>(len));
    }

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
    static void onSend(const wifi_tx_info_t* info, esp_now_send_status_t status) {
        (void)info;
#else
    static void onSendLegacy(const uint8_t* mac, esp_now_send_status_t status) {
        (void)mac;
#endif
        if (status == ESP_NOW_SEND_SUCCESS) {
            txOk.fetch_add(1);
        } else {
            txFail.fetch_add(1);
        }
    }
};

ESPNowMidi* ESPNowMidi::self = nullptr;
std::atomic<uint32_t> ESPNowMidi::txOk{0};
std::atomic<uint32_t> ESPNowMidi::txFail{0};
std::atomic<uint32_t> ESPNowMidi::rxTotal{0};
std::atomic<uint32_t> ESPNowMidi::rxBeacon{0};
std::atomic<uint32_t> ESPNowMidi::learnedPeers{0};

constexpr uint8_t ESPNowMidi::kBroadcast[6];
