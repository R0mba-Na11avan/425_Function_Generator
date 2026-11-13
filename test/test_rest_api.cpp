/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host tests for the web control API, end to end: HTTP over the
             fake esp_http_server into the real controller and generator
**************************************************************************/

#include "test.h"
#include <string.h>
#include <string>
#include "esp_http_server.h"
#include "../src/comms/rest_api.h"
#include "../src/comms/mcp_server.h"
#include "../src/comms/provisioning.h"
#include "../src/libs/ArduinoJson/ArduinoJson.h"

//generator + controller + HTTP server + REST API, all running
struct RestRig {
    FunctionGenerator gen;
    FuncGenController ctl;
    HttpServer http;
    RestApi api;
    RestRig() : gen(PIN_AD9833_CS, PIN_AMP_DAC_CS, PIN_OFFSET_DAC_CS, PIN_WAVE_SWITCH), ctl(gen), api(ctl, http) {
        fakeHttpdReset();
        gen.begin();
        api.begin();
        http.begin();
    }
};

static FakeHttpOptions jsonBody() {
    FakeHttpOptions o;
    o.headers["Content-Type"] = "application/json";
    return o;
}

static FakeHttpResponse post(const std::string& body, const FakeHttpOptions& o = jsonBody()) {
    return fakeHttpRequest(HTTP_POST, API_STATE_PATH, body, o);
}

struct Reply {
    int status;
    JsonDocument doc;
};

static Reply update(const std::string& body) {
    FakeHttpResponse r = post(body);
    Reply out;
    out.status = r.status;
    deserializeJson(out.doc, r.body);
    return out;
}

static Reply getJson(const char* path) {
    FakeHttpResponse r = fakeHttpRequest(HTTP_GET, path);
    Reply out;
    out.status = r.status;
    deserializeJson(out.doc, r.body);
    return out;
}

static bool contains(const std::string& s, const char* part) {
    return s.find(part) != std::string::npos;
}

static std::string errorText(const Reply& r) {
    return r.doc["error"] | "";
}

static std::string noteText(const Reply& r) {
    return r.doc["note"] | "";
}

//counts changes tagged WEB, and any others
struct SourceLog {
    int web = 0;
    int other = 0;
};

static void sourceListener(const FuncGenState&, CHANGE_SOURCE source, void* ctx) {
    SourceLog* log = (SourceLog*)ctx;
    if (source == CHANGE_SOURCE::WEB) log->web++;
    else log->other++;
}

//routes

TEST(rest_routes_and_page) {
    RestRig rig;
    CHECK_EQ(fakeHttpdState().handlers, 4u);

    FakeHttpResponse page = fakeHttpRequest(HTTP_GET, WEB_CONTROL_PATH);
    CHECK_EQ(page.status, 200);
    CHECK(contains(page.contentType, "text/html"));
    CHECK(contains(page.body, "<title>Function Generator</title>"));
    CHECK(contains(page.body, "'" API_STATE_PATH "'")); //paths come from config.h
    CHECK(contains(page.body, "'" API_INFO_PATH "'"));
    CHECK(page.headers["Cache-Control"] == "no-store");

    //the page can also be put on / (WifiManager does that outside setup mode)
    CHECK_EQ(fakeHttpRequest(HTTP_GET, "/").status, 404);
    CHECK(rig.http.registerRoute("/", HTTP_METHOD::GET, RestApi::handlePage, &rig.api));
    CHECK(contains(fakeHttpRequest(HTTP_GET, "/").body, "<title>Function Generator</title>"));
}

TEST(rest_routes_fit_next_to_mcp_and_setup) {
    //setup (AP) mode has the most routes: setup page + MCP + REST
    fakeHttpdReset();
    FunctionGenerator gen(PIN_AD9833_CS, PIN_AMP_DAC_CS, PIN_OFFSET_DAC_CS, PIN_WAVE_SWITCH);
    FuncGenController ctl(gen);
    HttpServer http;
    Provisioning prov(http);
    McpServer mcp(ctl, http);
    RestApi api(ctl, http);
    CHECK(prov.enable());
    CHECK(mcp.begin());
    CHECK(api.begin());
    CHECK(http.begin());
    CHECK(fakeHttpdState().handlers <= (size_t)HTTP_MAX_ROUTES);

    //and the normal network: home page instead of the setup routes
    prov.disable();
    CHECK(http.registerRoute("/", HTTP_METHOD::GET, RestApi::handlePage, &api));
    CHECK(contains(fakeHttpRequest(HTTP_GET, "/").body, "<title>Function Generator</title>"));
    CHECK_EQ(fakeHttpRequest(HTTP_GET, WEB_CONTROL_PATH).status, 200);
}

