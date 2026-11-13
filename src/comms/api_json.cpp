/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: JSON and request checks shared by the MCP server and the REST API
**************************************************************************/

#include "api_json.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

static const char* const WAVEFORM_NAMES[WAVEFORM_COUNT] = {"sine", "square", "triangle"};

const char* waveformJsonName(WAVEFORM waveform) {
    uint8_t i = (uint8_t)waveform;
    return (i < WAVEFORM_COUNT) ? WAVEFORM_NAMES[i] : "unknown";
}

bool parseWaveform(const char* name, WAVEFORM& out) {
    for (uint8_t i = 0; name && i < WAVEFORM_COUNT; i++) {
        if (strcasecmp(name, WAVEFORM_NAMES[i]) == 0) {
            out = (WAVEFORM)i;
            return true;
        }
    }
    return false;
}

void writeStateJson(const FuncGenState& s, JsonObject out) {
    out["waveform"] = waveformJsonName(s.waveform);
    out["frequency_hz"] = s.frequency;
    out["amplitude_vpp"] = s.amplitude;
    out["phase_deg"] = s.phase;
    out["dc_offset_v"] = s.dcOffset;
    out["output_enabled"] = s.outputEnabled;
}

void writeRangeJson(ValueRange r, JsonObject out) {
    out["min"] = r.min;
    out["max"] = r.max;
}

void writeLimitsJson(const FuncGenLimits& l, JsonObject out) {
    writeRangeJson(l.frequency, out["frequency_hz"].to<JsonObject>());
    JsonObject phase = out["phase_deg"].to<JsonObject>();
    writeRangeJson(l.phase, phase);
    phase["max_exclusive"] = true;
    JsonObject amp = out["amplitude_vpp"].to<JsonObject>();
    for (uint8_t i = 0; i < WAVEFORM_COUNT; i++) writeRangeJson(l.amplitude[i], amp[WAVEFORM_NAMES[i]].to<JsonObject>());
    writeRangeJson(l.dcOffset, out["dc_offset_v"].to<JsonObject>());
    out["output_peak_v"] = l.outputPeak;
}

void formatNumber(double v, char* out, size_t len) {
    if (isfinite(v) && fabs(v) < 1e15 && v == floor(v)) snprintf(out, len, "%.0f", v);
    else snprintf(out, len, "%.7g", v);
}

//origin check

static bool isIpv4(const char* s, size_t len) {
    int groups = 0;
    size_t i = 0;
    while (i < len) {
        int value = 0;
        size_t digits = 0;
        while (i < len && isdigit((unsigned char)s[i])) {
            value = value * 10 + (s[i] - '0');
            i++;
            if (++digits > 3) return false;
        }
        if (digits == 0 || value > 255) return false;
        groups++;
        if (i == len) break;
        if (s[i] != '.' || groups == 4) return false;
        i++;
        if (i == len) return false; //trailing dot
    }
    return groups == 4;
}

bool originAllowed(const char* origin) {
    if (!origin) return false;
    const char* host;
    if (strncasecmp(origin, "http://", 7) == 0) host = origin + 7;
    else if (strncasecmp(origin, "https://", 8) == 0) host = origin + 8;
    else return false; //includes "null"

    size_t hostLen = strcspn(host, ":/");
    const char* rest = host + hostLen;
    if (*rest == ':') {
        rest++;
        if (!*rest) return false;
        while (isdigit((unsigned char)*rest)) rest++;
    }
    if (*rest != '\0') return false; //an origin has no path

    static const char LOCAL_NAME[] = MDNS_HOSTNAME ".local";
    if (hostLen == sizeof(LOCAL_NAME) - 1 && strncasecmp(host, LOCAL_NAME, hostLen) == 0) return true;
    return isIpv4(host, hostLen);
}
