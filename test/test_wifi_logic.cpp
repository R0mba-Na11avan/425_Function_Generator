/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host tests for the Wi-Fi logic: the STA/AP state machine
             (fed every 100 ms like the Wi-Fi task does), credential checks,
             the scan list and its JSON
**************************************************************************/

#include "test.h"
#include <string.h>
#include <string>
#include <vector>
#include "../src/comms/wifi_logic.h"
#include "../src/libs/ArduinoJson/ArduinoJson.h"

static const uint32_t TICK_MS = 100;

static WifiInputs inputs(bool connected, bool failed = false, uint8_t clients = 0) {
    return WifiInputs{connected, failed, clients};
}

//ticks until something other than NONE comes out or maxMs passes. returns the action, now is left at that tick
static WIFI_ACTION runUntilAction(WifiSupervisor& s, uint32_t& now, uint32_t maxMs, WifiInputs in) {
    for (uint32_t end = now + maxMs; now != end; now += TICK_MS) {
        WIFI_ACTION a = s.update(in, now);
        if (a != WIFI_ACTION::NONE) return a;
    }
    return WIFI_ACTION::NONE;
}

//every attempt fails one tick after it started. returns when each STA_BEGIN happened, until the supervisor gives up
static std::vector<uint32_t> failEveryAttempt(WifiSupervisor& s, uint32_t& now, WIFI_ACTION& last) {
    std::vector<uint32_t> begins;
    bool failNext = true;
    for (int i = 0; i < 2000; i++, now += TICK_MS) {
        WIFI_ACTION a = s.update(inputs(false, failNext), now);
        failNext = false;
        if (a == WIFI_ACTION::STA_BEGIN) {
            begins.push_back(now);
            failNext = true;
        } else if (a != WIFI_ACTION::NONE) {
            last = a;
            return begins;
        }
    }
    last = WIFI_ACTION::NONE;
    return begins;
}

//supervisor

TEST(wifi_no_credentials_starts_ap_and_stays) {
    WifiSupervisor s;
    CHECK(s.state() == WIFI_STATE::IDLE);
    CHECK(s.begin(false, 0) == WIFI_ACTION::AP_START);
    CHECK(s.state() == WIFI_STATE::AP_MODE);
    uint32_t now = 0;
    CHECK(runUntilAction(s, now, AP_STA_RETRY_MS * 2, inputs(false)) == WIFI_ACTION::NONE); //nothing to retry
    CHECK(s.state() == WIFI_STATE::AP_MODE);
}

TEST(wifi_joins_saved_network) {
    WifiSupervisor s;
    CHECK(s.begin(true, 0) == WIFI_ACTION::STA_BEGIN);
    CHECK(s.state() == WIFI_STATE::CONNECTING);
    CHECK(s.update(inputs(false), 100) == WIFI_ACTION::NONE);
    CHECK(s.update(inputs(true), 2000) == WIFI_ACTION::STA_ONLINE);
    CHECK(s.state() == WIFI_STATE::CONNECTED);
    CHECK(s.update(inputs(true), 2100) == WIFI_ACTION::NONE); //only once
}

TEST(wifi_connect_timeout_falls_back_to_ap) {
    WifiSupervisor s;
    s.begin(true, 0);
    CHECK(s.update(inputs(false), STA_CONNECT_TIMEOUT_MS - 1) == WIFI_ACTION::NONE);
    CHECK(s.update(inputs(false), STA_CONNECT_TIMEOUT_MS) == WIFI_ACTION::AP_START);
    CHECK(s.state() == WIFI_STATE::AP_MODE);
}

