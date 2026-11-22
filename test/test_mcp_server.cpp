/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host tests for the MCP server, end to end: JSON-RPC over the
             fake esp_http_server into the real controller and generator
**************************************************************************/

#include "test.h"
#include <string.h>
#include <string>
#include "esp_http_server.h"
#include "../src/comms/mcp_server.h"
#include "../src/comms/api_json.h"
#include "../src/libs/ArduinoJson/ArduinoJson.h"

//generator + controller + HTTP server + MCP server, all running
struct McpRig {
    FunctionGenerator gen;
    FunctionGenerator gen2;
    FuncGenController ctl;
    HttpServer http;
    McpServer mcp;
    McpRig() : gen(PIN_AD9833_CS, PIN_AMP_DAC_CS, PIN_OFFSET_DAC_CS, OFFSET_DAC_CHANNEL, PIN_WAVE_SWITCH, PIN_OUTPUT_RELAY),
               gen2(PIN_AD9833_CS_2, PIN_AMP_DAC_CS_2, PIN_OFFSET_DAC_CS, OFFSET_DAC_CHANNEL_2, PIN_WAVE_SWITCH_2, PIN_OUTPUT_RELAY_2),
               ctl(gen, gen2), mcp(ctl, http) {
        fakeHttpdReset();
        gen.begin();
        gen2.begin();
        mcp.begin();
        http.begin();
    }
};

static FakeHttpOptions mcpHeaders() {
    FakeHttpOptions o;
    o.headers["Content-Type"] = "application/json";
    o.headers["Accept"] = "application/json, text/event-stream";
    return o;
}

static FakeHttpResponse post(const std::string& body, const FakeHttpOptions& o = mcpHeaders()) {
    return fakeHttpRequest(HTTP_POST, MCP_PATH, body, o);
}

struct Rpc {
    int status;
    JsonDocument doc;
};

static Rpc rpc(const char* method, const std::string& params = "", const std::string& id = "1") {
    std::string body = "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"method\":\"" + method + "\"";
    if (!params.empty()) body += ",\"params\":" + params;
    body += "}";
    FakeHttpResponse r = post(body);
    Rpc out;
    out.status = r.status;
    deserializeJson(out.doc, r.body);
    return out;
}

static Rpc call(const char* tool, const std::string& args = "") {
    std::string params = std::string("{\"name\":\"") + tool + "\"";
    if (!args.empty()) params += ",\"arguments\":" + args;
    params += "}";
    return rpc("tools/call", params);
}

static bool isToolError(const Rpc& r) {
    return r.doc["result"]["isError"].as<bool>();
}

static std::string toolText(const Rpc& r) {
    return r.doc["result"]["content"][0]["text"] | "";
}

static bool contains(const std::string& s, const char* part) {
    return s.find(part) != std::string::npos;
}

//counts MCP-tagged changes. a LOCAL one would make the LCD treat an agent's change as its own
static void countingListener(uint8_t, const FuncGenState&, CHANGE_SOURCE source, void* ctx) {
    if (source == CHANGE_SOURCE::MCP) (*(int*)ctx)++;
}

//lifecycle

TEST(mcp_initialize) {
    McpRig rig;
    Rpc r = rpc("initialize", "{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},\"clientInfo\":{\"name\":\"t\",\"version\":\"1\"}}");
    CHECK_EQ(r.status, 200);
    CHECK(strcmp(r.doc["jsonrpc"] | "", "2.0") == 0);
    CHECK_EQ(r.doc["id"].as<int>(), 1);
    JsonObject res = r.doc["result"];
    CHECK(strcmp(res["protocolVersion"] | "", "2025-06-18") == 0); //echoed
    CHECK(res["capabilities"]["tools"].is<JsonObject>());
    CHECK(strcmp(res["serverInfo"]["name"] | "", MCP_SERVER_NAME) == 0);
    CHECK(strcmp(res["serverInfo"]["version"] | "", FIRMWARE_VERSION) == 0);
    CHECK(res["instructions"].is<const char*>());

    r = rpc("initialize", "{\"protocolVersion\":\"1999-01-01\"}");
    CHECK(strcmp(r.doc["result"]["protocolVersion"] | "", "2025-11-25") == 0); //our newest
    r = rpc("initialize");
    CHECK(strcmp(r.doc["result"]["protocolVersion"] | "", "2025-11-25") == 0);
}

