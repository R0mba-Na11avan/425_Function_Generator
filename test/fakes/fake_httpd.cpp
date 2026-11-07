/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: state behind the fake esp_http_server.h
**************************************************************************/

#include "esp_http_server.h"

#include <stdlib.h>
#include <string.h>
#include <vector>

struct Handler {
    std::string uri;
    httpd_method_t method;
    esp_err_t (*fn)(httpd_req_t*);
    void* ctx;
};

struct Server {
    bool running = false;
    int starts = 0;
    int stops = 0;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    std::vector<Handler> handlers;
};

//per request, behind httpd_req_t::aux
struct Exchange {
    std::string body;
    size_t pos = 0;
    FakeHttpOptions options;
    int timeoutsLeft = 0;
    FakeHttpResponse response;
};

static Server server;
static int serverToken; //its address is the handle

void fakeHttpdReset() {
    server = Server();
}

FakeHttpdState fakeHttpdState() {
    return FakeHttpdState{server.running, server.starts, server.stops, server.handlers.size(), server.config};
}

esp_err_t httpd_start(httpd_handle_t* handle, const httpd_config_t* config) {
    if (server.running) return ESP_FAIL; //only one fake server
    server.running = true;
    server.starts++;
    server.config = *config;
    server.handlers.clear();
    *handle = &serverToken;
    return ESP_OK;
}

esp_err_t httpd_stop(httpd_handle_t handle) {
    if (handle != &serverToken || !server.running) return ESP_ERR_INVALID_ARG;
    server.running = false;
    server.stops++;
    server.handlers.clear();
    return ESP_OK;
}

esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t* h) {
    if (handle != &serverToken || !server.running) return ESP_ERR_INVALID_ARG;
    for (const Handler& existing : server.handlers) {
        if (existing.uri == h->uri && existing.method == h->method) return ESP_ERR_HTTPD_HANDLER_EXISTS;
    }
    if (server.handlers.size() >= server.config.max_uri_handlers) return ESP_ERR_HTTPD_HANDLERS_FULL;
    server.handlers.push_back(Handler{h->uri, h->method, h->handler, h->user_ctx});
    return ESP_OK;
}

esp_err_t httpd_unregister_uri_handler(httpd_handle_t handle, const char* uri, httpd_method_t method) {
    if (handle != &serverToken) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < server.handlers.size(); i++) {
        if (server.handlers[i].uri == uri && server.handlers[i].method == method) {
            server.handlers.erase(server.handlers.begin() + i);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static Exchange& exchange(httpd_req_t* r) {
    return *(Exchange*)r->aux;
}

int httpd_req_recv(httpd_req_t* r, char* buf, size_t buf_len) {
    Exchange& x = exchange(r);
    if (x.timeoutsLeft > 0) {
        x.timeoutsLeft--;
        return HTTPD_SOCK_ERR_TIMEOUT;
    }
    size_t end = x.options.closeEarly ? x.body.size() / 2 : x.body.size();
    if (x.pos >= end) return 0;
    size_t n = end - x.pos;
    if (n > buf_len) n = buf_len;
    if (n > x.options.chunk) n = x.options.chunk;
    memcpy(buf, x.body.data() + x.pos, n);
    x.pos += n;
    return (int)n;
}

size_t httpd_req_get_hdr_value_len(httpd_req_t* r, const char* field) {
    auto& headers = exchange(r).options.headers;
    auto it = headers.find(field);
    return (it == headers.end()) ? 0 : it->second.size();
}

esp_err_t httpd_req_get_hdr_value_str(httpd_req_t* r, const char* field, char* val, size_t val_size) {
    auto& headers = exchange(r).options.headers;
    auto it = headers.find(field);
    if (it == headers.end()) return ESP_ERR_NOT_FOUND;
    if (it->second.size() >= val_size) return ESP_ERR_INVALID_ARG;
    strcpy(val, it->second.c_str());
    return ESP_OK;
}

esp_err_t httpd_resp_set_status(httpd_req_t* r, const char* status) {
    exchange(r).response.statusLine = status;
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t* r, const char* type) {
    exchange(r).response.contentType = type;
    return ESP_OK;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t* r, const char* field, const char* value) {
    exchange(r).response.headers[field] = value;
    return ESP_OK;
}

esp_err_t httpd_resp_send(httpd_req_t* r, const char* buf, ssize_t buf_len) {
    FakeHttpResponse& resp = exchange(r).response;
    if (buf_len == HTTPD_RESP_USE_STRLEN) buf_len = buf ? (ssize_t)strlen(buf) : 0;
    resp.body.assign(buf ? buf : "", buf ? (size_t)buf_len : 0);
    resp.sent = true;
    return ESP_OK;
}

static FakeHttpResponse simpleResponse(int status, const char* line) {
    FakeHttpResponse resp;
    resp.sent = true;
    resp.status = status;
    resp.statusLine = line;
    return resp;
}

FakeHttpResponse fakeHttpRequest(httpd_method_t method, const char* uri, const std::string& body,
                                 const FakeHttpOptions& options) {
    if (!server.running) return simpleResponse(0, "server not running");

    const Handler* match = nullptr;
    bool uriKnown = false;
    for (const Handler& h : server.handlers) {
        if (h.uri != uri) continue;
        uriKnown = true;
        if (h.method == method) match = &h;
    }
    if (!match) return uriKnown ? simpleResponse(405, "405 Method Not Allowed") : simpleResponse(404, "404 Not Found");

    Exchange x;
    x.body = body;
    x.options = options;
    x.timeoutsLeft = options.timeouts;
    x.response.sent = false;
    x.response.statusLine = "200 OK";
    x.response.contentType = "text/html"; //esp_http_server's default

    httpd_req_t req = {};
    req.handle = &serverToken;
    req.method = method;
    strncpy(req.uri, uri, HTTPD_MAX_URI_LEN);
    req.content_len = options.declaredLength >= 0 ? (size_t)options.declaredLength : body.size();
    req.aux = &x;
    req.user_ctx = match->ctx;

    if (match->fn(&req) != ESP_OK) return simpleResponse(500, "500 Internal Server Error");

    x.response.status = atoi(x.response.statusLine.c_str());
    return x.response;
}