TEST(wifi_failed_attempts_retry_with_backoff) {
    WifiSupervisor s;
    uint32_t now = 0;
    s.begin(true, now);
    WIFI_ACTION last;
    std::vector<uint32_t> begins = failEveryAttempt(s, now, last);
    //each failure is seen a tick after its STA_BEGIN, the wait after it doubles: 1, 2, 4 s.
    //the next one (8 s) would land past the 15 s timeout
    CHECK_EQ(begins.size(), 3u);
    if (begins.size() == 3) {
        CHECK_EQ(begins[0], STA_RETRY_MIN_MS);
        CHECK_EQ(begins[1] - begins[0], TICK_MS + 2 * STA_RETRY_MIN_MS);
        CHECK_EQ(begins[2] - begins[1], TICK_MS + 4 * STA_RETRY_MIN_MS);
    }
    CHECK(last == WIFI_ACTION::AP_START);
    CHECK_EQ(now, STA_CONNECT_TIMEOUT_MS);
}

TEST(wifi_failure_while_retry_pending_does_not_push_it_back) {
    WifiSupervisor s;
    s.begin(true, 0);
    CHECK(s.update(inputs(false, true), 0) == WIFI_ACTION::NONE);   //retry at 1000
    CHECK(s.update(inputs(false, true), 500) == WIFI_ACTION::NONE); //already pending
    CHECK(s.update(inputs(false), 900) == WIFI_ACTION::NONE);
    CHECK(s.update(inputs(false), 1000) == WIFI_ACTION::STA_BEGIN);
}

TEST(wifi_slow_attempt_is_not_interrupted) {
    //no failure reported: one attempt runs until it succeeds or times out
    WifiSupervisor s;
    uint32_t now = 0;
    s.begin(true, now);
    CHECK(runUntilAction(s, now, STA_CONNECT_TIMEOUT_MS - TICK_MS, inputs(false)) == WIFI_ACTION::NONE);
}

TEST(wifi_lost_link_reconnects) {
    WifiSupervisor s;
    s.begin(true, 0);
    s.update(inputs(true), 1000);
    CHECK(s.update(inputs(false), 5000) == WIFI_ACTION::NONE);
    CHECK(s.state() == WIFI_STATE::CONNECTING);
    uint32_t now = 5000;
    CHECK(runUntilAction(s, now, 5000, inputs(false)) == WIFI_ACTION::STA_BEGIN);
    CHECK_EQ(now, 5000 + STA_RETRY_MIN_MS);
    CHECK(s.update(inputs(true), now + TICK_MS) == WIFI_ACTION::STA_ONLINE);
    CHECK(s.state() == WIFI_STATE::CONNECTED);
}

TEST(wifi_lost_link_backoff_caps_then_falls_back) {
    WifiSupervisor s;
    s.begin(true, 0);
    s.update(inputs(true), 1000);
    uint32_t now = 2000;
    s.update(inputs(false), now); //lost, first retry scheduled
    now += TICK_MS;
    WIFI_ACTION last;
    std::vector<uint32_t> begins = failEveryAttempt(s, now, last);
    CHECK(begins.size() >= 5u);
    if (begins.size() >= 5) {
        CHECK_EQ(begins[0], 2000 + STA_RETRY_MIN_MS);
        CHECK_EQ(begins[1] - begins[0], TICK_MS + 2 * STA_RETRY_MIN_MS);
        CHECK_EQ(begins[2] - begins[1], TICK_MS + 4 * STA_RETRY_MIN_MS);
        CHECK_EQ(begins[3] - begins[2], TICK_MS + STA_RETRY_MAX_MS);
        CHECK_EQ(begins[4] - begins[3], TICK_MS + STA_RETRY_MAX_MS); //capped
    }
    CHECK(last == WIFI_ACTION::AP_START);
    CHECK_EQ(now, 2000 + STA_LOST_TIMEOUT_MS);
}

//begins with saved credentials, fails to join and ends up in AP mode at the returned time
static uint32_t fallBackToAp(WifiSupervisor& s) {
    s.begin(true, 0);
    s.update(inputs(false), STA_CONNECT_TIMEOUT_MS);
    return STA_CONNECT_TIMEOUT_MS;
}