TEST(mcp_notifications_and_client_responses_get_202) {
    McpRig rig;
    FakeHttpResponse r = post("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}");
    CHECK_EQ(r.status, 202);
    CHECK(r.body.empty());
    r = post("{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"set_output\",\"arguments\":{\"enabled\":true}}}");
    CHECK_EQ(r.status, 202); //a notification never runs anything
    CHECK(!rig.ctl.state(CHANNEL_1).outputEnabled);
    CHECK_EQ(post("{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":{}}").status, 202);
    CHECK_EQ(post("{\"jsonrpc\":\"2.0\",\"id\":7,\"error\":{\"code\":1,\"message\":\"x\"}}").status, 202);
}

TEST(mcp_ping_and_ids) {
    McpRig rig;
    Rpc r = rpc("ping", "", "\"abc\"");
    CHECK_EQ(r.status, 200);
    CHECK(strcmp(r.doc["id"] | "", "abc") == 0); //string ids echoed
    CHECK(r.doc["result"].is<JsonObject>());
    CHECK_EQ(r.doc["result"].as<JsonObject>().size(), 0u);
    r = rpc("ping", "", "12345678901");
    CHECK(r.doc["id"].as<long long>() == 12345678901LL);
}

TEST(mcp_get_is_405) {
    McpRig rig;
    FakeHttpResponse r = fakeHttpRequest(HTTP_GET, MCP_PATH);
    CHECK_EQ(r.status, 405);
    CHECK(r.headers["Allow"] == "POST");
}

//JSON-RPC errors

static int errorCode(const FakeHttpResponse& r) {
    JsonDocument d;
    deserializeJson(d, r.body);
    return d["error"]["code"] | 0;
}

TEST(mcp_protocol_errors) {
    McpRig rig;
    FakeHttpResponse r = post("{\"jsonrpc\":\"2.0\",\"id\":1,");
    CHECK_EQ(r.status, 400);
    CHECK_EQ(errorCode(r), -32700);
    JsonDocument d;
    deserializeJson(d, r.body);
    CHECK(d["id"].isNull());
    CHECK(strcmp(d["jsonrpc"] | "", "2.0") == 0);

    r = post("[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}]");
    CHECK_EQ(r.status, 400);
    CHECK_EQ(errorCode(r), -32600);
    CHECK(contains(r.body, "batch")); //says why
    CHECK_EQ(errorCode(post("42")), -32600);
    CHECK_EQ(post("{\"jsonrpc\":\"2.0\",\"id\":{},\"method\":\"ping\"}").status, 400);
    CHECK_EQ(post("{\"jsonrpc\":\"2.0\"}").status, 400);

    r = post("{\"jsonrpc\":\"1.0\",\"id\":3,\"method\":\"ping\"}");
    CHECK_EQ(r.status, 200);
    CHECK_EQ(errorCode(r), -32600);
    deserializeJson(d, r.body);
    CHECK_EQ(d["id"].as<int>(), 3);

    CHECK_EQ(errorCode(post("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":5}")), -32600);
    CHECK_EQ(rpc("resources/list").doc["error"]["code"].as<int>(), -32601);
    CHECK_EQ(rpc("ping", "[1]").doc["error"]["code"].as<int>(), -32602);
}

TEST(mcp_tools_call_protocol_errors) {
    McpRig rig;
    CHECK_EQ(rpc("tools/call", "{\"name\":\"self_destruct\"}").doc["error"]["code"].as<int>(), -32602);
    CHECK_EQ(rpc("tools/call", "{}").doc["error"]["code"].as<int>(), -32602);
    CHECK_EQ(rpc("tools/call", "{\"name\":\"set_frequency\",\"arguments\":[1000]}").doc["error"]["code"].as<int>(), -32602);
    CHECK_EQ(rpc("tools/call").doc["error"]["code"].as<int>(), -32602);
}

//tools/list

TEST(mcp_tools_list_schemas_follow_limits) {
    McpRig rig;
    Rpc r = rpc("tools/list");
    CHECK_EQ(r.status, 200);
    JsonArray tools = r.doc["result"]["tools"];
    CHECK_EQ(tools.size(), 9u);

    FuncGenLimits limits = rig.ctl.limits();
    bool sawFreq = false, sawPhase = false, sawWave = false, sawState = false;
    for (JsonObject t : tools) {
        const char* name = t["name"];
        CHECK(t["description"].is<const char*>());
        CHECK(strcmp(t["inputSchema"]["type"] | "", "object") == 0);
        CHECK(t["inputSchema"]["additionalProperties"].is<bool>() && !t["inputSchema"]["additionalProperties"].as<bool>());
        if (strcmp(name, "set_frequency") == 0) {
            sawFreq = true;
            JsonObject hz = t["inputSchema"]["properties"]["hz"];
            CHECK(strcmp(hz["type"] | "", "number") == 0);
            CHECK_NEAR(hz["minimum"].as<double>(), limits.frequency.min, 1e-6);
            CHECK_NEAR(hz["maximum"].as<double>(), limits.frequency.max, 1e-3);
            CHECK(strcmp(t["inputSchema"]["required"][0] | "", "channel") == 0);
            CHECK(strcmp(t["inputSchema"]["required"][1] | "", "hz") == 0);
            JsonObject ch = t["inputSchema"]["properties"]["channel"];
            CHECK(strcmp(ch["type"] | "", "integer") == 0);
            CHECK_EQ(ch["minimum"].as<int>(), 1);
            CHECK_EQ(ch["maximum"].as<int>(), CHANNEL_COUNT);
            CHECK(!t["annotations"]["readOnlyHint"].as<bool>());
        }
        if (strcmp(name, "set_phase") == 0) {
            sawPhase = true;
            JsonObject deg = t["inputSchema"]["properties"]["degrees"];
            CHECK_NEAR(deg["exclusiveMaximum"].as<double>(), limits.phase.max, 1e-6);
            CHECK(deg["maximum"].isNull());
        }
        if (strcmp(name, "set_waveform") == 0) {
            sawWave = true;
            JsonArray e = t["inputSchema"]["properties"]["waveform"]["enum"];
            CHECK_EQ(e.size(), 3u);
            CHECK(strcmp(e[1] | "", "square") == 0);
        }
        if (strcmp(name, "get_state") == 0) {
            sawState = true;
            //channel is optional here: every channel without it
            CHECK_EQ(t["inputSchema"]["properties"].as<JsonObject>().size(), 1u);
            CHECK(t["inputSchema"]["properties"]["channel"].is<JsonObject>());
            CHECK(t["inputSchema"]["required"].isNull());
            CHECK(t["annotations"]["readOnlyHint"].as<bool>());
        }
    }
    CHECK(sawFreq && sawPhase && sawWave && sawState);
}

//tools/call

TEST(mcp_setters_apply_and_report_state) {
    McpRig rig;
    int notified = 0;
    rig.ctl.addListener(countingListener, &notified);

    Rpc r = call("set_frequency", "{\"channel\":1,\"hz\":1000}"); //integer JSON number
    CHECK_EQ(r.status, 200);
    CHECK(!isToolError(r));
    CHECK_NEAR(rig.ctl.state(CHANNEL_1).frequency, 1000, 1e-3);
    CHECK_NEAR(r.doc["result"]["structuredContent"]["frequency_hz"].as<double>(), 1000, 1e-3);
    CHECK_EQ(notified, 1); //the LCD hears about it

    //the text block carries the same JSON
    JsonDocument text;
    CHECK(!deserializeJson(text, toolText(r)));
    CHECK_NEAR(text["frequency_hz"].as<double>(), 1000, 1e-3);
    CHECK(strcmp(text["waveform"] | "", "sine") == 0);

    call("set_waveform", "{\"channel\":1,\"waveform\":\"SQUARE\"}");
    CHECK(rig.ctl.state(CHANNEL_1).waveform == WAVEFORM::SQUARE);
    call("set_amplitude", "{\"channel\":1,\"vpp\":2.5}");
    CHECK_NEAR(rig.ctl.state(CHANNEL_1).amplitude, 2.5, 1e-6);
    call("set_phase", "{\"channel\":1,\"degrees\":90.5}");
    CHECK_NEAR(rig.ctl.state(CHANNEL_1).phase, 90.5, 0.01);
    call("set_dc_offset", "{\"channel\":1,\"volts\":-1.25}");
    CHECK_NEAR(rig.ctl.state(CHANNEL_1).dcOffset, -1.25, 1e-6);
    r = call("set_output", "{\"channel\":1,\"enabled\":true}");
    CHECK(rig.ctl.state(CHANNEL_1).outputEnabled);
    CHECK(r.doc["result"]["structuredContent"]["output_enabled"].as<bool>());
    CHECK_EQ(notified, 6);

    r = call("get_state", "{\"channel\":1}");
    CHECK(!isToolError(r));
    JsonObject s = r.doc["result"]["structuredContent"];
    CHECK_EQ(s["channel"].as<int>(), 1);
    CHECK(s["mirror"].is<bool>());
    CHECK(strcmp(s["waveform"] | "", "square") == 0);
    CHECK_NEAR(s["amplitude_vpp"].as<double>(), 2.5, 1e-6);
    CHECK_NEAR(s["dc_offset_v"].as<double>(), -1.25, 1e-6);
    CHECK_EQ(notified, 6); //reading changes nothing
}

TEST(mcp_out_of_range_is_rejected_not_clamped) {
    McpRig rig;
    int notified = 0;
    rig.ctl.addListener(countingListener, &notified);

    Rpc r = call("set_frequency", "{\"channel\":1,\"hz\":20000000}");
    CHECK_EQ(r.status, 200); //a tool error, not a protocol error
    CHECK(isToolError(r));
    CHECK(contains(toolText(r), "between 0.1 and 12500000 Hz"));
    CHECK(contains(toolText(r), "got 20000000"));
    CHECK(r.doc["result"]["structuredContent"].isNull());
    CHECK_NEAR(rig.ctl.state(CHANNEL_1).frequency, 1.0, 1e-6); //untouched
    CHECK_EQ(notified, 0);

    CHECK(isToolError(r = call("set_frequency", "{\"channel\":1,\"hz\":0.09}")));
    CHECK(!isToolError(r = call("set_frequency", "{\"channel\":1,\"hz\":0.1}"))); //0.1 passes the 0.1f minimum
    CHECK(!isToolError(r = call("set_frequency", "{\"channel\":1,\"hz\":12500000}")));
    CHECK(isToolError(r = call("set_frequency", "{\"channel\":1,\"hz\":1e999}")));

    r = call("set_amplitude", "{\"channel\":1,\"vpp\":100}");
    CHECK(isToolError(r));
    CHECK(contains(toolText(r), "for channel 1's sine waveform at 0 V offset"));

    CHECK(isToolError(r = call("set_phase", "{\"channel\":1,\"degrees\":360}"))); //exclusive
    CHECK(contains(toolText(r), "less than 360"));
    CHECK(isToolError(call("set_phase", "{\"channel\":1,\"degrees\":-1}")));
    CHECK(!isToolError(call("set_phase", "{\"channel\":1,\"degrees\":0}")));
    CHECK(!isToolError(call("set_phase", "{\"channel\":1,\"degrees\":359.9}")));

    CHECK(isToolError(call("set_dc_offset", "{\"channel\":1,\"volts\":100}")));
    CHECK(isToolError(call("set_dc_offset", "{\"channel\":1,\"volts\":-100}")));
}

TEST(mcp_bad_arguments_are_tool_errors) {
    McpRig rig;
    Rpc r;
    CHECK(isToolError(r = call("set_frequency")));
    CHECK(contains(toolText(r), "missing required argument 'channel'"));
    CHECK(isToolError(r = call("set_frequency", "{\"hz\":1000}")));    //no silent default channel
    CHECK(contains(toolText(r), "missing required argument 'channel'"));
    CHECK(isToolError(r = call("set_frequency", "{\"channel\":1}")));
    CHECK(contains(toolText(r), "missing required argument 'hz'"));
    CHECK(isToolError(r = call("set_frequency", "{\"channel\":3,\"hz\":1000}")));
    CHECK(contains(toolText(r), "'channel' must be the number 1 or 2"));
    CHECK(isToolError(call("set_frequency", "{\"channel\":0,\"hz\":1000}")));
    CHECK(isToolError(call("set_frequency", "{\"channel\":\"1\",\"hz\":1000}")));
    CHECK(isToolError(call("set_frequency", "{\"channel\":1.5,\"hz\":1000}")));
    CHECK(isToolError(r = call("set_frequency", "{\"channel\":1,\"hz\":null}")));
    CHECK(isToolError(r = call("set_frequency", "{\"channel\":1,\"hz\":\"1000\"}")));
    CHECK(contains(toolText(r), "must be a number"));
    CHECK(isToolError(r = call("set_frequency", "{\"channel\":1,\"hz\":1000,\"phase\":3}")));
    CHECK(contains(toolText(r), "unknown argument 'phase'"));
    CHECK(isToolError(r = call("set_frequency", "{\"channel\":1,\"frequency\":1000}")));
    CHECK(isToolError(r = call("set_output", "{\"channel\":1,\"enabled\":1}")));
    CHECK(contains(toolText(r), "true or false"));
    CHECK(isToolError(r = call("set_waveform", "{\"channel\":1,\"waveform\":\"sawtooth\"}")));
    CHECK(contains(toolText(r), "\"triangle\""));
    CHECK(isToolError(call("set_waveform", "{\"channel\":1,\"waveform\":2}")));
    CHECK(isToolError(r = call("get_state", "{\"verbose\":true}")));
    CHECK(contains(toolText(r), "only takes 'channel'"));
    CHECK(isToolError(r = call("get_limits", "{\"channel\":1}")));
    CHECK(contains(toolText(r), "takes no arguments"));
    CHECK(isToolError(r = call("set_mirror", "{\"channel\":2,\"enabled\":true}")));
    CHECK(contains(toolText(r), "only takes 'enabled'"));
    CHECK(!isToolError(call("get_state", "{}")));
    CHECK(rig.ctl.state(CHANNEL_1).waveform == WAVEFORM::SINE);
    CHECK(!rig.ctl.mirror());
    CHECK(!rig.ctl.state(CHANNEL_1).outputEnabled);
}

TEST(mcp_get_limits_matches_controller) {
    McpRig rig;
    Rpc r = call("get_limits");
    CHECK(!isToolError(r));
    FuncGenLimits l = rig.ctl.limits();
    JsonObject s = r.doc["result"]["structuredContent"];
    CHECK_NEAR(s["frequency_hz"]["min"].as<double>(), l.frequency.min, 1e-6);
    CHECK_NEAR(s["frequency_hz"]["max"].as<double>(), l.frequency.max, 1e-3);
    CHECK_NEAR(s["phase_deg"]["max"].as<double>(), l.phase.max, 1e-6);
    CHECK(s["phase_deg"]["max_exclusive"].as<bool>());
    const char* names[] = {"sine", "square", "triangle"};
    for (int i = 0; i < WAVEFORM_COUNT; i++) {
        CHECK_NEAR(s["amplitude_vpp"][names[i]]["min"].as<double>(), l.amplitude[i].min, 1e-6);
        CHECK_NEAR(s["amplitude_vpp"][names[i]]["max"].as<double>(), l.amplitude[i].max, 1e-6);
    }
    CHECK_NEAR(s["dc_offset_v"]["min"].as<double>(), l.dcOffset.min, 1e-6);
    CHECK_NEAR(s["output_peak_v"].as<double>(), l.outputPeak, 1e-6);
    CHECK_EQ(s["channels"].as<int>(), CHANNEL_COUNT);
    //floats are printed short, not as 0.1000000015
    CHECK(contains(toolText(r), "0.1"));
    CHECK(!contains(toolText(r), "0.1000000"));
}

TEST(mcp_amplitude_and_offset_share_the_peak_limit) {
    McpRig rig;
    float peak = rig.ctl.limits().outputPeak;
    CHECK(!isToolError(call("set_amplitude", "{\"channel\":1,\"vpp\":12}")));
    CHECK_NEAR(rig.ctl.state(CHANNEL_1).amplitude, 12.0, 1e-6);

    //12 Vpp leaves peak - 6 V of offset; asking for more is an error, nothing changes
    char args[64];
    snprintf(args, sizeof(args), "{\"channel\":1,\"volts\":%.3f}", peak - 6.0f + 0.5f);
    Rpc r = call("set_dc_offset", args);
    CHECK(isToolError(r));
    CHECK(contains(toolText(r), "at channel 1's 12 Vpp amplitude"));
    CHECK_NEAR(rig.ctl.state(CHANNEL_1).dcOffset, 0.0, 1e-6);
    CHECK_NEAR(rig.ctl.state(CHANNEL_1).amplitude, 12.0, 1e-6);

    snprintf(args, sizeof(args), "{\"channel\":1,\"volts\":%.3f}", peak - 6.0f);
    CHECK(!isToolError(call("set_dc_offset", args)));
    CHECK_NEAR(rig.ctl.state(CHANNEL_1).dcOffset, peak - 6.0f, 1e-5);

    //and back: with a big offset the amplitude can't grow, even below its own max.
    //the most negative offset is the biggest one (the range isn't symmetric)
    float off = rig.ctl.limits().dcOffset.min;
    float left = 2.0f * (peak - fabsf(off));
    CHECK(left < 12.0f); //the peak rule has to bite here, or this tests nothing
    CHECK(!isToolError(call("set_amplitude", "{\"channel\":1,\"vpp\":1}")));
    snprintf(args, sizeof(args), "{\"channel\":1,\"volts\":%.3f}", off);
    CHECK(!isToolError(call("set_dc_offset", args)));
    snprintf(args, sizeof(args), "{\"channel\":1,\"vpp\":%.3f}", left + 0.5f);
    CHECK(isToolError(r = call("set_amplitude", args))); //only `left` Vpp left
    CHECK(contains(toolText(r), "V offset"));
    CHECK_NEAR(rig.ctl.state(CHANNEL_1).amplitude, 1.0, 1e-6);
    snprintf(args, sizeof(args), "{\"channel\":1,\"vpp\":%.3f}", left);
    CHECK(!isToolError(call("set_amplitude", args)));
    CHECK_NEAR(rig.ctl.state(CHANNEL_1).amplitude, left, 1e-5);
}

TEST(mcp_channels_are_separate) {
    McpRig rig;
    Rpc r = call("set_frequency", "{\"channel\":2,\"hz\":2500}");
    CHECK(!isToolError(r));
    CHECK_EQ(r.doc["result"]["structuredContent"]["channel"].as<int>(), 2);
    CHECK_NEAR(r.doc["result"]["structuredContent"]["frequency_hz"].as<double>(), 2500, 1e-3);
    CHECK_NEAR(rig.ctl.state(CHANNEL_2).frequency, 2500, 1e-3);
    CHECK_NEAR(rig.ctl.state(CHANNEL_1).frequency, 1.0, 1e-6);

    CHECK(!isToolError(call("set_output", "{\"channel\":2,\"enabled\":true}")));
    CHECK(rig.ctl.state(CHANNEL_2).outputEnabled);
    CHECK(!rig.ctl.state(CHANNEL_1).outputEnabled);

    //without a channel get_state lists both
    r = call("get_state");
    CHECK(!isToolError(r));
    JsonObject s = r.doc["result"]["structuredContent"];
    JsonArray all = s["channels"];
    CHECK_EQ(all.size(), (size_t)CHANNEL_COUNT);
    CHECK_EQ(all[0]["channel"].as<int>(), 1);
    CHECK_EQ(all[1]["channel"].as<int>(), 2);
    CHECK_NEAR(all[1]["frequency_hz"].as<double>(), 2500, 1e-3);
    CHECK(all[1]["output_enabled"].as<bool>());
    CHECK(!s["mirror"].as<bool>());

    r = call("get_state", "{\"channel\":2}");
    CHECK_EQ(r.doc["result"]["structuredContent"]["channel"].as<int>(), 2);
    CHECK(r.doc["result"]["structuredContent"]["channels"].isNull());
}

TEST(mcp_mirror_follows_and_locks_channel_2) {
    McpRig rig;
    call("set_frequency", "{\"channel\":1,\"hz\":1234}");
    Rpc r = call("set_mirror", "{\"enabled\":true}");
    CHECK(!isToolError(r));
    JsonObject s = r.doc["result"]["structuredContent"];
    CHECK(s["mirror"].as<bool>());
    CHECK_NEAR(s["channels"][1]["frequency_hz"].as<double>(), 1234, 1e-3);
    CHECK(rig.ctl.mirror());

    //channel 1 changes reach channel 2
    CHECK(!isToolError(call("set_amplitude", "{\"channel\":1,\"vpp\":3}")));
    CHECK_NEAR(rig.ctl.state(CHANNEL_2).amplitude, 3.0, 1e-6);

    //mirrored settings on channel 2 are rejected with a way out
    r = call("set_frequency", "{\"channel\":2,\"hz\":50}");
    CHECK(isToolError(r));
    CHECK(contains(toolText(r), "channel 2 mirrors channel 1"));
    CHECK(contains(toolText(r), "set_mirror"));
    CHECK_NEAR(rig.ctl.state(CHANNEL_2).frequency, 1234, 1e-3);
    CHECK(isToolError(call("set_waveform", "{\"channel\":2,\"waveform\":\"square\"}")));
    CHECK(isToolError(call("set_phase", "{\"channel\":2,\"degrees\":10}")));
    CHECK(isToolError(call("set_dc_offset", "{\"channel\":2,\"volts\":1}")));

    //its output stays its own
    CHECK(!isToolError(call("set_output", "{\"channel\":2,\"enabled\":true}")));
    CHECK(rig.ctl.state(CHANNEL_2).outputEnabled);
    CHECK(!rig.ctl.state(CHANNEL_1).outputEnabled);

    CHECK(isToolError(call("set_mirror", "{\"enabled\":\"no\"}")));
    CHECK(!isToolError(call("set_mirror", "{\"enabled\":false}")));
    CHECK(!rig.ctl.mirror());
    CHECK(!isToolError(call("set_frequency", "{\"channel\":2,\"hz\":50}")));
    CHECK_NEAR(rig.ctl.state(CHANNEL_2).frequency, 50, 1e-3);
}

//HTTP-level checks

TEST(mcp_request_checks) {
    McpRig rig;
    const std::string ping = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}";

    CHECK_EQ(post(ping, FakeHttpOptions()).status, 415);
    FakeHttpOptions o = mcpHeaders();
    o.headers["Content-Type"] = "text/plain";
    CHECK_EQ(post(ping, o).status, 415);
    o.headers["Content-Type"] = "application/json; charset=utf-8";
    CHECK_EQ(post(ping, o).status, 200);

    o = mcpHeaders();
    o.headers["Origin"] = "http://evil.example";
    FakeHttpResponse r = post(ping, o);
    CHECK_EQ(r.status, 403);
    CHECK_EQ(errorCode(r), -32600);
    o.headers["Origin"] = "null";
    CHECK_EQ(post(ping, o).status, 403);
    o.headers["Origin"] = "http://" + std::string(200, 'a') + ".example"; //too long to read is not "absent"
    CHECK_EQ(post(ping, o).status, 403);
    o.headers["Origin"] = "http://" MDNS_HOSTNAME ".local";
    CHECK_EQ(post(ping, o).status, 200);
    o.headers["Origin"] = "http://192.168.4.1:8080";
    CHECK_EQ(post(ping, o).status, 200);

    o = mcpHeaders();
    o.headers["MCP-Protocol-Version"] = "2099-01-01";
    CHECK_EQ(post(ping, o).status, 400);
    o.headers["MCP-Protocol-Version"] = "2025-06-18";
    CHECK_EQ(post(ping, o).status, 200);

    CHECK_EQ(post(std::string(HTTP_MAX_BODY + 1, ' ')).status, 413);
}

TEST(mcp_origin_rules) {
    CHECK(originAllowed("http://" MDNS_HOSTNAME ".local"));
    CHECK(originAllowed("HTTP://" MDNS_HOSTNAME ".LOCAL:80"));
    CHECK(originAllowed("https://10.0.0.5"));
    CHECK(originAllowed("http://192.168.1.50:3000"));
    CHECK(!originAllowed("http://" MDNS_HOSTNAME ".local.evil.com"));
    CHECK(!originAllowed("http://evil" MDNS_HOSTNAME ".local"));
    CHECK(!originAllowed("http://func"));            //a prefix of the name
    CHECK(!originAllowed("http://" MDNS_HOSTNAME));  //without .local
    CHECK(!originAllowed("http://"));
    CHECK(!originAllowed("http://:80"));
    CHECK(!originAllowed("http://localhost:6274"));
    CHECK(!originAllowed("http://256.1.1.1"));
    CHECK(!originAllowed("http://1.2.3"));
    CHECK(!originAllowed("http://1.2.3.4.5"));
    CHECK(!originAllowed("http://1.2.3.4."));
    CHECK(!originAllowed("http://1.2.3.4/"));
    CHECK(!originAllowed("http://1.2.3.4:"));
    CHECK(!originAllowed("http://1.2.3.4:80x"));
    CHECK(!originAllowed("http://1234.2.3.4"));
    CHECK(!originAllowed("ftp://1.2.3.4"));
    CHECK(!originAllowed("null"));
    CHECK(!originAllowed(""));
    CHECK(!originAllowed(nullptr));
}

TEST(mcp_versions) {
    CHECK(mcpVersionSupported("2025-11-25"));
    CHECK(mcpVersionSupported("2025-06-18"));
    CHECK(mcpVersionSupported("2025-03-26"));
    CHECK(mcpVersionSupported("2024-11-05"));
    CHECK(!mcpVersionSupported("2025-06-19"));
    CHECK(!mcpVersionSupported(nullptr));
}
