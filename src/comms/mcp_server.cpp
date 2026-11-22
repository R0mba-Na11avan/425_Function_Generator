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
    "Controls a two-channel bench function generator (one AD9833 DDS per channel, with DAC-set "
    "amplitude and DC offset). Changes take effect on the physical outputs immediately. Every "
    "set_* tool needs \"channel\": 1 or 2. Call get_limits before setting values: out-of-range "
    "values are rejected, not clamped, and the amplitude and offset ranges depend on each other. "
    "Mirror (set_mirror): while it is on, channel 2 follows channel 1's waveform, frequency, "
    "amplitude, phase and offset, and setting those on channel 2 is rejected; each channel's "
    "output on/off stays its own. Every set_* tool returns the state that was actually applied. "
    "The LCD can change the settings too, so call get_state when in doubt.";

//tool table

enum class PARAM : uint8_t {
    NONE,
    NUMBER,
    BOOLEAN,
    WAVEFORM
};

//whether a tool takes "channel"
enum class CHANNEL_ARG : uint8_t {
    NONE,
    REQUIRED,
    OPTIONAL    //left out = every channel
};

struct ToolArg {
    float number;
    bool boolean;
    WAVEFORM waveform;
    int8_t channel;     //index, -1 if left out
};

struct Tool {
    const char* name;
    const char* title;
    const char* description;
    PARAM param;
    CHANNEL_ARG channelArg;
    bool mirrored;      //a setting the mirror copies to CH2 (so it is locked there while on)
    const char* paramName;
    const char* paramDescription;
    const char* unit;   //NUMBER: for error messages
    //NUMBER: valid values. state is nullptr for the schema (widest range over all waveforms)
    ValueRange (*range)(const FuncGenLimits& limits, const FuncGenState* state);
    bool maxExclusive;  //NUMBER: the max itself is not allowed
    bool readOnly;
    //applies the argument (setters) and fills out with the result. false if
    //the controller refused (the mirror came on since the arguments were checked)
    bool (*run)(FuncGenController& controller, const ToolArg& arg, JsonObject out);
};

//one channel's state + the mirror flag, or every channel when channel is -1
static void writeState(FuncGenController& c, int8_t channel, JsonObject out) {
    if (channel >= 0) {
        writeStateJson((uint8_t)channel, c.state((uint8_t)channel), out);
    } else {
        JsonArray all = out["channels"].to<JsonArray>();
        for (uint8_t ch = 0; ch < CHANNEL_COUNT; ch++) writeStateJson(ch, c.state(ch), all.add<JsonObject>());
    }
    out["mirror"] = c.mirror();
}

static void writeLimits(FuncGenController& c, JsonObject out) {
    writeLimitsJson(c.limits(), out);
    out["channels"] = CHANNEL_COUNT;
}

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

static uint8_t channelOf(const ToolArg& a) { return (uint8_t)a.channel; }

static bool runSetWaveform(FuncGenController& c, const ToolArg& a, JsonObject out) {
    if (!c.setWaveform(channelOf(a), a.waveform, CHANGE_SOURCE::MCP)) return false;
    writeState(c, a.channel, out);
    return true;
}
static bool runSetFrequency(FuncGenController& c, const ToolArg& a, JsonObject out) {
    if (!c.setFrequency(channelOf(a), a.number, CHANGE_SOURCE::MCP)) return false;
    writeState(c, a.channel, out);
    return true;
}
static bool runSetAmplitude(FuncGenController& c, const ToolArg& a, JsonObject out) {
    if (!c.setAmplitude(channelOf(a), a.number, CHANGE_SOURCE::MCP)) return false;
    writeState(c, a.channel, out);
    return true;
}
static bool runSetPhase(FuncGenController& c, const ToolArg& a, JsonObject out) {
    if (!c.setPhase(channelOf(a), a.number, CHANGE_SOURCE::MCP)) return false;
    writeState(c, a.channel, out);
    return true;
}
static bool runSetDCOffset(FuncGenController& c, const ToolArg& a, JsonObject out) {
    if (!c.setDCOffset(channelOf(a), a.number, CHANGE_SOURCE::MCP)) return false;
    writeState(c, a.channel, out);
    return true;
}
static bool runSetOutput(FuncGenController& c, const ToolArg& a, JsonObject out) {
    if (!c.setOutputEnabled(channelOf(a), a.boolean, CHANGE_SOURCE::MCP)) return false;
    writeState(c, a.channel, out);
    return true;
}
static bool runSetMirror(FuncGenController& c, const ToolArg& a, JsonObject out) {
    c.setMirror(a.boolean, CHANGE_SOURCE::MCP);
    writeState(c, -1, out);
    return true;
}
static bool runGetState(FuncGenController& c, const ToolArg& a, JsonObject out) { writeState(c, a.channel, out); return true; }
static bool runGetLimits(FuncGenController& c, const ToolArg&, JsonObject out) { writeLimits(c, out); return true; }

