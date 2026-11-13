/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: MCP server (Streamable HTTP, POST /mcp): JSON-RPC dispatch plus the tool table
**************************************************************************/

#include "mcp_server.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "api_json.h"

//newest first. initialize falls back to the first one
static const char* const SUPPORTED_VERSIONS[] = {"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};

static const int RPC_PARSE_ERROR = -32700;
static const int RPC_INVALID_REQUEST = -32600;
static const int RPC_METHOD_NOT_FOUND = -32601;
static const int RPC_INVALID_PARAMS = -32602;
static const int RPC_INTERNAL_ERROR = -32603;

static const char* const INSTRUCTIONS =
    "Controls a bench function generator (AD9833 DDS with a digital-pot amplitude stage). "
    "Changes take effect on the physical output immediately. Call get_limits before setting "
    "values: out-of-range values are rejected, not clamped, and the amplitude range depends on "
    "the waveform. Every set_* tool returns the state that was actually applied. The LCD can "
    "change the settings too, so call get_state when in doubt.";

//tool table

enum class PARAM : uint8_t {
    NONE,
    NUMBER,
    BOOLEAN,
    WAVEFORM
};

struct ToolArg {
    float number;
    bool boolean;
    WAVEFORM waveform;
};

struct Tool {
    const char* name;
    const char* title;
    const char* description;
    PARAM param;
    const char* paramName;
    const char* paramDescription;
    const char* unit;   //NUMBER: for error messages
    //NUMBER: valid values. state is nullptr for the schema (widest range over all waveforms)
    ValueRange (*range)(const FuncGenLimits& limits, const FuncGenState* state);
    bool maxExclusive;  //NUMBER: the max itself is not allowed
    bool readOnly;
    //applies the argument (setters) and fills out with the result
    void (*run)(FuncGenController& controller, const ToolArg& arg, JsonObject out);
};

static void writeState(FuncGenController& c, JsonObject out) { writeStateJson(c.state(), out); }
static void writeLimits(FuncGenController& c, JsonObject out) { writeLimitsJson(c.limits(), out); }

static ValueRange frequencyRange(const FuncGenLimits& l, const FuncGenState*) { return l.frequency; }
static ValueRange phaseRange(const FuncGenLimits& l, const FuncGenState*) { return l.phase; }
//with a state: what may be set right now (peak rule, the other setting held where it is)
static ValueRange dcOffsetRange(const FuncGenLimits& l, const FuncGenState* state) {
    return state ? l.dcOffsetFor(*state) : l.dcOffset;
}

static ValueRange amplitudeRange(const FuncGenLimits& l, const FuncGenState* state) {
    if (state) return l.amplitudeFor(*state);
    ValueRange r = l.amplitude[0];
    for (uint8_t i = 1; i < WAVEFORM_COUNT; i++) {
        if (l.amplitude[i].min < r.min) r.min = l.amplitude[i].min;
        if (l.amplitude[i].max > r.max) r.max = l.amplitude[i].max;
    }
    return r;
}

static void runSetWaveform(FuncGenController& c, const ToolArg& a, JsonObject out) { c.setWaveform(a.waveform, CHANGE_SOURCE::MCP); writeState(c, out); }
static void runSetFrequency(FuncGenController& c, const ToolArg& a, JsonObject out) { c.setFrequency(a.number, CHANGE_SOURCE::MCP); writeState(c, out); }
static void runSetAmplitude(FuncGenController& c, const ToolArg& a, JsonObject out) { c.setAmplitude(a.number, CHANGE_SOURCE::MCP); writeState(c, out); }
static void runSetPhase(FuncGenController& c, const ToolArg& a, JsonObject out) { c.setPhase(a.number, CHANGE_SOURCE::MCP); writeState(c, out); }
static void runSetDCOffset(FuncGenController& c, const ToolArg& a, JsonObject out) { c.setDCOffset(a.number, CHANGE_SOURCE::MCP); writeState(c, out); }
static void runSetOutput(FuncGenController& c, const ToolArg& a, JsonObject out) { c.setOutputEnabled(a.boolean, CHANGE_SOURCE::MCP); writeState(c, out); }
static void runGetState(FuncGenController& c, const ToolArg&, JsonObject out) { writeState(c, out); }
static void runGetLimits(FuncGenController& c, const ToolArg&, JsonObject out) { writeLimits(c, out); }

//adding a tool = one entry here
static const Tool TOOLS[] = {
    {"set_waveform", "Set waveform",
     "Select the output waveform. If the current amplitude is outside the new waveform's range it is clamped into it.",
     PARAM::WAVEFORM, "waveform", "Output waveform", nullptr, nullptr, false, false, runSetWaveform},
    {"set_frequency", "Set frequency", "Set the output frequency in Hz.",
     PARAM::NUMBER, "hz", "Frequency in Hz", "Hz", frequencyRange, false, false, runSetFrequency},
    {"set_amplitude", "Set amplitude",
     "Set the peak-to-peak amplitude in volts. The valid range depends on the current waveform and DC offset: amplitude / 2 + |offset| must stay at or below output_peak_v (see get_limits). The offset is never changed by this call.",
     PARAM::NUMBER, "vpp", "Amplitude in volts peak-to-peak", "Vpp", amplitudeRange, false, false, runSetAmplitude},
    {"set_phase", "Set phase", "Set the phase offset in degrees, from 0 up to (not including) 360.",
     PARAM::NUMBER, "degrees", "Phase in degrees", "degrees", phaseRange, true, false, runSetPhase},
    {"set_dc_offset", "Set DC offset",
     "Set the DC offset in volts. The valid range depends on the current amplitude: amplitude / 2 + |offset| must stay at or below output_peak_v (see get_limits). The amplitude is never changed by this call.",
     PARAM::NUMBER, "volts", "DC offset in volts", "V", dcOffsetRange, false, false, runSetDCOffset},
    {"set_output", "Enable/disable output", "Turn the signal output on or off.",
     PARAM::BOOLEAN, "enabled", "true = output on", nullptr, nullptr, false, false, runSetOutput},
    {"get_state", "Get state", "Read the current settings.",
     PARAM::NONE, nullptr, nullptr, nullptr, nullptr, false, true, runGetState},
    {"get_limits", "Get limits",
     "Read the valid range of every setting (amplitude per waveform) and output_peak_v. Amplitude and DC offset also share one limit: amplitude / 2 + |offset| <= output_peak_v. Values outside these ranges are rejected.",
     PARAM::NONE, nullptr, nullptr, nullptr, nullptr, false, true, runGetLimits},
};

static const size_t TOOL_COUNT = sizeof(TOOLS) / sizeof(TOOLS[0]);

static const Tool* findTool(const char* name) {
    for (size_t i = 0; i < TOOL_COUNT; i++) {
        if (strcmp(TOOLS[i].name, name) == 0) return &TOOLS[i];
    }
    return nullptr;
}

static void writeToolSchema(const Tool& t, const FuncGenLimits& limits, JsonObject out) {
    out["name"] = t.name;
    out["title"] = t.title;
    out["description"] = t.description;

    JsonObject schema = out["inputSchema"].to<JsonObject>();
    schema["type"] = "object";
    JsonObject props = schema["properties"].to<JsonObject>();
    if (t.param != PARAM::NONE) {
        JsonObject p = props[t.paramName].to<JsonObject>();
        p["description"] = t.paramDescription;
        if (t.param == PARAM::NUMBER) {
            ValueRange r = t.range(limits, nullptr);
            p["type"] = "number";
            p["minimum"] = r.min;
            p[t.maxExclusive ? "exclusiveMaximum" : "maximum"] = r.max;
        } else if (t.param == PARAM::BOOLEAN) {
            p["type"] = "boolean";
        } else {
            p["type"] = "string";
            JsonArray e = p["enum"].to<JsonArray>();
            for (uint8_t i = 0; i < WAVEFORM_COUNT; i++) e.add(waveformJsonName((WAVEFORM)i));
        }
        schema["required"].to<JsonArray>().add(t.paramName);
    }
    schema["additionalProperties"] = false;

    JsonObject hints = out["annotations"].to<JsonObject>();
    hints["readOnlyHint"] = t.readOnly;
    hints["destructiveHint"] = false;
    hints["idempotentHint"] = true;
    hints["openWorldHint"] = false;
}

//checks the arguments against the tool. false + message (for an isError result) if they don't fit
static bool readArgument(const Tool& t, JsonObjectConst args, const FuncGenLimits& limits,
                         const FuncGenState& state, ToolArg& out, char* err, size_t len) {
    for (JsonPairConst kv : args) {
        if (t.param == PARAM::NONE || strcmp(kv.key().c_str(), t.paramName) != 0) {
            if (t.param == PARAM::NONE) snprintf(err, len, "unknown argument '%.32s', %s takes no arguments", kv.key().c_str(), t.name);
            else snprintf(err, len, "unknown argument '%.32s', %s only takes '%s'", kv.key().c_str(), t.name, t.paramName);
            return false;
        }
    }
    if (t.param == PARAM::NONE) return true;

    JsonVariantConst v = args[t.paramName];
    if (v.isNull()) {
        snprintf(err, len, "missing required argument '%s'", t.paramName);
        return false;
    }

    switch (t.param) {
        case PARAM::NUMBER: {
            if (!v.is<double>()) {
                snprintf(err, len, "'%s' must be a number in %s", t.paramName, t.unit);
                return false;
            }
            double d = v.as<double>();
            ValueRange r = t.range(limits, &state);
            //compared as float: the limits are floats, and 0.1 must pass a 0.1f minimum.
            //(ArduinoJson 7.4 already parses short decimals as float, this keeps it right if that changes)
            float f = (float)d;
            bool aboveMax = t.maxExclusive ? (f >= r.max) : (f > r.max);
            if (!isfinite(d) || f < r.min || aboveMax) {
                char lo[24], hi[24], got[24];
                formatNumber(r.min, lo, sizeof(lo));
                formatNumber(r.max, hi, sizeof(hi));
                formatNumber(d, got, sizeof(got));
                //amplitude and offset ranges depend on the current settings, so say which
                char forWave[64] = "";
                char other[24];
                if (t.range == amplitudeRange) {
                    formatNumber(state.dcOffset, other, sizeof(other));
                    snprintf(forWave, sizeof(forWave), " for the %s waveform at %s V offset", waveformJsonName(state.waveform), other);
                } else if (t.range == dcOffsetRange) {
                    formatNumber(state.amplitude, other, sizeof(other));
                    snprintf(forWave, sizeof(forWave), " at %s Vpp amplitude", other);
                }
                if (t.maxExclusive) {
                    snprintf(err, len, "'%s' must be at least %s and less than %s %s, got %s", t.paramName, lo, hi, t.unit, got);
                } else {
                    snprintf(err, len, "'%s' must be between %s and %s %s%s, got %s", t.paramName, lo, hi, t.unit, forWave, got);
                }
                return false;
            }
            out.number = f;
            return true;
        }
        case PARAM::BOOLEAN:
            if (!v.is<bool>()) {
                snprintf(err, len, "'%s' must be true or false", t.paramName);
                return false;
            }
            out.boolean = v.as<bool>();
            return true;
        case PARAM::WAVEFORM: {
            const char* name = v.is<const char*>() ? v.as<const char*>() : nullptr;
            if (parseWaveform(name, out.waveform)) return true;
            snprintf(err, len, "'%s' must be one of \"sine\", \"square\", \"triangle\"", t.paramName);
            return false;
        }
        case PARAM::NONE:
        default:
            return true;
    }
}

//version check

bool mcpVersionSupported(const char* version) {
    if (!version) return false;
    for (const char* v : SUPPORTED_VERSIONS) {
        if (strcmp(v, version) == 0) return true;
    }
    return false;
}

//JSON-RPC

static int finish(JsonDocument& resp, int status, char** out) {
    size_t size = measureJson(resp) + 1;
    *out = (char*)malloc(size);
    if (!*out) return 503;
    serializeJson(resp, *out, size);
    return status;
}

static JsonObject startResponse(JsonDocument& resp, JsonVariantConst id) {
    resp["jsonrpc"] = "2.0";
    if (id.isNull()) resp["id"] = nullptr;
    else resp["id"] = id;
    return resp.as<JsonObject>();
}

static int rpcError(JsonDocument& resp, JsonVariantConst id, int code, const char* message, int status, char** out) {
    JsonObject r = startResponse(resp, id);
    JsonObject e = r["error"].to<JsonObject>();
    e["code"] = code;
    e["message"] = message;
    return finish(resp, status, out);
}

static void initializeResult(JsonVariantConst params, JsonObject result) {
    const char* asked = params["protocolVersion"].is<const char*>() ? params["protocolVersion"].as<const char*>() : nullptr;
    result["protocolVersion"] = mcpVersionSupported(asked) ? asked : SUPPORTED_VERSIONS[0];
    result["capabilities"].to<JsonObject>()["tools"].to<JsonObject>()["listChanged"] = false;
    JsonObject info = result["serverInfo"].to<JsonObject>();
    info["name"] = MCP_SERVER_NAME;
    info["title"] = "Function Generator";
    info["version"] = FIRMWARE_VERSION;
    result["instructions"] = INSTRUCTIONS;
}

McpServer::McpServer(FuncGenController& controller, HttpServer& http) : _controller(controller), _http(http) {}

bool McpServer::begin() {
    return _http.registerRoute(MCP_PATH, HTTP_METHOD::POST, handlePost, this)
        && _http.registerRoute(MCP_PATH, HTTP_METHOD::GET, handleGet, this);
}

int McpServer::handleMessage(const char* body, size_t len, char** out) {
    *out = nullptr;
    JsonDocument resp;
    JsonVariantConst noId;

    JsonDocument req;
    if (deserializeJson(req, body, len)) return rpcError(resp, noId, RPC_PARSE_ERROR, "parse error: body is not valid JSON", 400, out);
    if (req.is<JsonArray>()) return rpcError(resp, noId, RPC_INVALID_REQUEST, "batch requests are not supported", 400, out);
    if (!req.is<JsonObject>()) return rpcError(resp, noId, RPC_INVALID_REQUEST, "expected a JSON-RPC 2.0 object", 400, out);

    JsonVariantConst id = req["id"];
    JsonVariantConst method = req["method"];
    bool validId = id.is<const char*>() || id.is<double>();

    //notifications, and responses to requests we never send: accept and ignore
    if (id.isNull() && (method.is<const char*>() || !req["result"].isNull() || !req["error"].isNull())) return 202;

    if (!validId) return rpcError(resp, noId, RPC_INVALID_REQUEST, "id must be a string or a number", 400, out);
    if (!req["jsonrpc"].is<const char*>() || strcmp(req["jsonrpc"].as<const char*>(), "2.0") != 0) {
        return rpcError(resp, id, RPC_INVALID_REQUEST, "jsonrpc must be \"2.0\"", 200, out);
    }
    if (!req["result"].isNull() || !req["error"].isNull()) return 202; //a response with an id
    if (!method.is<const char*>()) return rpcError(resp, id, RPC_INVALID_REQUEST, "method must be a string", 200, out);

    const char* m = method.as<const char*>();
    JsonVariantConst params = req["params"];
    if (!params.isNull() && !params.is<JsonObjectConst>()) {
        return rpcError(resp, id, RPC_INVALID_PARAMS, "params must be an object", 200, out);
    }

    if (strcmp(m, "initialize") == 0) {
        JsonObject r = startResponse(resp, id);
        initializeResult(params, r["result"].to<JsonObject>());
        return finish(resp, 200, out);
    }

    if (strcmp(m, "ping") == 0) {
        JsonObject r = startResponse(resp, id);
        r["result"].to<JsonObject>();
        return finish(resp, 200, out);
    }

    if (strcmp(m, "tools/list") == 0) {
        JsonObject r = startResponse(resp, id);
        JsonArray tools = r["result"].to<JsonObject>()["tools"].to<JsonArray>();
        FuncGenLimits limits = _controller.limits();
        for (size_t i = 0; i < TOOL_COUNT; i++) writeToolSchema(TOOLS[i], limits, tools.add<JsonObject>());
        return finish(resp, 200, out);
    }

    if (strcmp(m, "tools/call") == 0) {
        if (!params["name"].is<const char*>()) return rpcError(resp, id, RPC_INVALID_PARAMS, "params.name must be a string", 200, out);
        const char* name = params["name"].as<const char*>();
        const Tool* tool = findTool(name);
        if (!tool) {
            char msg[80];
            snprintf(msg, sizeof(msg), "unknown tool: %.40s", name);
            return rpcError(resp, id, RPC_INVALID_PARAMS, msg, 200, out);
        }
        JsonVariantConst args = params["arguments"];
        if (!args.isNull() && !args.is<JsonObjectConst>()) {
            return rpcError(resp, id, RPC_INVALID_PARAMS, "params.arguments must be an object", 200, out);
        }

        JsonObject r = startResponse(resp, id);
        JsonObject result = r["result"].to<JsonObject>();
        JsonObject text = result["content"].to<JsonArray>().add<JsonObject>();
        text["type"] = "text";

        ToolArg arg = {};
        char err[192];
        if (!readArgument(*tool, args.as<JsonObjectConst>(), _controller.limits(), _controller.state(), arg, err, sizeof(err))) {
            text["text"] = err;
            result["isError"] = true;
            return finish(resp, 200, out);
        }

        JsonObject structured = result["structuredContent"].to<JsonObject>();
        tool->run(_controller, arg, structured);
        //the same JSON as text, for clients that ignore structuredContent
        size_t size = measureJson(structured) + 1;
        char* json = (char*)malloc(size);
        if (!json) return rpcError(resp, id, RPC_INTERNAL_ERROR, "out of memory", 200, out);
        serializeJson(structured, json, size);
        text["text"] = (const char*)json; //only freed after finish(), whether the document copies it or not
        int status = finish(resp, 200, out);
        free(json);
        return status;
    }

    char msg[80];
    snprintf(msg, sizeof(msg), "method not found: %.40s", m);
    return rpcError(resp, id, RPC_METHOD_NOT_FOUND, msg, 200, out);
}

//HTTP

static void sendRpcError(HttpRequest& req, int status, int code, const char* message) {
    JsonDocument resp;
    JsonVariantConst noId;
    char* out;
    status = rpcError(resp, noId, code, message, status, &out);
    if (!out) {
        req.sendError(status, message);
        return;
    }
    req.sendJson(status, out);
    free(out);
}

void McpServer::handlePost(HttpRequest& req, void* ctx) {
    McpServer* self = (McpServer*)ctx;

    //checked before the body is read. see the header for why
    char value[96];
    if (req.hasHeader("Origin") && (!req.header("Origin", value, sizeof(value)) || !originAllowed(value))) {
        sendRpcError(req, 403, RPC_INVALID_REQUEST, "origin not allowed");
        return;
    }
    if (!req.header("Content-Type", value, sizeof(value)) || strncasecmp(value, "application/json", 16) != 0) {
        sendRpcError(req, 415, RPC_INVALID_REQUEST, "Content-Type must be application/json");
        return;
    }
    if (req.hasHeader("MCP-Protocol-Version") &&
        (!req.header("MCP-Protocol-Version", value, sizeof(value)) || !mcpVersionSupported(value))) {
        sendRpcError(req, 400, RPC_INVALID_REQUEST, "unsupported MCP-Protocol-Version");
        return;
    }

    char* body = (char*)malloc(HTTP_MAX_BODY + 1);
    if (!body) {
        sendRpcError(req, 503, RPC_INTERNAL_ERROR, "out of memory");
        return;
    }
    size_t len;
    if (!req.readBody(body, HTTP_MAX_BODY + 1, len)) {
        free(body);
        return;
    }

    char* out;
    int status = self->handleMessage(body, len, &out);
    free(body);
    if (out) {
        req.sendJson(status, out);
        free(out);
    } else {
        req.sendEmpty(status);
    }
}

void McpServer::handleGet(HttpRequest& req, void*) {
    req.setHeader("Allow", "POST");
    req.sendError(405, "this MCP server has no SSE stream, use POST");
}