TEST(wifi_ap_tries_saved_network_again) {
    WifiSupervisor s;
    uint32_t apAt = fallBackToAp(s);
    CHECK(s.update(inputs(false), apAt + AP_STA_RETRY_MS - 1) == WIFI_ACTION::NONE);
    CHECK(s.update(inputs(false, false, 1), apAt + AP_STA_RETRY_MS) == WIFI_ACTION::NONE); //someone is on the AP
    CHECK(s.update(inputs(false), apAt + AP_STA_RETRY_MS + 100) == WIFI_ACTION::STA_BEGIN);
    CHECK(s.state() == WIFI_STATE::AP_MODE); //the AP stays up while trying
    CHECK(s.update(inputs(true), apAt + AP_STA_RETRY_MS + 3000) == WIFI_ACTION::STA_ONLINE);
    CHECK(s.state() == WIFI_STATE::CONNECTED);
}

TEST(wifi_ap_retry_gives_up_without_restarting_ap) {
    WifiSupervisor s;
    uint32_t t = fallBackToAp(s) + AP_STA_RETRY_MS;
    CHECK(s.update(inputs(false), t) == WIFI_ACTION::STA_BEGIN);
    CHECK(s.update(inputs(false), t + STA_CONNECT_TIMEOUT_MS) == WIFI_ACTION::STA_ABORT);
    CHECK(s.state() == WIFI_STATE::AP_MODE);
    //next try a full period later
    t += STA_CONNECT_TIMEOUT_MS;
    CHECK(s.update(inputs(false), t + AP_STA_RETRY_MS - 1) == WIFI_ACTION::NONE);
    CHECK(s.update(inputs(false), t + AP_STA_RETRY_MS) == WIFI_ACTION::STA_BEGIN);
}

TEST(wifi_ap_retry_aborts_when_someone_joins_the_ap) {
    WifiSupervisor s;
    uint32_t t = fallBackToAp(s) + AP_STA_RETRY_MS;
    s.update(inputs(false), t);
    CHECK(s.update(inputs(false, false, 1), t + 500) == WIFI_ACTION::STA_ABORT);
    CHECK(s.state() == WIFI_STATE::AP_MODE);
    CHECK(s.update(inputs(false), t + 600) == WIFI_ACTION::NONE); //waits a full period again
}

//the info panel's Change Wi-Fi / Back to <network>

static void goOnline(WifiSupervisor& s, uint32_t t = 0) {
    s.begin(true, t);
    s.update(inputs(true), t + 100);
}

TEST(wifi_open_setup_from_online) {
    WifiSupervisor s;
    goOnline(s);
    CHECK(s.state() == WIFI_STATE::CONNECTED);
    CHECK(s.openSetup(1000) == WIFI_ACTION::AP_START);
    CHECK(s.state() == WIFI_STATE::AP_MODE);
    //a connected flag from before the switch doesn't bring it back online
    CHECK(s.update(inputs(true), 1100) == WIFI_ACTION::NONE);
    CHECK(s.state() == WIFI_STATE::AP_MODE);
    //the saved network is tried again like after any fallback
    CHECK(s.update(inputs(false), 1000 + AP_STA_RETRY_MS - 1) == WIFI_ACTION::NONE);
    CHECK(s.update(inputs(false), 1000 + AP_STA_RETRY_MS) == WIFI_ACTION::STA_BEGIN);
}

TEST(wifi_open_setup_from_other_phases) {
    WifiSupervisor joining;
    joining.begin(true, 0);
    CHECK(joining.openSetup(500) == WIFI_ACTION::AP_START);
    CHECK(joining.state() == WIFI_STATE::AP_MODE);

    //already in setup mode: nothing to do
    WifiSupervisor ap;
    uint32_t t = fallBackToAp(ap);
    CHECK(ap.openSetup(t + 100) == WIFI_ACTION::NONE);

    //retrying the saved network from setup mode: stop trying, the AP is up already
    CHECK(ap.update(inputs(false), t + AP_STA_RETRY_MS) == WIFI_ACTION::STA_BEGIN);
    CHECK(ap.openSetup(t + AP_STA_RETRY_MS + 100) == WIFI_ACTION::STA_ABORT);
    CHECK(ap.state() == WIFI_STATE::AP_MODE);
    //and the retry period starts over
    CHECK(ap.update(inputs(false), t + 2 * AP_STA_RETRY_MS) == WIFI_ACTION::NONE);
}