//GET /api/state

TEST(rest_get_state) {
    RestRig rig;
    Reply r = getJson(API_STATE_PATH);
    CHECK_EQ(r.status, 200);
    JsonObject s = r.doc["state"];
    CHECK(strcmp(s["waveform"] | "", "sine") == 0);
    CHECK_NEAR(s["frequency_hz"].as<float>(), 1.0f, 0.1f);
    CHECK_NEAR(s["amplitude_vpp"].as<float>(), AMP_DEFAULT_VPP, 1e-4);
    CHECK_NEAR(s["dc_offset_v"].as<float>(), 0.0f, 1e-4);
    CHECK(s["output_enabled"].is<bool>());
    CHECK(!s["output_enabled"].as<bool>());

    JsonObject ranges = r.doc["ranges"];
    CHECK_NEAR(ranges["frequency_hz"]["min"].as<float>(), FREQ_MIN_HZ, 1e-4);
    CHECK_NEAR(ranges["frequency_hz"]["max"].as<float>(), FREQ_MAX_HZ, 1);
    CHECK(ranges["phase_deg"]["max_exclusive"].as<bool>());
    CHECK_NEAR(r.doc["output_peak_v"].as<float>(), OUTPUT_PEAK_MAX_V, 1e-4);

    //the ranges are the ones allowed right now: they follow the peak rule
    FuncGenLimits l = rig.ctl.limits();
    rig.ctl.setDCOffset(7.0f);
    FuncGenState st = rig.ctl.state();
    r = getJson(API_STATE_PATH);
    CHECK_NEAR(r.doc["ranges"]["amplitude_vpp"]["max"].as<float>(), l.amplitudeFor(st).max, 1e-4);
    CHECK_NEAR(r.doc["ranges"]["dc_offset_v"]["max"].as<float>(), l.dcOffsetFor(st).max, 1e-4);
    CHECK(r.doc["ranges"]["amplitude_vpp"]["max"].as<float>() < l.amplitude[0].max);
}

//POST /api/state: values

TEST(rest_set_each_setting) {
    RestRig rig;
    SourceLog log;
    rig.ctl.addListener(sourceListener, &log);

    Reply r = update("{\"frequency_hz\": 1000}");
    CHECK_EQ(r.status, 200);
    CHECK(!r.doc["clamped"].as<bool>());
    CHECK(r.doc["note"].isNull());
    CHECK_NEAR(rig.ctl.state().frequency, 1000.0f, 0.1f);
    CHECK_NEAR(r.doc["state"]["frequency_hz"].as<float>(), rig.ctl.state().frequency, 1e-3);

    CHECK_EQ(update("{\"amplitude_vpp\": 2.5}").status, 200);
    CHECK_NEAR(rig.ctl.state().amplitude, 2.5f, 1e-4);
    CHECK_EQ(update("{\"dc_offset_v\": -0.25}").status, 200);
    CHECK_NEAR(rig.ctl.state().dcOffset, -0.25f, 1e-4);
    CHECK_EQ(update("{\"phase_deg\": 90}").status, 200);
    CHECK_NEAR(rig.ctl.state().phase, 90.0f, 0.1f);
    CHECK_EQ(update("{\"waveform\": \"Square\"}").status, 200); //any case
    CHECK(rig.ctl.state().waveform == WAVEFORM::SQUARE);
    r = update("{\"output_enabled\": true}");
    CHECK_EQ(r.status, 200);
    CHECK(rig.ctl.state().outputEnabled);
    CHECK(r.doc["state"]["output_enabled"].as<bool>());

    //every change tagged WEB, so the LCD shows the badge
    CHECK_EQ(log.web, 6);
    CHECK_EQ(log.other, 0);
}

