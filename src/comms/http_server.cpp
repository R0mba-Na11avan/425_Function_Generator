/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: shared HTTP server (esp_http_server) with a route table so other modules can register handlers
**************************************************************************/

#include "http_server.h"

#include <string.h>
#include "esp_http_server.h"

static const int RECV_TIMEOUT_RETRIES = 2;

static httpd_method_t toHttpd(HTTP_METHOD method) {
    return (method == HTTP_METHOD::POST) ? HTTP_POST : HTTP_GET;
}

//HttpRequest

HttpRequest::HttpRequest(httpd_req* req) : _req(req), _responded(false) {}

const char* HttpRequest::uri() const {
    return _req->uri;
}

HTTP_METHOD HttpRequest::method() const {
    return (_req->method == HTTP_POST) ? HTTP_METHOD::POST : HTTP_METHOD::GET;
}

size_t HttpRequest::contentLength() const {
    return _req->content_len;
}

bool HttpRequest::header(const char* name, char* out, size_t len) {
    size_t valueLen = httpd_req_get_hdr_value_len(_req, name);
    if (valueLen == 0 || valueLen >= len) return false;
    return httpd_req_get_hdr_value_str(_req, name, out, len) == ESP_OK;
}

bool HttpRequest::readBody(char* buf, size_t bufSize, size_t& len) {
    len = 0;
    size_t max = bufSize ? bufSize - 1 : 0;
    if (max > HTTP_MAX_BODY) max = HTTP_MAX_BODY;

    size_t total = _req->content_len;
    if (total > max) {
        //don't make the server read a huge body just to throw it away
        setHeader("Connection", "close");
        sendError(413, "request body too large");
        return false;
    }

    int timeouts = 0;
    while (len < total) {
        int got = httpd_req_recv(_req, buf + len, total - len);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            //each timeout is a full recv_wait_timeout (5 s), so a stalled client
            //can't hold the server task forever
            if (++timeouts >= RECV_TIMEOUT_RETRIES) {
                sendError(408, "timed out waiting for the request body");
                return false;
            }
            continue;
        }
        if (got <= 0) {
            sendError(400, "request body ended early");
            return false;
        }
        len += got;
    }
    buf[len] = '\0';
    return true;
}

void HttpRequest::setHeader(const char* name, const char* value) {
    httpd_resp_set_hdr(_req, name, value);
}

void HttpRequest::send(int status, const char* contentType, const char* body, size_t len) {
    httpd_resp_set_status(_req, httpStatusLine(status));
    if (contentType) httpd_resp_set_type(_req, contentType);
    httpd_resp_send(_req, body, len);
    _responded = true;
}

void HttpRequest::sendJson(int status, const char* json) {
    send(status, "application/json", json, strlen(json));
}

void HttpRequest::sendText(int status, const char* text) {
    send(status, "text/plain", text, strlen(text));
}

void HttpRequest::sendEmpty(int status) {
    send(status, nullptr, nullptr, 0);
}

void HttpRequest::sendError(int status, const char* message) {
    sendText(status, message);
}

const char* httpStatusLine(int status) {
    switch (status) {
        case 200: return "200 OK";
        case 201: return "201 Created";
        case 202: return "202 Accepted";
        case 204: return "204 No Content";
        case 400: return "400 Bad Request";
        case 403: return "403 Forbidden";
        case 404: return "404 Not Found";
        case 405: return "405 Method Not Allowed";
        case 406: return "406 Not Acceptable";
        case 408: return "408 Request Timeout";
        case 413: return "413 Payload Too Large";
        case 415: return "415 Unsupported Media Type";
        case 500: return "500 Internal Server Error";
        case 503: return "503 Service Unavailable";
        default: break;
    }
    if (status >= 200 && status < 300) return "200 OK";
    if (status >= 400 && status < 500) return "400 Bad Request";
    return "500 Internal Server Error";
}

//HttpServer

HttpServer::HttpServer() : _server(nullptr), _lock(xSemaphoreCreateMutex()) {
    memset(_routes, 0, sizeof(_routes));
}

bool HttpServer::begin() {
    xSemaphoreTake(_lock, portMAX_DELAY);

    if (_server) {
        httpd_stop(_server);
        _server = nullptr;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = HTTP_PORT;
    config.max_uri_handlers = HTTP_MAX_ROUTES;
    config.stack_size = HTTP_TASK_STACK;
    config.task_priority = HTTP_TASK_PRIO;
    config.core_id = HTTP_TASK_CORE;
    config.lru_purge_enable = true; //drop the oldest idle connection instead of refusing new ones

    httpd_handle_t handle = nullptr;
    bool ok = httpd_start(&handle, &config) == ESP_OK;
    if (ok) {
        _server = handle;
        for (uint8_t i = 0; i < HTTP_MAX_ROUTES; i++) {
            if (_routes[i].used && !attach(_routes[i])) ok = false;
        }
    }

    xSemaphoreGive(_lock);
    return ok;
}

void HttpServer::stop() {
    xSemaphoreTake(_lock, portMAX_DELAY);
    if (_server) {
        httpd_stop(_server);
        _server = nullptr;
    }
    xSemaphoreGive(_lock);
}

bool HttpServer::running() const {
    return _server != nullptr;
}

bool HttpServer::registerRoute(const char* path, HTTP_METHOD method, Handler handler, void* ctx) {
    if (!path || !handler || strlen(path) >= HTTP_MAX_PATH_LEN) return false;

    xSemaphoreTake(_lock, portMAX_DELAY);
    bool ok = false;
    if (!find(path, method)) {
        for (uint8_t i = 0; i < HTTP_MAX_ROUTES; i++) {
            Route& r = _routes[i];
            if (r.used) continue;
            r.used = true;
            strncpy(r.path, path, sizeof(r.path));
            r.method = method;
            r.handler = handler;
            r.ctx = ctx;
            ok = !_server || attach(r);
            if (!ok) r.used = false;
            break;
        }
    }
    xSemaphoreGive(_lock);
    return ok;
}

bool HttpServer::unregisterRoute(const char* path, HTTP_METHOD method) {
    xSemaphoreTake(_lock, portMAX_DELAY);
    Route* r = find(path, method);
    if (r) {
        if (_server) httpd_unregister_uri_handler(_server, r->path, toHttpd(method));
        r->used = false;
    }
    xSemaphoreGive(_lock);
    return r != nullptr;
}

int HttpServer::dispatch(httpd_req* req) {
    Route* route = (Route*)req->user_ctx;
    HttpRequest request(req);
    route->handler(request, route->ctx);
    if (!request.responded()) request.sendError(500, "handler sent no response");
    return ESP_OK;
}

bool HttpServer::attach(Route& route) {
    httpd_uri_t uri = {};
    uri.uri = route.path;
    uri.method = toHttpd(route.method);
    uri.handler = dispatch;
    uri.user_ctx = &route;
    return httpd_register_uri_handler(_server, &uri) == ESP_OK;
}

HttpServer::Route* HttpServer::find(const char* path, HTTP_METHOD method) {
    for (uint8_t i = 0; i < HTTP_MAX_ROUTES; i++) {
        Route& r = _routes[i];
        if (r.used && r.method == method && strcmp(r.path, path) == 0) return &r;
    }
    return nullptr;
}