TEST(wifi_leave_setup_joins_saved_network) {
    WifiSupervisor s;
    goOnline(s);
    s.openSetup(1000);
    CHECK(s.leaveSetup(2000) == WIFI_ACTION::LEAVE_AP);
    CHECK(s.state() == WIFI_STATE::CONNECTING);
    //someone still on the setup Wi-Fi doesn't stop it, the user asked for this
    CHECK(s.update(inputs(false, false, 1), 2100) == WIFI_ACTION::NONE);
    CHECK(s.update(inputs(true), 3000) == WIFI_ACTION::STA_ONLINE);
    CHECK(s.state() == WIFI_STATE::CONNECTED);
}

TEST(wifi_leave_setup_falls_back_if_join_fails) {
    WifiSupervisor s;
    uint32_t t = fallBackToAp(s);
    CHECK(s.leaveSetup(t) == WIFI_ACTION::LEAVE_AP);
    CHECK(s.update(inputs(false), t + STA_CONNECT_TIMEOUT_MS - 1) == WIFI_ACTION::NONE);
    //the AP was stopped, so it has to be started again
    CHECK(s.update(inputs(false), t + STA_CONNECT_TIMEOUT_MS) == WIFI_ACTION::AP_START);
    CHECK(s.state() == WIFI_STATE::AP_MODE);
}

TEST(wifi_leave_setup_only_from_setup_mode_with_a_saved_network) {
    WifiSupervisor noCreds;
    noCreds.begin(false, 0);
    CHECK(noCreds.leaveSetup(100) == WIFI_ACTION::NONE);
    CHECK(noCreds.state() == WIFI_STATE::AP_MODE);

    WifiSupervisor online;
    goOnline(online);
    CHECK(online.leaveSetup(1000) == WIFI_ACTION::NONE);
    CHECK(online.state() == WIFI_STATE::CONNECTED);

    WifiSupervisor retrying;
    uint32_t t = fallBackToAp(retrying);
    retrying.update(inputs(false), t + AP_STA_RETRY_MS);
    CHECK(retrying.leaveSetup(t + AP_STA_RETRY_MS + 100) == WIFI_ACTION::NONE);
}

TEST(wifi_timers_survive_millis_wrap) {
    WifiSupervisor s;
    uint32_t start = 0xFFFFFFFFu - 500; //the first retry is due after millis() wraps
    s.begin(true, start);
    CHECK(s.update(inputs(false, true), start) == WIFI_ACTION::NONE);
    CHECK(s.update(inputs(false), start + 100) == WIFI_ACTION::NONE); //not early
    CHECK(s.update(inputs(false), start + STA_RETRY_MIN_MS - 1) == WIFI_ACTION::NONE);
    CHECK(s.update(inputs(false), start + STA_RETRY_MIN_MS) == WIFI_ACTION::STA_BEGIN);
    CHECK(s.update(inputs(false), start + 6000) == WIFI_ACTION::NONE); //not timed out
    CHECK(s.update(inputs(false), start + STA_CONNECT_TIMEOUT_MS) == WIFI_ACTION::AP_START);
}

//credentials