//adding a tool = one entry here
static const Tool TOOLS[] = {
    {"set_waveform", "Set waveform",
     "Select a channel's waveform. If the current amplitude is outside the new waveform's range it is clamped into it.",
     PARAM::WAVEFORM, CHANNEL_ARG::REQUIRED, true, "waveform", "Output waveform", nullptr, nullptr, false, false, runSetWaveform},
    {"set_frequency", "Set frequency", "Set a channel's frequency in Hz.",
     PARAM::NUMBER, CHANNEL_ARG::REQUIRED, true, "hz", "Frequency in Hz", "Hz", frequencyRange, false, false, runSetFrequency},
    {"set_amplitude", "Set amplitude",
     "Set a channel's peak-to-peak amplitude in volts. The valid range depends on that channel's waveform and DC offset: amplitude / 2 + |offset| must stay at or below output_peak_v (see get_limits). The offset is never changed by this call.",
     PARAM::NUMBER, CHANNEL_ARG::REQUIRED, true, "vpp", "Amplitude in volts peak-to-peak", "Vpp", amplitudeRange, false, false, runSetAmplitude},
    {"set_phase", "Set phase", "Set a channel's phase offset in degrees, from 0 up to (not including) 360.",
     PARAM::NUMBER, CHANNEL_ARG::REQUIRED, true, "degrees", "Phase in degrees", "degrees", phaseRange, true, false, runSetPhase},
    {"set_dc_offset", "Set DC offset",
     "Set a channel's DC offset in volts. The valid range depends on that channel's amplitude: amplitude / 2 + |offset| must stay at or below output_peak_v (see get_limits). The amplitude is never changed by this call.",
     PARAM::NUMBER, CHANNEL_ARG::REQUIRED, true, "volts", "DC offset in volts", "V", dcOffsetRange, false, false, runSetDCOffset},
    {"set_output", "Enable/disable output", "Turn a channel's output on or off. Allowed on channel 2 while mirrored.",
     PARAM::BOOLEAN, CHANNEL_ARG::REQUIRED, false, "enabled", "true = output on", nullptr, nullptr, false, false, runSetOutput},
    {"set_mirror", "Mirror channel 1 on channel 2",
     "Turn the mirror on or off. On: channel 2 copies channel 1's waveform, frequency, amplitude, phase and offset right away and follows every later change to them; setting those on channel 2 is rejected. Output on/off stays per channel. Off: channel 2 keeps its current values. Returns both channels.",
     PARAM::BOOLEAN, CHANNEL_ARG::NONE, false, "enabled", "true = channel 2 follows channel 1", nullptr, nullptr, false, false, runSetMirror},
    {"get_state", "Get state",
     "Read the current settings: one channel's if \"channel\" is given, otherwise every channel. Includes the mirror flag.",
     PARAM::NONE, CHANNEL_ARG::OPTIONAL, false, nullptr, nullptr, nullptr, nullptr, false, true, runGetState},
    {"get_limits", "Get limits",
     "Read the valid range of every setting (amplitude per waveform), output_peak_v and the number of channels. Both channels share these limits. Amplitude and DC offset also share one limit: amplitude / 2 + |offset| <= output_peak_v. Values outside these ranges are rejected.",
     PARAM::NONE, CHANNEL_ARG::NONE, false, nullptr, nullptr, nullptr, nullptr, false, true, runGetLimits},
};

