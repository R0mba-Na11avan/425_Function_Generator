/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host tests for the Wi-Fi setup routes against the fake
             esp_http_server, playing the Wi-Fi task's side by hand
**************************************************************************/

#include "test.h"
#include <string.h>
#include <string>
#include "esp_http_server.h"
#include "../src/comms/provisioning.h"
#include "../src/libs/ArduinoJson/ArduinoJson.h"

static FakeHttpOptions jsonBody() {
    FakeHttpOptions o;
    o.headers["Content-Type"] = "application/json";
    return o;
}

static FakeHttpResponse save(const std::string& body, const FakeHttpOptions& o = jsonBody()) {
    return fakeHttpRequest(HTTP_POST, "/save", body, o);
}

static JsonDocument parseJson(const std::string& s) {
    JsonDocument doc;
    if (deserializeJson(doc, s)) doc.clear();
    return doc;
}

TEST(prov_routes_only_while_enabled) {
    fakeHttpdReset();
    HttpServer http;
    Provisioning p(http);
    http.begin();
    CHECK(!p.enabled());
    CHECK_EQ(fakeHttpRequest(HTTP_GET, "/").status, 404);

    CHECK(p.enable());
    CHECK(p.enabled());
    CHECK(p.enable()); //twice is fine
    CHECK_EQ(fakeHttpdState().handlers, 4u);
    CHECK_EQ(fakeHttpRequest(HTTP_GET, "/").status, 200);
    CHECK_EQ(fakeHttpRequest(HTTP_GET, "/scan").status, 200);

    p.disable();
    CHECK(!p.enabled());
    CHECK_EQ(fakeHttpdState().handlers, 0u);
    CHECK_EQ(fakeHttpRequest(HTTP_GET, "/").status, 404);
    CHECK_EQ(save("{\"ssid\":\"x\"}").status, 404); //nobody can overwrite the credentials in STA mode

    //survives a server restart (mode change)
    p.enable();
    http.begin();
    CHECK_EQ(fakeHttpRequest(HTTP_GET, "/").status, 200);
}

static void dummy(HttpRequest& req, void*) {
    req.sendEmpty(204);
}

TEST(prov_enable_is_all_or_nothing) {
    fakeHttpdReset();
    HttpServer http;
    char path[16];
    for (int i = 0; i < HTTP_MAX_ROUTES - 2; i++) { //leaves room for only 2 of the 4 routes
        snprintf(path, sizeof(path), "/r%d", i);
        http.registerRoute(path, HTTP_METHOD::GET, dummy);
    }
    http.begin();
    Provisioning p(http);
    CHECK(!p.enable());
    CHECK(!p.enabled());
    CHECK_EQ(fakeHttpRequest(HTTP_GET, "/").status, 404);
    CHECK_EQ(fakeHttpdState().handlers, (size_t)(HTTP_MAX_ROUTES - 2));
}

TEST(prov_page) {
    fakeHttpdReset();
    HttpServer http;
    Provisioning p(http);
    p.enable();
    http.begin();
    FakeHttpResponse r = fakeHttpRequest(HTTP_GET, "/");
    CHECK_EQ(r.status, 200);
    CHECK(r.contentType.rfind("text/html", 0) == 0);
    CHECK(r.headers["Cache-Control"] == "no-store");
    CHECK(r.body.find("<title>") != std::string::npos);
    CHECK(r.body.find("http://" MDNS_HOSTNAME ".local/mcp") != std::string::npos);
    CHECK(r.body.find("/save") != std::string::npos);
    //SSIDs come from strangers: never inserted as HTML, nothing loaded from outside
    CHECK(r.body.find("innerHTML") == std::string::npos);
    CHECK(r.body.find("http://", r.body.find("<script")) == std::string::npos);
    CHECK(r.body.find("https://") == std::string::npos);
}

