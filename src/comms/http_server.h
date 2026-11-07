/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: the one shared HTTP server. other modules register their routes
             on it so there is only a single listening socket/task
**************************************************************************/

/*
    Why shared
        - the ESP32 has a limited number of sockets and RAM. WifiManager
          (provisioning) and McpServer (and a later REST API/web UI) each add
          routes to the same server instead of starting their own

    Implementation
        - esp_http_server (ships with the Arduino-ESP32 core). handlers run in
          the httpd task (config.h: HTTP_TASK_*), which is fine because every
          write goes through FuncGenController's mutex
        - routes live in a table here, so begin() can restart the server (e.g.
          after a Wi-Fi mode change) and re-register all of them. routes can be
          added or removed before or after begin()
        - callers only see HttpRequest, never the esp-idf types

    Limits
        - request bodies are capped at HTTP_MAX_BODY, beyond that 413
        - no TLS and no auth, by choice, for now. this is a bench instrument on
          a trusted network
*/

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "../config.h"

struct httpd_req; //esp_http_server's request, only used inside http_server.cpp

enum class HTTP_METHOD : uint8_t {
    GET,
    POST
};

//one request, valid only inside the handler it was passed to
class HttpRequest {
    public:
        explicit HttpRequest(struct httpd_req* req);

        const char* uri() const;
        HTTP_METHOD method() const;
        size_t contentLength() const;

        //copies a request header into out (NUL terminated). false if missing or too long
        bool header(const char* name, char* out, size_t len);

        //reads the whole body into buf and NUL terminates it. on failure it has
        //already answered (413 too large, 408 timeout, 400 broken) and returns false
        bool readBody(char* buf, size_t bufSize, size_t& len);

        //extra response header. name and value must stay valid until the response is sent
        void setHeader(const char* name, const char* value);

        void send(int status, const char* contentType, const char* body, size_t len);
        void sendJson(int status, const char* json);
        void sendText(int status, const char* text);
        void sendEmpty(int status);
        void sendError(int status, const char* message); //text/plain

        bool responded() const { return _responded; }

    private:
        struct httpd_req* _req;
        bool _responded;
};

//"200 OK", "413 Payload Too Large", ... (anything unknown maps to its class, e.g. "400 Bad Request")
const char* httpStatusLine(int status);

class HttpServer {
    public:
        //must send a response. if it doesn't, the server answers 500
        using Handler = void (*)(HttpRequest& request, void* ctx);

        HttpServer();

        //(re)starts the server on HTTP_PORT and registers every route. call once
        //Wi-Fi (STA or AP) is up, again after a mode change
        bool begin();
        void stop();
        bool running() const;

        //false if the path is too long, the table is full or path + method already exist
        bool registerRoute(const char* path, HTTP_METHOD method, Handler handler, void* ctx = nullptr);
        bool unregisterRoute(const char* path, HTTP_METHOD method);

    private:
        struct Route {
            bool used;
            char path[HTTP_MAX_PATH_LEN];
            HTTP_METHOD method;
            Handler handler;
            void* ctx;
        };

        //esp_http_server handler, user_ctx is the Route. returns esp_err_t
        static int dispatch(struct httpd_req* req);
        bool attach(Route& route);
        Route* find(const char* path, HTTP_METHOD method);

        Route _routes[HTTP_MAX_ROUTES];
        void* _server; //httpd_handle_t
        SemaphoreHandle_t _lock;
};

#endif
