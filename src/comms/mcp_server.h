/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: MCP server so AI agents can control the function generator.
             implements MCP over Streamable HTTP at POST /mcp
**************************************************************************/

/*
    TO IMPLEMENT: class McpServer(FuncGenController&, HttpServer&)

    Scope (decided)
        - basic tools only, no auth, stateless plain-JSON responses. no SSE
          stream, no Mcp-Session-Id, no server-initiated notifications
        - revisit auth if the device is ever exposed beyond a trusted LAN

    Transport
        - single route: POST /mcp, body = one JSON-RPC 2.0 message,
          response = application/json
        - GET /mcp -> 405 Method Not Allowed (we don't offer an SSE stream)
        - notifications (no "id", e.g. notifications/initialized) -> 202 Accepted
          with an empty body
        - parse with ArduinoJson (vendored v7.4.3 in src/libs/ArduinoJson). cap the body size

    JSON-RPC methods
        - initialize: reply with protocolVersion (echo the client's if we
          support it, otherwise our newest), capabilities { tools: {} } and
          serverInfo { name, version }
        - ping: reply with an empty result
        - tools/list: return the tool table below
        - tools/call: look up the tool by name, validate the arguments against its
          schema, run it, return { content: [{ type: "text", text: ... }] }
        - anything else: error -32601 (method not found). malformed JSON: -32700.
          bad params: -32602

    Tool table (single table: name, description, JSON schema, handler)
        Adding a tool should be a single new entry. Each handler calls exactly one
        FuncGenController method.
        - set_waveform { waveform: "sine" | "square" | "triangle" }
        - set_frequency { hz: number }
        - set_amplitude { vpp: number }
        - set_phase { degrees: number }
        - set_dc_offset { volts: number }
        - set_output { enabled: boolean }
        - get_state  -> current FuncGenState as JSON
        - get_limits -> min/max (and unit) for frequency, amplitude, phase and
                        offset, per waveform where they differ, so an agent can
                        check before setting

    Errors the agent can act on
        - out-of-range or wrong-type arguments are TOOL errors (result with
          isError: true and a clear message such as "frequency must be between 0
          and 12500000 Hz"), not JSON-RPC protocol errors, so the model sees the
          message and can correct itself
        - decide whether out-of-range values are rejected (preferred for an agent:
          explicit) or silently clamped as FunctionGenerator does today. the
          response should always report the value that was actually applied

    Dependencies in other files (do these first)
        - FunctionGenerator/FuncGenController need limit getters (frequency
          max is AD9833_MAX_FREQ, amplitude min is AMP_MIN and the max depends on the
          waveform), so get_limits and the schemas aren't hardcoded in two places

    Public interface (sketch)
        - begin(): register POST /mcp (and GET /mcp -> 405) on the HttpServer
*/