TEST(rest_out_of_range_is_clamped) {
    RestRig rig;
    FuncGenLimits l = rig.ctl.limits();

    Reply r = update("{\"frequency_hz\": 99000000}");
    CHECK_EQ(r.status, 200);
    CHECK(r.doc["clamped"].as<bool>());
    CHECK_NEAR(rig.ctl.state().frequency, FREQ_MAX_HZ, 1);
    CHECK(contains(noteText(r), "frequency_hz clamped to 12500000 Hz"));

    r = update("{\"frequency_hz\": 0}");
    CHECK(r.doc["clamped"].as<bool>());
    CHECK_NEAR(rig.ctl.state().frequency, FREQ_MIN_HZ, 0.1f);

    //amplitude stops at the peak limit, the offset stays where it is
    rig.ctl.setDCOffset(7.0f);
    float ampMax = l.amplitudeFor(rig.ctl.state()).max;
    r = update("{\"amplitude_vpp\": 50}");
    CHECK_EQ(r.status, 200);
    CHECK(r.doc["clamped"].as<bool>());
    CHECK_NEAR(rig.ctl.state().amplitude, ampMax, 1e-4);
    CHECK_NEAR(rig.ctl.state().dcOffset, 7.0f, 1e-4);
    CHECK(contains(noteText(r), "amplitude_vpp clamped to"));
    CHECK(contains(noteText(r), "less with this offset"));
    //and the reply's range shows the new edge
    CHECK_NEAR(r.doc["ranges"]["amplitude_vpp"]["max"].as<float>(), ampMax, 1e-4);

    //offset stops at the peak limit, the amplitude stays
    float amp = rig.ctl.state().amplitude;
    float offMin = l.dcOffsetFor(rig.ctl.state()).min;
    r = update("{\"dc_offset_v\": -100}");
    CHECK(r.doc["clamped"].as<bool>());
    CHECK_NEAR(rig.ctl.state().dcOffset, offMin, 1e-4);
    CHECK_NEAR(rig.ctl.state().amplitude, amp, 1e-4);
    CHECK(contains(noteText(r), "dc_offset_v clamped to"));

    //exactly on the edge is not a clamp
    r = update("{\"dc_offset_v\": 0}");
    CHECK(!r.doc["clamped"].as<bool>());
}

TEST(rest_phase_wraps) {
    RestRig rig;
    Reply r = update("{\"phase_deg\": 400}");
    CHECK_EQ(r.status, 200);
    CHECK(r.doc["clamped"].as<bool>());
    CHECK(contains(noteText(r), "phase_deg wrapped to 40 degrees"));
    CHECK_NEAR(rig.ctl.state().phase, 40.0f, 0.1f);

    r = update("{\"phase_deg\": -90}");
    CHECK_NEAR(rig.ctl.state().phase, 270.0f, 0.1f);

    r = update("{\"phase_deg\": 360}");
    CHECK_NEAR(rig.ctl.state().phase, 0.0f, 0.1f);

    //a huge value would never finish the driver's wrap loop: wrapped before it gets there
    r = update("{\"phase_deg\": 1e20}");
    CHECK_EQ(r.status, 200);
    CHECK(rig.ctl.state().phase >= 0.0f && rig.ctl.state().phase < 360.0f);
    r = update("{\"phase_deg\": -0.000001}");
    CHECK(rig.ctl.state().phase >= 0.0f && rig.ctl.state().phase < 360.0f);
}

//POST /api/state: rejected bodies

TEST(rest_bad_bodies_rejected) {
    RestRig rig;
    SourceLog log;
    rig.ctl.addListener(sourceListener, &log);
    FuncGenState before = rig.ctl.state();

    struct Case { const char* body; const char* error; };
    const Case cases[] = {
        {"not json", "not valid JSON"},
        {"[1]", "JSON object"},
        {"42", "JSON object"},
        {"{}", "exactly one setting"},
        {"{\"amplitude_vpp\": 1, \"dc_offset_v\": 1}", "exactly one setting"},
        {"{\"volume\": 11}", "unknown setting 'volume'"},
        {"{\"amplitude_vpp\": \"2\"}", "'amplitude_vpp' must be a finite number in Vpp"},
        {"{\"frequency_hz\": true}", "'frequency_hz' must be a finite number"},
        {"{\"frequency_hz\": null}", "'frequency_hz' must be a finite number"},
        {"{\"frequency_hz\": 1e999}", "'frequency_hz' must be a finite number"},
        {"{\"phase_deg\": 1e300}", "'phase_deg' must be a finite number"},
        {"{\"waveform\": \"sawtooth\"}", "'waveform' must be one of"},
        {"{\"waveform\": 1}", "'waveform' must be one of"},
        {"{\"output_enabled\": \"true\"}", "'output_enabled' must be true or false"},
        {"{\"output_enabled\": 1}", "'output_enabled' must be true or false"},
    };
    for (const Case& c : cases) {
        Reply r = update(c.body);
        CHECK_EQ(r.status, 400);
        if (!contains(errorText(r), c.error)) {
            printf("    body %s -> \"%s\"\n", c.body, errorText(r).c_str());
            CHECK(contains(errorText(r), c.error));
        }
    }

    //nothing was changed or announced
    FuncGenState after = rig.ctl.state();
    CHECK(after.waveform == before.waveform);
    CHECK(after.frequency == before.frequency);
    CHECK(after.amplitude == before.amplitude);
    CHECK(after.phase == before.phase);
    CHECK(after.dcOffset == before.dcOffset);
    CHECK(after.outputEnabled == before.outputEnabled);
    CHECK_EQ(log.web, 0);
}

