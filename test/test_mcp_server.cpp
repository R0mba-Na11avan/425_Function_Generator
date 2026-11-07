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
#include "../src/libs/ArduinoJson/ArduinoJson.h"

//generator + controller + HTTP server + MCP server, all running
struct McpRig {
    FunctionGenerator gen;
    FuncGenController ctl;
    HttpServer http;
    McpServer mcp;
    McpRig() : gen(PIN_AD9833_CS, PIN_MCP4131_CS), ctl(gen), mcp(ctl, http) {
        fakeHttpdReset();
        gen.begin();
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

static void countingListener(const FuncGenState&, void* ctx) {
    (*(int*)ctx)++;
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
    CHECK(!rig.ctl.state().outputEnabled);
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
    CHECK_EQ(tools.size(), 8u);

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
            CHECK(strcmp(t["inputSchema"]["required"][0] | "", "hz") == 0);
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
            CHECK_EQ(t["inputSchema"]["properties"].as<JsonObject>().size(), 0u);
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

    Rpc r = call("set_frequency", "{\"hz\":1000}"); //integer JSON number
    CHECK_EQ(r.status, 200);
    CHECK(!isToolError(r));
    CHECK_NEAR(rig.ctl.state().frequency, 1000, 1e-3);
    CHECK_NEAR(r.doc["result"]["structuredContent"]["frequency_hz"].as<double>(), 1000, 1e-3);
    CHECK_EQ(notified, 1); //the LCD hears about it

    //the text block carries the same JSON
    JsonDocument text;
    CHECK(!deserializeJson(text, toolText(r)));
    CHECK_NEAR(text["frequency_hz"].as<double>(), 1000, 1e-3);
    CHECK(strcmp(text["waveform"] | "", "sine") == 0);

    call("set_waveform", "{\"waveform\":\"SQUARE\"}");
    CHECK(rig.ctl.state().waveform == WAVEFORM::SQUARE);
    call("set_amplitude", "{\"vpp\":2.5}");
    CHECK_NEAR(rig.ctl.state().amplitude, 2.5, 1e-6);
    call("set_phase", "{\"degrees\":90.5}");
    CHECK_NEAR(rig.ctl.state().phase, 90.5, 0.01);
    call("set_dc_offset", "{\"volts\":-1.25}");
    CHECK_NEAR(rig.ctl.state().dcOffset, -1.25, 1e-6);
    r = call("set_output", "{\"enabled\":true}");
    CHECK(rig.ctl.state().outputEnabled);
    CHECK(r.doc["result"]["structuredContent"]["output_enabled"].as<bool>());
    CHECK_EQ(notified, 6);

    r = call("get_state");
    CHECK(!isToolError(r));
    JsonObject s = r.doc["result"]["structuredContent"];
    CHECK(strcmp(s["waveform"] | "", "square") == 0);
    CHECK_NEAR(s["amplitude_vpp"].as<double>(), 2.5, 1e-6);
    CHECK_NEAR(s["dc_offset_v"].as<double>(), -1.25, 1e-6);
    CHECK_EQ(notified, 6); //reading changes nothing
}

TEST(mcp_out_of_range_is_rejected_not_clamped) {
    McpRig rig;
    int notified = 0;
    rig.ctl.addListener(countingListener, &notified);

    Rpc r = call("set_frequency", "{\"hz\":20000000}");
    CHECK_EQ(r.status, 200); //a tool error, not a protocol error
    CHECK(isToolError(r));
    CHECK(contains(toolText(r), "between 0.1 and 12500000 Hz"));
    CHECK(contains(toolText(r), "got 20000000"));
    CHECK(r.doc["result"]["structuredContent"].isNull());
    CHECK_NEAR(rig.ctl.state().frequency, 1.0, 1e-6); //untouched
    CHECK_EQ(notified, 0);

    CHECK(isToolError(r = call("set_frequency", "{\"hz\":0.09}")));
    CHECK(!isToolError(r = call("set_frequency", "{\"hz\":0.1}"))); //0.1 passes the 0.1f minimum
    CHECK(!isToolError(r = call("set_frequency", "{\"hz\":12500000}")));
    CHECK(isToolError(r = call("set_frequency", "{\"hz\":1e999}")));

    r = call("set_amplitude", "{\"vpp\":9}");
    CHECK(isToolError(r));
    CHECK(contains(toolText(r), "for the sine waveform"));

    CHECK(isToolError(r = call("set_phase", "{\"degrees\":360}"))); //exclusive
    CHECK(contains(toolText(r), "less than 360"));
    CHECK(isToolError(call("set_phase", "{\"degrees\":-1}")));
    CHECK(!isToolError(call("set_phase", "{\"degrees\":0}")));
    CHECK(!isToolError(call("set_phase", "{\"degrees\":359.9}")));

    CHECK(isToolError(call("set_dc_offset", "{\"volts\":3}")));
}

TEST(mcp_bad_arguments_are_tool_errors) {
    McpRig rig;
    Rpc r;
    CHECK(isToolError(r = call("set_frequency")));
    CHECK(contains(toolText(r), "missing required argument 'hz'"));
    CHECK(isToolError(r = call("set_frequency", "{\"hz\":null}")));
    CHECK(isToolError(r = call("set_frequency", "{\"hz\":\"1000\"}")));
    CHECK(contains(toolText(r), "must be a number"));
    CHECK(isToolError(r = call("set_frequency", "{\"hz\":1000,\"phase\":3}")));
    CHECK(contains(toolText(r), "unknown argument 'phase'"));
    CHECK(isToolError(r = call("set_frequency", "{\"frequency\":1000}")));
    CHECK(isToolError(r = call("set_output", "{\"enabled\":1}")));
    CHECK(contains(toolText(r), "true or false"));
    CHECK(isToolError(r = call("set_waveform", "{\"waveform\":\"sawtooth\"}")));
    CHECK(contains(toolText(r), "\"triangle\""));
    CHECK(isToolError(call("set_waveform", "{\"waveform\":2}")));
    CHECK(isToolError(r = call("get_state", "{\"verbose\":true}")));
    CHECK(contains(toolText(r), "takes no arguments"));
    CHECK(!isToolError(call("get_state", "{}")));
    CHECK(rig.ctl.state().waveform == WAVEFORM::SINE);
    CHECK(!rig.ctl.state().outputEnabled);
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
    //floats are printed short, not as 0.6119999886
    CHECK(contains(toolText(r), "0.612"));
    CHECK(!contains(toolText(r), "0.61199"));
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
    CHECK(mcpOriginAllowed("http://" MDNS_HOSTNAME ".local"));
    CHECK(mcpOriginAllowed("HTTP://" MDNS_HOSTNAME ".LOCAL:80"));
    CHECK(mcpOriginAllowed("https://10.0.0.5"));
    CHECK(mcpOriginAllowed("http://192.168.1.50:3000"));
    CHECK(!mcpOriginAllowed("http://" MDNS_HOSTNAME ".local.evil.com"));
    CHECK(!mcpOriginAllowed("http://evil" MDNS_HOSTNAME ".local"));
    CHECK(!mcpOriginAllowed("http://func"));            //a prefix of the name
    CHECK(!mcpOriginAllowed("http://" MDNS_HOSTNAME));  //without .local
    CHECK(!mcpOriginAllowed("http://"));
    CHECK(!mcpOriginAllowed("http://:80"));
    CHECK(!mcpOriginAllowed("http://localhost:6274"));
    CHECK(!mcpOriginAllowed("http://256.1.1.1"));
    CHECK(!mcpOriginAllowed("http://1.2.3"));
    CHECK(!mcpOriginAllowed("http://1.2.3.4.5"));
    CHECK(!mcpOriginAllowed("http://1.2.3.4."));
    CHECK(!mcpOriginAllowed("http://1.2.3.4/"));
    CHECK(!mcpOriginAllowed("http://1.2.3.4:"));
    CHECK(!mcpOriginAllowed("http://1.2.3.4:80x"));
    CHECK(!mcpOriginAllowed("http://1234.2.3.4"));
    CHECK(!mcpOriginAllowed("ftp://1.2.3.4"));
    CHECK(!mcpOriginAllowed("null"));
    CHECK(!mcpOriginAllowed(""));
    CHECK(!mcpOriginAllowed(nullptr));
}

TEST(mcp_versions) {
    CHECK(mcpVersionSupported("2025-11-25"));
    CHECK(mcpVersionSupported("2025-06-18"));
    CHECK(mcpVersionSupported("2025-03-26"));
    CHECK(mcpVersionSupported("2024-11-05"));
    CHECK(!mcpVersionSupported("2025-06-19"));
    CHECK(!mcpVersionSupported(nullptr));
}