static const char* const CHANNEL_KEY = "channel";

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
    JsonArray required = schema["required"].to<JsonArray>();
    if (t.channelArg != CHANNEL_ARG::NONE) {
        JsonObject c = props[CHANNEL_KEY].to<JsonObject>();
        c["type"] = "integer";
        c["minimum"] = 1;
        c["maximum"] = CHANNEL_COUNT;
        c["description"] = t.channelArg == CHANNEL_ARG::REQUIRED ? "Output channel, 1 or 2"
                                                                 : "Output channel, 1 or 2. Leave out for every channel";
        if (t.channelArg == CHANNEL_ARG::REQUIRED) required.add(CHANNEL_KEY);
    }
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
        required.add(t.paramName);
    }
    if (required.size() == 0) schema.remove("required");
    schema["additionalProperties"] = false;

    JsonObject hints = out["annotations"].to<JsonObject>();
    hints["readOnlyHint"] = t.readOnly;
    hints["destructiveHint"] = false;
    hints["idempotentHint"] = true;
    hints["openWorldHint"] = false;
}

//"'channel' and 'vpp'", "'channel'", ... for error messages
static void argumentList(const Tool& t, char* out, size_t len) {
    bool channel = t.channelArg != CHANNEL_ARG::NONE;
    bool param = t.param != PARAM::NONE;
    if (channel && param) snprintf(out, len, "'%s' and '%s'", CHANNEL_KEY, t.paramName);
    else if (channel) snprintf(out, len, "'%s'", CHANNEL_KEY);
    else if (param) snprintf(out, len, "'%s'", t.paramName);
    else snprintf(out, len, "no arguments");
}

//checks the arguments against the tool and that channel's state. false +
//message (for an isError result) if they don't fit
static bool readArgument(const Tool& t, JsonObjectConst args, FuncGenController& controller,
                         ToolArg& out, char* err, size_t len) {
    for (JsonPairConst kv : args) {
        const char* key = kv.key().c_str();
        bool known = (t.param != PARAM::NONE && strcmp(key, t.paramName) == 0) ||
                     (t.channelArg != CHANNEL_ARG::NONE && strcmp(key, CHANNEL_KEY) == 0);
        if (!known) {
            char allowed[48];
            argumentList(t, allowed, sizeof(allowed));
            if (t.param == PARAM::NONE && t.channelArg == CHANNEL_ARG::NONE) {
                snprintf(err, len, "unknown argument '%.32s', %s takes no arguments", key, t.name);
            } else {
                snprintf(err, len, "unknown argument '%.32s', %s only takes %s", key, t.name, allowed);
            }
            return false;
        }
    }

    out.channel = -1;
    if (t.channelArg != CHANNEL_ARG::NONE) {
        JsonVariantConst c = args[CHANNEL_KEY];
        uint8_t ch;
        if (c.isNull()) {
            if (t.channelArg == CHANNEL_ARG::REQUIRED) {
                snprintf(err, len, "missing required argument '%s' (1 or 2)", CHANNEL_KEY);
                return false;
            }
        } else if (!parseChannel(c, ch)) {
            snprintf(err, len, "'%s' must be the number 1 or 2", CHANNEL_KEY);
            return false;
        } else {
            out.channel = (int8_t)ch;
        }
    }
    if (t.param == PARAM::NONE) return true;

    FuncGenLimits limits = controller.limits();
    FuncGenState state = controller.state(out.channel >= 0 ? (uint8_t)out.channel : CHANNEL_1);
    if (t.mirrored && state.mirrored) {
        snprintf(err, len, "channel 2 mirrors channel 1, so %s is locked on it. Set channel 1 instead "
                 "(channel 2 follows), or call set_mirror with enabled false first", t.paramName);
        return false;
    }

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
                    snprintf(forWave, sizeof(forWave), " for channel %d's %s waveform at %s V offset",
                             out.channel + 1, waveformJsonName(state.waveform), other);
                } else if (t.range == dcOffsetRange) {
                    formatNumber(state.amplitude, other, sizeof(other));
                    snprintf(forWave, sizeof(forWave), " at channel %d's %s Vpp amplitude", out.channel + 1, other);
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
        char err[224];
        if (!readArgument(*tool, args.as<JsonObjectConst>(), _controller, arg, err, sizeof(err))) {
            text["text"] = err;
            result["isError"] = true;
            return finish(resp, 200, out);
        }

        JsonObject structured = result["structuredContent"].to<JsonObject>();
        if (!tool->run(_controller, arg, structured)) {
            result.remove("structuredContent");
            text["text"] = "channel 2 mirrors channel 1 (the mirror was just turned on), nothing was changed";
            result["isError"] = true;
            return finish(resp, 200, out);
        }
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