TEST(wifi_validate_credentials) {
    CHECK(validateCredentials("home", "") == CREDS_ERROR::OK);
    CHECK(validateCredentials("home", nullptr) == CREDS_ERROR::OK);
    CHECK(validateCredentials("home", "12345678") == CREDS_ERROR::OK);
    CHECK(validateCredentials("home", "1234567") == CREDS_ERROR::PASSWORD_INVALID);
    CHECK(validateCredentials("home", std::string(63, 'p').c_str()) == CREDS_ERROR::OK);
    CHECK(validateCredentials("home", std::string(64, 'a').c_str()) == CREDS_ERROR::OK);  //hex PSK
    CHECK(validateCredentials("home", std::string(64, 'p').c_str()) == CREDS_ERROR::PASSWORD_INVALID);
    CHECK(validateCredentials("home", std::string(65, 'a').c_str()) == CREDS_ERROR::PASSWORD_INVALID);
    CHECK(validateCredentials("", "12345678") == CREDS_ERROR::SSID_MISSING);
    CHECK(validateCredentials(nullptr, "") == CREDS_ERROR::SSID_MISSING);
    CHECK(validateCredentials(std::string(32, 's').c_str(), "") == CREDS_ERROR::OK);
    CHECK(validateCredentials(std::string(33, 's').c_str(), "") == CREDS_ERROR::SSID_TOO_LONG);
}

static CREDS_ERROR parse(const std::string& json, WifiCredentials& c) {
    memset(&c, 0x55, sizeof(c));
    return parseCredentials(json.c_str(), json.size(), c);
}

TEST(wifi_parse_credentials) {
    WifiCredentials c;
    CHECK(parse("{\"ssid\":\"Lab Net\",\"password\":\"secret123\"}", c) == CREDS_ERROR::OK);
    CHECK(strcmp(c.ssid, "Lab Net") == 0);
    CHECK(strcmp(c.password, "secret123") == 0);

    CHECK(parse("{\"ssid\":\"open\"}", c) == CREDS_ERROR::OK); //password optional
    CHECK(strcmp(c.password, "") == 0);

    //16 x "é" is 32 bytes: the limit is in bytes, not characters
    std::string e16;
    for (int i = 0; i < 16; i++) e16 += "\xC3\xA9";
    CHECK(parse("{\"ssid\":\"" + e16 + "\"}", c) == CREDS_ERROR::OK);
    CHECK_EQ(strlen(c.ssid), 32u);
    CHECK(parse("{\"ssid\":\"" + e16 + "x\"}", c) == CREDS_ERROR::SSID_TOO_LONG);

    CHECK(parse("{\"ssid\":\"q\\\"uote\"}", c) == CREDS_ERROR::OK); //escapes decoded
    CHECK(strcmp(c.ssid, "q\"uote") == 0);

    CHECK(parse("", c) == CREDS_ERROR::BAD_JSON);
    CHECK(parse("{\"ssid\":", c) == CREDS_ERROR::BAD_JSON);
    CHECK(parse("[\"ssid\"]", c) == CREDS_ERROR::BAD_JSON);
    CHECK(parse("{}", c) == CREDS_ERROR::SSID_MISSING);
    CHECK(parse("{\"ssid\":42}", c) == CREDS_ERROR::SSID_MISSING);
    CHECK(parse("{\"ssid\":\"a\",\"password\":12345678}", c) == CREDS_ERROR::PASSWORD_INVALID);
    CHECK(parse("{\"ssid\":\"a\",\"password\":\"short\"}", c) == CREDS_ERROR::PASSWORD_INVALID);
    CHECK(strlen(credsErrorMessage(CREDS_ERROR::PASSWORD_INVALID)) > 0);
}

//scan list

