/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: MCP server so AI agents can control the function generator.
             implements MCP over Streamable HTTP at POST /mcp
**************************************************************************/

/*
    Scope (decided)
        - basic tools only, no auth, stateless plain-JSON responses. no SSE
          stream, no Mcp-Session-Id, no server-initiated notifications
        - revisit auth if the device is ever exposed beyond a trusted LAN

    Transport
        - POST /mcp, body = one JSON-RPC 2.0 message, response = application/json
        - GET /mcp -> 405 (no SSE stream offered). other methods get 405 from
          esp_http_server since no route exists for them
        - notifications and responses from the client (no "id") -> 202, empty body
        - batches (JSON arrays) -> -32600, they were dropped from MCP in 2025-06-18
        - body capped at HTTP_MAX_BODY (413 beyond)

    Request checks (before any JSON is parsed)
        - Content-Type must be application/json (415). a cross-site form or a
          "simple" fetch can't send that, so a web page can't reach the tools
          without a CORS preflight, which this server never answers
        - Origin, if present, must pass originAllowed (api_json.h, 403):
          http(s)://<MDNS_HOSTNAME>.local or an IPv4 address. this stops DNS
          rebinding, as the MCP spec requires. clients outside a browser send
          no Origin
        - MCP-Protocol-Version, if present, must be one we support (400)

    JSON-RPC methods
        - initialize: echoes the client's protocolVersion if supported, else our
          newest. capabilities { tools: { listChanged: false } }, serverInfo and
          short instructions for the agent
        - ping: empty result
        - tools/list: built from the tool table, schemas use controller.limits()
        - tools/call: unknown tool or params not an object -> -32602. bad
          arguments (missing, wrong type, out of range, unknown) -> a tool
          result with isError: true and a message the model can act on.
          success -> text + structuredContent with the state that was actually applied
        - anything else -32601, malformed JSON -32700 (HTTP 400), not a JSON-RPC
          2.0 object -32600 (HTTP 400 without a usable id)

    Tools (TOOLS table in mcp_server.cpp, one entry each)
        - set_waveform { channel, waveform: "sine" | "square" | "triangle" }
        - set_frequency { channel, hz }, set_amplitude { channel, vpp },
          set_phase { channel, degrees }, set_dc_offset { channel, volts },
          set_output { channel, enabled }
        - set_mirror { enabled }: CH2 follows CH1 (see funcgen_controller.h)
        - get_state { channel? }: one channel, or every channel without it
        - get_limits (shared by both channels, plus "channels": 2)
        - channel is 1 or 2 and required on every set_* tool except
          set_mirror, so an agent never changes the wrong output by leaving it out
        - out-of-range values are rejected, never clamped (the LCD clamps instead)
        - a mirrored setting on channel 2 while the mirror is on is rejected
          the same way, with a message saying how to change it
        - set_* results: that channel's state + "mirror". set_mirror and
          get_state without a channel: { "channels": [ ... ], "mirror" }

    Threading
        - handlers run in the HTTP task. the controller's mutex serializes them
          with the LCD. no state is kept between requests
*/

#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include "../config.h"
#include "../core/funcgen_controller.h"
#include "http_server.h"

//true for protocol versions this server speaks
bool mcpVersionSupported(const char* version);

class McpServer {
    public:
        McpServer(FuncGenController& controller, HttpServer& http);

        //registers POST/GET MCP_PATH. works before or after http.begin()
        bool begin();

        //handles one JSON-RPC message (the body of a POST). writes the response
        //into out (malloc'd, caller frees) and returns the HTTP status. *out is
        //nullptr for 202. public so the tests can call it without HTTP
        int handleMessage(const char* body, size_t len, char** out);

    private:
        static void handlePost(HttpRequest& req, void* ctx);
        static void handleGet(HttpRequest& req, void* ctx);

        FuncGenController& _controller;
        HttpServer& _http;
};

#endif
