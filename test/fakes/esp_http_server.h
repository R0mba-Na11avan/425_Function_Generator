/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host-test stand-in for ESP-IDF's esp_http_server. keeps the
             registered handlers and lets a test push a request through them
             (fakeHttpRequest) and look at the response. mimics the real
             server where it matters: exact URI match, 404/405, 500 when a
             handler fails, recv in chunks, recv timeouts
**************************************************************************/

#ifndef FAKE_ESP_HTTP_SERVER_H
#define FAKE_ESP_HTTP_SERVER_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <map>
#include <string>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_HTTPD_HANDLERS_FULL 0xb001
#define ESP_ERR_HTTPD_HANDLER_EXISTS 0xb002

enum http_method {
    HTTP_DELETE = 0,
    HTTP_GET = 1,
    HTTP_HEAD = 2,
    HTTP_POST = 3
};
typedef enum http_method httpd_method_t;

typedef void* httpd_handle_t;

#define HTTPD_MAX_URI_LEN 512
#define HTTPD_SOCK_ERR_TIMEOUT -3
#define HTTPD_RESP_USE_STRLEN -1

typedef struct httpd_req {
    httpd_handle_t handle;
    int method;
    char uri[HTTPD_MAX_URI_LEN + 1];
    size_t content_len;
    void* aux;
    void* user_ctx;
} httpd_req_t;

typedef struct httpd_uri {
    const char* uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t* r);
    void* user_ctx;
} httpd_uri_t;

typedef struct {
    unsigned task_priority;
    size_t stack_size;
    int core_id;
    uint16_t server_port;
    uint16_t max_open_sockets;
    uint16_t max_uri_handlers;
    bool lru_purge_enable;
} httpd_config_t;

#define HTTPD_DEFAULT_CONFIG() httpd_config_t{5, 4096, 0x7fffffff, 80, 7, 8, false}

esp_err_t httpd_start(httpd_handle_t* handle, const httpd_config_t* config);
esp_err_t httpd_stop(httpd_handle_t handle);
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t* uri_handler);
esp_err_t httpd_unregister_uri_handler(httpd_handle_t handle, const char* uri, httpd_method_t method);
int httpd_req_recv(httpd_req_t* r, char* buf, size_t buf_len);
size_t httpd_req_get_hdr_value_len(httpd_req_t* r, const char* field);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t* r, const char* field, char* val, size_t val_size);
esp_err_t httpd_resp_set_status(httpd_req_t* r, const char* status);
esp_err_t httpd_resp_set_type(httpd_req_t* r, const char* type);
esp_err_t httpd_resp_set_hdr(httpd_req_t* r, const char* field, const char* value);
esp_err_t httpd_resp_send(httpd_req_t* r, const char* buf, ssize_t buf_len);

//test side

struct FakeHttpResponse {
    bool sent;              //false if the handler never answered
    int status;             //parsed from the status line
    std::string statusLine;
    std::string contentType;
    std::string body;
    std::map<std::string, std::string> headers;
};

struct FakeHttpOptions {
    std::map<std::string, std::string> headers;
    long declaredLength = -1;   //Content-Length, -1 = the body's size
    size_t chunk = 1 << 20;     //max bytes per recv
    int timeouts = 0;           //recv calls that time out before data arrives
    bool closeEarly = false;    //connection drops halfway through the body
};

FakeHttpResponse fakeHttpRequest(httpd_method_t method, const char* uri, const std::string& body = "",
                                 const FakeHttpOptions& options = FakeHttpOptions());

struct FakeHttpdState {
    bool running;
    int starts;
    int stops;
    size_t handlers;
    httpd_config_t config;
};

FakeHttpdState fakeHttpdState();
void fakeHttpdReset();

#endif