TEST(rest_request_checks) {
    RestRig rig;
    const std::string body = "{\"output_enabled\": true}";

    //a cross-site form or a simple fetch can't send JSON
    FakeHttpOptions o;
    CHECK_EQ(post(body, o).status, 415);
    o.headers["Content-Type"] = "text/plain";
    CHECK_EQ(post(body, o).status, 415);
    CHECK(!rig.ctl.state().outputEnabled);

    //another site open in the same browser
    o = jsonBody();
    o.headers["Origin"] = "http://evil.example";
    FakeHttpResponse r = post(body, o);
    CHECK_EQ(r.status, 403);
    CHECK(contains(r.body, "origin not allowed"));
    o.headers["Origin"] = "http://" + std::string(200, 'a') + ".example"; //too long to read is not "absent"
    CHECK_EQ(post(body, o).status, 403);
    CHECK(!rig.ctl.state().outputEnabled);

    //the page itself, from funcgen.local or an IP
    o.headers["Origin"] = "http://" MDNS_HOSTNAME ".local";
    CHECK_EQ(post(body, o).status, 200);
    o.headers["Origin"] = "http://192.168.4.1";
    CHECK_EQ(post("{\"output_enabled\": false}", o).status, 200);
    CHECK(!rig.ctl.state().outputEnabled);

    //content type with a charset is fine
    o = jsonBody();
    o.headers["Content-Type"] = "application/json; charset=utf-8";
    CHECK_EQ(post(body, o).status, 200);

    CHECK_EQ(post(std::string(300, ' ')).status, 413);
    CHECK_EQ(fakeHttpRequest(HTTP_POST, API_INFO_PATH, body, jsonBody()).status, 405);
}

//GET /api/info

static NetStatus fakeNet(void*) {
    NetStatus n = {};
    n.state = WIFI_STATE::CONNECTED;
    strcpy(n.ssid, "Lab \"5\"");
    strcpy(n.ip, "192.168.1.137");
    strcpy(n.hostname, "funcgen");
    strcpy(n.savedSsid, "Lab \"5\"");
    return n;
}

static NetStatus fakeApNet(void*) {
    NetStatus n = {};
    n.state = WIFI_STATE::AP_MODE;
    strcpy(n.ssid, "FuncGen-1A2B");
    strcpy(n.ip, "192.168.4.1");
    strcpy(n.hostname, "funcgen");
    return n;
}

static BleStatus fakeBle(void*) {
    BleStatus b = {};
    b.state = BLE_STATE::CONNECTED;
    strcpy(b.name, "FuncGen-1A2B");
    b.connected = 1;
    b.bonded = 2;
    return b;
}

TEST(rest_info) {
    RestRig rig;
    Reply r = getJson(API_INFO_PATH);
    CHECK_EQ(r.status, 200);
    CHECK(strcmp(r.doc["firmware"] | "", FIRMWARE_VERSION) == 0);
    CHECK_EQ(r.doc["poll_ms"].as<int>(), WEB_POLL_MS);
    CHECK(r.doc["wifi"].isNull()); //no sources given yet
    CHECK(r.doc["ble"].isNull());

    rig.api.setInfoSources(fakeNet, nullptr, fakeBle, nullptr);
    r = getJson(API_INFO_PATH);
    JsonObject w = r.doc["wifi"];
    CHECK(strcmp(w["state"] | "", "connected") == 0);
    CHECK(strcmp(w["ssid"] | "", "Lab \"5\"") == 0); //escaped and back
    CHECK(strcmp(w["ip"] | "", "192.168.1.137") == 0);
    CHECK(strcmp(w["mcp_url"] | "", "http://funcgen.local" MCP_PATH) == 0);
    JsonObject b = r.doc["ble"];
    CHECK(strcmp(b["state"] | "", "connected") == 0);
    CHECK(strcmp(b["name"] | "", "FuncGen-1A2B") == 0);
    CHECK_EQ(b["connected"].as<int>(), 1);
    CHECK_EQ(b["paired"].as<int>(), 2);

    //setup mode: no mDNS name on the AP, the URL uses the IP
    rig.api.setInfoSources(fakeApNet, nullptr, nullptr, nullptr);
    r = getJson(API_INFO_PATH);
    CHECK(strcmp(r.doc["wifi"]["state"] | "", "setup") == 0);
    CHECK(strcmp(r.doc["wifi"]["mcp_url"] | "", "http://192.168.4.1" MCP_PATH) == 0);
    CHECK(r.doc["ble"].isNull());
}