TEST(prov_first_scan_and_results) {
    fakeHttpdReset();
    HttpServer http;
    Provisioning p(http);
    p.enable();
    http.begin();

    FakeHttpResponse r = fakeHttpRequest(HTTP_GET, "/scan");
    CHECK_EQ(r.status, 200);
    CHECK(r.contentType == "application/json");
    JsonDocument d = parseJson(r.body);
    CHECK(d["scanning"].as<bool>()); //the first GET starts one
    CHECK_EQ(d["networks"].as<JsonArray>().size(), 0u);

    CHECK(p.takeScanRequest());
    CHECK(!p.takeScanRequest()); //once
    fakeHttpRequest(HTTP_GET, "/scan");
    CHECK(!p.takeScanRequest()); //already running, no second request

    ScanList l;
    clearScanList(l);
    addScanResult(l, "Lab", -45, true);
    addScanResult(l, "Guest", -70, false);
    p.scanFinished(l);

    d = parseJson(fakeHttpRequest(HTTP_GET, "/scan").body);
    CHECK(!d["scanning"].as<bool>());
    CHECK_EQ(d["networks"].as<JsonArray>().size(), 2u);
    CHECK(strcmp(d["networks"][0]["ssid"], "Lab") == 0);
    CHECK(!p.takeScanRequest()); //results exist, GET doesn't rescan
}

TEST(prov_rescan_and_failure) {
    fakeHttpdReset();
    HttpServer http;
    Provisioning p(http);
    p.enable();
    http.begin();

    ScanList l;
    clearScanList(l);
    addScanResult(l, "Old", -50, true);
    p.requestScan();
    CHECK(p.takeScanRequest());
    p.scanFinished(l);

    FakeHttpResponse r = fakeHttpRequest(HTTP_POST, "/scan");
    CHECK_EQ(r.status, 202);
    JsonDocument d = parseJson(r.body);
    CHECK(d["scanning"].as<bool>());
    CHECK_EQ(d["networks"].as<JsonArray>().size(), 1u); //old list stays visible meanwhile
    CHECK(p.takeScanRequest());

    p.requestScan(); //ignored while one runs
    p.scanFailed();
    CHECK(!p.takeScanRequest());
    d = parseJson(fakeHttpRequest(HTTP_GET, "/scan").body);
    CHECK(!d["scanning"].as<bool>());
    CHECK(strcmp(d["networks"][0]["ssid"], "Old") == 0);
}

TEST(prov_save_valid) {
    fakeHttpdReset();
    HttpServer http;
    Provisioning p(http);
    p.enable();
    http.begin();

    WifiCredentials c;
    CHECK(!p.takeCredentials(c));

    FakeHttpOptions o = jsonBody();
    o.headers["Content-Type"] = "application/json; charset=utf-8";
    FakeHttpResponse r = save("{\"ssid\":\"Lab\",\"password\":\"hunter2hunter2\"}", o);
    CHECK_EQ(r.status, 200);
    CHECK(r.body == "{\"ok\":true}");
    CHECK(r.body.find("hunter2") == std::string::npos);

    CHECK(p.takeCredentials(c));
    CHECK(strcmp(c.ssid, "Lab") == 0);
    CHECK(strcmp(c.password, "hunter2hunter2") == 0);
    CHECK(!p.takeCredentials(c)); //once

    //a second save before the Wi-Fi task picked up the first replaces it
    save("{\"ssid\":\"A\",\"password\":\"aaaaaaaa\"}");
    save("{\"ssid\":\"B\"}");
    CHECK(p.takeCredentials(c));
    CHECK(strcmp(c.ssid, "B") == 0);
    CHECK(strcmp(c.password, "") == 0);
}

TEST(prov_save_rejects) {
    fakeHttpdReset();
    HttpServer http;
    Provisioning p(http);
    p.enable();
    http.begin();
    const std::string good = "{\"ssid\":\"Lab\",\"password\":\"12345678\"}";

    //what a cross-site form could send
    CHECK_EQ(save(good, FakeHttpOptions()).status, 415);
    FakeHttpOptions text;
    text.headers["Content-Type"] = "text/plain";
    CHECK_EQ(save(good, text).status, 415);

    FakeHttpResponse r = save("{\"ssid\":\"Lab\",\"password\":\"short\"}");
    CHECK_EQ(r.status, 400);
    CHECK(r.contentType == "application/json");
    JsonDocument d = parseJson(r.body);
    CHECK(strcmp(d["error"] | "", credsErrorMessage(CREDS_ERROR::PASSWORD_INVALID)) == 0);
    CHECK(r.body.find("short") == std::string::npos); //the password isn't echoed

    CHECK_EQ(save("not json").status, 400);
    CHECK_EQ(save("{\"password\":\"12345678\"}").status, 400);
    CHECK_EQ(save(std::string(2000, ' ')).status, 413);

    WifiCredentials c;
    CHECK(!p.takeCredentials(c)); //nothing got through
}