TEST(wifi_scan_list_sorted_and_deduplicated) {
    ScanList l;
    clearScanList(l);
    addScanResult(l, "weak", -80, true);
    addScanResult(l, "strong", -40, true);
    addScanResult(l, "", -30, false);       //hidden, skipped
    addScanResult(l, nullptr, -30, false);
    addScanResult(l, "mesh", -70, true);
    addScanResult(l, "mesh", -50, true);    //stronger AP of the same network
    addScanResult(l, "mesh", -90, true);    //weaker, ignored
    addScanResult(l, "mid", -60, false);

    CHECK_EQ(l.count, 4);
    const char* order[] = {"strong", "mesh", "mid", "weak"};
    for (int i = 0; i < 4 && i < l.count; i++) CHECK(strcmp(l.entries[i].ssid, order[i]) == 0);
    CHECK_EQ(l.entries[1].rssi, -50);
    CHECK(!l.entries[2].secured);
}

TEST(wifi_scan_list_full_keeps_strongest) {
    ScanList l;
    clearScanList(l);
    char name[8];
    for (int i = 0; i < WIFI_SCAN_MAX; i++) {
        snprintf(name, sizeof(name), "n%d", i);
        addScanResult(l, name, -50 - i, true); //n0 strongest
    }
    CHECK_EQ(l.count, WIFI_SCAN_MAX);
    addScanResult(l, "tooweak", -100, true);
    CHECK_EQ(l.count, WIFI_SCAN_MAX);
    for (int i = 0; i < l.count; i++) CHECK(strcmp(l.entries[i].ssid, "tooweak") != 0);

    addScanResult(l, "best", -20, true);
    CHECK_EQ(l.count, WIFI_SCAN_MAX);
    CHECK(strcmp(l.entries[0].ssid, "best") == 0);
    snprintf(name, sizeof(name), "n%d", WIFI_SCAN_MAX - 2);
    CHECK(strcmp(l.entries[WIFI_SCAN_MAX - 1].ssid, name) == 0); //the weakest one fell off
}

TEST(wifi_scan_list_clamps_and_truncates) {
    ScanList l;
    clearScanList(l);
    addScanResult(l, std::string(40, 'x').c_str(), 5, true);
    addScanResult(l, "far", -300, true);
    CHECK_EQ(strlen(l.entries[0].ssid), 32u);
    CHECK_EQ(l.entries[0].rssi, 0);
    CHECK_EQ(l.entries[1].rssi, -127);
}

TEST(wifi_scan_json_round_trips_and_escapes) {
    ScanList l;
    clearScanList(l);
    addScanResult(l, "a\"b\\c", -40, true);
    addScanResult(l, "<img src=x>", -50, false);
    addScanResult(l, "tab\there", -60, true);

    size_t size = scanListJsonSize(l, true);
    std::vector<char> out(size);
    size_t n = scanListJson(l, true, out.data(), out.size());
    CHECK_EQ(n + 1, size);

    JsonDocument doc;
    CHECK(!deserializeJson(doc, out.data()));
    CHECK(doc["scanning"].as<bool>());
    JsonArray nets = doc["networks"];
    CHECK_EQ(nets.size(), 3u);
    CHECK(strcmp(nets[0]["ssid"], "a\"b\\c") == 0);
    CHECK_EQ(nets[0]["rssi"].as<int>(), -40);
    CHECK(nets[0]["secured"].as<bool>());
    CHECK(strcmp(nets[1]["ssid"], "<img src=x>") == 0);
    CHECK(!nets[1]["secured"].as<bool>());
    CHECK(strcmp(nets[2]["ssid"], "tab\there") == 0);

    char small[16];
    CHECK_EQ(scanListJson(l, true, small, sizeof(small)), 0u);

    ScanList empty;
    clearScanList(empty);
    char buf[64];
    scanListJson(empty, false, buf, sizeof(buf));
    CHECK(strcmp(buf, "{\"scanning\":false,\"networks\":[]}") == 0);
}

TEST(wifi_ap_ssid_from_mac) {
    uint8_t mac[6] = {0x24, 0x6F, 0x28, 0x01, 0xA1, 0x0B};
    char ssid[33];
    apSsidFromMac(mac, ssid, sizeof(ssid));
    CHECK(strcmp(ssid, AP_SSID_PREFIX "A10B") == 0);
}
