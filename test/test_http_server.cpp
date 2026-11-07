/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host tests for HttpServer / HttpRequest against the fake
             esp_http_server: route table, restarts, body reading limits
**************************************************************************/

#include "test.h"
#include <string.h>
#include <string>
#include "esp_http_server.h"
#include "../src/comms/http_server.h"

struct Recorder {
    int calls = 0;
    std::string lastBody;
};

static void textHandler(HttpRequest& req, void* ctx) {
    if (ctx) ((Recorder*)ctx)->calls++;
    req.sendText(200, "hello");
}

static void echoHandler(HttpRequest& req, void* ctx) {
    char buf[64];
    size_t len;
    if (!req.readBody(buf, sizeof(buf), len)) return;
    if (ctx) ((Recorder*)ctx)->lastBody.assign(buf, len);
    req.sendJson(200, buf);
}

static void bigEchoHandler(HttpRequest& req, void*) {
    static char buf[HTTP_MAX_BODY + 100];
    size_t len;
    if (!req.readBody(buf, sizeof(buf), len)) return;
    req.send(200, "text/plain", buf, len);
}

static void silentHandler(HttpRequest&, void*) {}

static void headerHandler(HttpRequest& req, void*) {
    char value[16];
    if (req.header("X-Test", value, sizeof(value))) req.sendText(200, value);
    else req.sendError(400, "no header");
}

static void statusHandler(HttpRequest& req, void*) {
    req.setHeader("Allow", "POST");
    req.sendEmpty(202);
}

TEST(http_routes_before_and_after_begin) {
    fakeHttpdReset();
    HttpServer http;
    Recorder rec;
    CHECK(http.registerRoute("/a", HTTP_METHOD::GET, textHandler, &rec));
    CHECK(!http.running());
    CHECK(http.begin());
    CHECK(http.running());

    FakeHttpResponse r = fakeHttpRequest(HTTP_GET, "/a");
    CHECK_EQ(r.status, 200);
    CHECK(r.body == "hello");
    CHECK(r.contentType == "text/plain");
    CHECK_EQ(rec.calls, 1); //ctx passed through

    CHECK(http.registerRoute("/b", HTTP_METHOD::POST, echoHandler)); //while running
    r = fakeHttpRequest(HTTP_POST, "/b", "{\"x\":1}");
    CHECK_EQ(r.status, 200);
    CHECK(r.body == "{\"x\":1}");
    CHECK(r.contentType == "application/json");

    CHECK_EQ(fakeHttpRequest(HTTP_GET, "/nope").status, 404);
    CHECK_EQ(fakeHttpRequest(HTTP_GET, "/b").status, 405);
}

TEST(http_config_from_config_h) {
    fakeHttpdReset();
    HttpServer http;
    http.begin();
    httpd_config_t c = fakeHttpdState().config;
    CHECK_EQ(c.server_port, HTTP_PORT);
    CHECK_EQ(c.max_uri_handlers, HTTP_MAX_ROUTES);
    CHECK_EQ(c.stack_size, HTTP_TASK_STACK);
    CHECK_EQ(c.task_priority, HTTP_TASK_PRIO);
    CHECK_EQ(c.core_id, HTTP_TASK_CORE);
    CHECK(c.lru_purge_enable);
}

TEST(http_restart_reregisters_routes) {
    fakeHttpdReset();
    HttpServer http;
    http.registerRoute("/a", HTTP_METHOD::GET, textHandler);
    http.registerRoute("/b", HTTP_METHOD::POST, echoHandler);
    http.begin();
    CHECK(http.begin()); //e.g. Wi-Fi mode change
    FakeHttpdState s = fakeHttpdState();
    CHECK_EQ(s.starts, 2);
    CHECK_EQ(s.stops, 1);
    CHECK_EQ(s.handlers, 2u);
    CHECK_EQ(fakeHttpRequest(HTTP_GET, "/a").status, 200);

    http.stop();
    CHECK(!http.running());
    CHECK(!fakeHttpdState().running);
    CHECK(http.begin());
    CHECK_EQ(fakeHttpRequest(HTTP_POST, "/b", "hi").status, 200);
}

TEST(http_register_rejects_bad_routes) {
    fakeHttpdReset();
    HttpServer http;
    CHECK(http.registerRoute("/a", HTTP_METHOD::GET, textHandler));
    CHECK(!http.registerRoute("/a", HTTP_METHOD::GET, textHandler)); //duplicate
    CHECK(http.registerRoute("/a", HTTP_METHOD::POST, echoHandler));  //same path, other method is fine
    CHECK(!http.registerRoute("/a-path-that-is-much-too-long-to-fit", HTTP_METHOD::GET, textHandler));
    CHECK(!http.registerRoute("/x", HTTP_METHOD::GET, nullptr));

    char path[16];
    int added = 2;
    for (int i = 0; i < HTTP_MAX_ROUTES + 2; i++) {
        snprintf(path, sizeof(path), "/r%d", i);
        if (http.registerRoute(path, HTTP_METHOD::GET, textHandler)) added++;
    }
    CHECK_EQ(added, HTTP_MAX_ROUTES); //table full
    CHECK(http.begin());
    CHECK_EQ(fakeHttpdState().handlers, (size_t)HTTP_MAX_ROUTES);
}

