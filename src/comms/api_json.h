/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: JSON and request checks shared by the MCP server and the REST
             API, so both interfaces name and format the settings the same way
**************************************************************************/

/*
    State JSON (MCP's get_state / set_* results and the REST API's "state")
        { "waveform": "sine" | "square" | "triangle", "frequency_hz", "amplitude_vpp",
          "phase_deg", "dc_offset_v", "output_enabled" }

    Origin check
        - a browser always sends Origin on POST. it must be
          http(s)://<MDNS_HOSTNAME>.local or an IPv4 address, so a page on
          another site (or an attacker's domain resolving to the device's IP,
          DNS rebinding) can't drive the generator. clients outside a browser
          send no Origin and are let through by the caller
*/

#ifndef API_JSON_H
#define API_JSON_H

#include <stddef.h>
#include "../config.h"
#include "../core/funcgen_state.h"
#include "../libs/ArduinoJson/ArduinoJson.h"

//"sine", "square", "triangle" ("unknown" for anything else)
const char* waveformJsonName(WAVEFORM waveform);

//case-insensitive. false if name is null or not a waveform
bool parseWaveform(const char* name, WAVEFORM& out);

void writeStateJson(const FuncGenState& state, JsonObject out);

//{ min, max }
void writeRangeJson(ValueRange range, JsonObject out);

//the widest ranges (amplitude per waveform) and output_peak_v, as MCP's get_limits
void writeLimitsJson(const FuncGenLimits& limits, JsonObject out);

//whole numbers without an exponent (12500000, not 1.25e+07), the rest to float precision
void formatNumber(double value, char* out, size_t len);

//true if a browser Origin header may talk to the server (see above)
bool originAllowed(const char* origin);

#endif