TEST(http_unregister) {
    fakeHttpdReset();
    HttpServer http;
    http.registerRoute("/setup", HTTP_METHOD::GET, textHandler);
    http.registerRoute("/mcp", HTTP_METHOD::POST, echoHandler);
    http.begin();
    CHECK(http.unregisterRoute("/setup", HTTP_METHOD::GET));
    CHECK_EQ(fakeHttpRequest(HTTP_GET, "/setup").status, 404);
    CHECK(!http.unregisterRoute("/setup", HTTP_METHOD::GET)); //already gone
    CHECK_EQ(fakeHttpRequest(HTTP_POST, "/mcp", "x").status, 200);

    //gone after a restart too, and the slot can be reused
    http.begin();
    CHECK_EQ(fakeHttpRequest(HTTP_GET, "/setup").status, 404);
    CHECK(http.registerRoute("/setup", HTTP_METHOD::GET, textHandler));
    CHECK_EQ(fakeHttpRequest(HTTP_GET, "/setup").status, 200);
}

TEST(http_handler_without_response_gets_500) {
    fakeHttpdReset();
    HttpServer http;
    http.registerRoute("/silent", HTTP_METHOD::GET, silentHandler);
    http.begin();
    FakeHttpResponse r = fakeHttpRequest(HTTP_GET, "/silent");
    CHECK(r.sent);
    CHECK_EQ(r.status, 500);
}

TEST(http_read_body_in_chunks_and_after_timeouts) {
    fakeHttpdReset();
    HttpServer http;
    Recorder rec;
    http.registerRoute("/echo", HTTP_METHOD::POST, echoHandler, &rec);
    http.begin();

    FakeHttpOptions o;
    o.chunk = 3;
    o.timeouts = 1; //one slow packet is fine
    FakeHttpResponse r = fakeHttpRequest(HTTP_POST, "/echo", "split into pieces", o);
    CHECK_EQ(r.status, 200);
    CHECK(rec.lastBody == "split into pieces");

    r = fakeHttpRequest(HTTP_POST, "/echo", "");
    CHECK_EQ(r.status, 200);
    CHECK(rec.lastBody.empty());
}

TEST(http_read_body_errors) {
    fakeHttpdReset();
    HttpServer http;
    http.registerRoute("/echo", HTTP_METHOD::POST, echoHandler);
    http.registerRoute("/big", HTTP_METHOD::POST, bigEchoHandler);
    http.begin();

    //bigger than the handler's 64 byte buffer
    FakeHttpResponse r = fakeHttpRequest(HTTP_POST, "/echo", std::string(100, 'a'));
    CHECK_EQ(r.status, 413);
    CHECK(r.headers["Connection"] == "close");

    //bigger than HTTP_MAX_BODY even though the buffer could take it
    r = fakeHttpRequest(HTTP_POST, "/big", std::string(HTTP_MAX_BODY + 1, 'a'));
    CHECK_EQ(r.status, 413);
    r = fakeHttpRequest(HTTP_POST, "/big", std::string(HTTP_MAX_BODY, 'a'));
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.body.size(), (size_t)HTTP_MAX_BODY);

    //client stalls
    FakeHttpOptions stall;
    stall.timeouts = 100;
    r = fakeHttpRequest(HTTP_POST, "/echo", "hello", stall);
    CHECK_EQ(r.status, 408);

    //connection drops halfway
    FakeHttpOptions drop;
    drop.closeEarly = true;
    r = fakeHttpRequest(HTTP_POST, "/echo", "hello world", drop);
    CHECK_EQ(r.status, 400);

    //Content-Length larger than what arrives
    FakeHttpOptions lie;
    lie.declaredLength = 20;
    r = fakeHttpRequest(HTTP_POST, "/echo", "short", lie);
    CHECK_EQ(r.status, 400);
}

TEST(http_request_headers) {
    fakeHttpdReset();
    HttpServer http;
    http.registerRoute("/h", HTTP_METHOD::GET, headerHandler);
    http.begin();

    FakeHttpOptions o;
    o.headers["X-Test"] = "abc";
    FakeHttpResponse r = fakeHttpRequest(HTTP_GET, "/h", "", o);
    CHECK_EQ(r.status, 200);
    CHECK(r.body == "abc");

    CHECK_EQ(fakeHttpRequest(HTTP_GET, "/h").status, 400); //missing

    o.headers["X-Test"] = std::string(40, 'x'); //longer than the handler's buffer
    CHECK_EQ(fakeHttpRequest(HTTP_GET, "/h", "", o).status, 400);
}

TEST(http_response_status_and_headers) {
    fakeHttpdReset();
    HttpServer http;
    http.registerRoute("/s", HTTP_METHOD::POST, statusHandler);
    http.begin();
    FakeHttpResponse r = fakeHttpRequest(HTTP_POST, "/s");
    CHECK_EQ(r.status, 202);
    CHECK(r.statusLine == "202 Accepted");
    CHECK(r.body.empty());
    CHECK(r.headers["Allow"] == "POST");
}

TEST(http_status_lines) {
    CHECK(strcmp(httpStatusLine(200), "200 OK") == 0);
    CHECK(strcmp(httpStatusLine(405), "405 Method Not Allowed") == 0);
    CHECK(strcmp(httpStatusLine(413), "413 Payload Too Large") == 0);
    CHECK(strcmp(httpStatusLine(418), "400 Bad Request") == 0); //unknown 4xx
    CHECK(strcmp(httpStatusLine(299), "200 OK") == 0);
    CHECK(strcmp(httpStatusLine(999), "500 Internal Server Error") == 0);
}
