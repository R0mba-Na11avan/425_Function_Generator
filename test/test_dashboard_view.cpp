/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host tests for the dashboard drawing. renders a few screens
             with the real Adafruit_GFX code into canvases, checks that every
             area stays inside its box and draws the same on a canvas as on
             the panel, and saves each screen as a BMP in test/build/screens
             so the layout can be looked at without hardware
**************************************************************************/

#include "test.h"
#include <stdio.h>
#include <string.h>
#include "../src/ui/dashboard_view.h"
#include "../src/core/func_gen.h"
#include "../src/libs/Adafruit_GFX/Fonts/FreeSans9pt7b.h" //the info panel's font, to measure its lines

//set by run_tests.sh, which also creates the folder
#ifndef SCREENS_DIR
#define SCREENS_DIR "."
#endif

static const uint16_t SENTINEL = 0x0841; //near-black, never used by the dashboard
static const uint16_t REMOTE_COLOR = 0xF81F; //magenta frame/badge for agent changes
static const uint16_t BLE_COLOR = 0x3D9F;    //Bluetooth icon and pairing popup
static const uint16_t TEXT_COLOR = 0xFFFF;
static const uint16_t LABEL_COLOR = 0x8410;

static DashboardView baseView() {
    DashboardView v;
    memset(&v, 0, sizeof(v));
    v.state = FuncGenState{WAVEFORM::SINE, 1.0f, 0.0f, FunctionGenerator::amplitudeRange(WAVEFORM::SINE).min, 0.0f, false};
    v.net.state = WIFI_STATE::IDLE;
    v.selected = UI_FIELD::FREQUENCY;
    v.editing = false;
    v.freqStepIndex = UI_FREQ_STEP_DEFAULT;
    return v;
}

static DashboardView scenario(int i, const char*& name) {
    DashboardView v = baseView();
    switch (i) {
        case 0:
            name = "boot";
            break;
        case 1:
            name = "ap_mode_square_12M5_editing";
            v.state.waveform = WAVEFORM::SQUARE;
            v.state.frequency = 12500000.0f;
            v.state.outputEnabled = true;
            v.editing = true;
            v.freqStepIndex = 0;
            v.net.state = WIFI_STATE::AP_MODE;
            strcpy(v.net.ssid, "FuncGen-A1B2C3");
            strcpy(v.net.ip, "192.168.4.1");
            break;
        case 2:
            name = "connected_triangle_amp_editing";
            v.state.waveform = WAVEFORM::TRIANGLE;
            v.state.frequency = 1500.0f;
            v.state.amplitude = 2.35f;
            v.state.dcOffset = -1.25f;
            v.state.outputEnabled = true;
            v.selected = UI_FIELD::AMPLITUDE;
            v.editing = true;
            v.freqStepIndex = 2;
            v.net.state = WIFI_STATE::CONNECTED;
            strcpy(v.net.hostname, "funcgen");
            strcpy(v.net.ip, "192.168.1.137");
            break;
        case 4:
            name = "remote_change_flash_and_badge";
            v.state.frequency = 2500.0f;
            v.state.amplitude = 3.0f;
            v.state.outputEnabled = true;
            v.selected = UI_FIELD::FREQUENCY;
            v.remoteFlash = fieldBit(UI_FIELD::FREQUENCY) | fieldBit(UI_FIELD::AMPLITUDE);
            v.remoteBadge = true;
            v.net.state = WIFI_STATE::CONNECTED;
            strcpy(v.net.hostname, "funcgen");
            strcpy(v.net.ip, "192.168.1.137");
            break;
        case 5:
            name = "ble_change_badge_and_icon";
            v.state.waveform = WAVEFORM::SQUARE;
            v.state.outputEnabled = true;
            v.selected = UI_FIELD::WAVEFORM;
            v.remoteFlash = fieldBit(UI_FIELD::OUTPUT_ENABLE);
            v.remoteBadge = true;
            v.badgeSource = CHANGE_SOURCE::BLE;
            v.bleConnected = true;
            v.net.state = WIFI_STATE::CONNECTED;
            strcpy(v.net.hostname, "funcgen");
            strcpy(v.net.ip, "192.168.1.137");
            break;
        default:
            name = "connecting_long_ssid_output_selected";
            v.state.frequency = 440.0f;
            v.selected = UI_FIELD::OUTPUT_ENABLE;
            v.net.state = WIFI_STATE::CONNECTING;
            strcpy(v.net.ssid, "a-really-long-network-name-32chr");
            break;
    }
    return v;
}

static const int SCENARIO_COUNT = 6;

//one area through the canvas path, like DisplayUI with PSRAM
static void renderToCanvas(GFXcanvas16& canvas, uint8_t area, const DashboardView& v) {
    Rect r = areaRect(area);
    canvas.fillScreen(DASHBOARD_BG);
    renderArea(canvas, area, Rect{0, 0, r.w, r.h}, v);
}

static void saveBmp(const char* path, GFXcanvas16& screen) {
    int w = screen.width(), h = screen.height();
    int rowBytes = (w * 3 + 3) & ~3;
    uint32_t dataSize = rowBytes * h;
    uint8_t header[54] = {'B', 'M'};
    uint32_t fileSize = 54 + dataSize;
    memcpy(header + 2, &fileSize, 4);
    uint32_t offset = 54, infoSize = 40, planesBpp = 1 | (24 << 16);
    int32_t bw = w, bh = h;
    memcpy(header + 10, &offset, 4);
    memcpy(header + 14, &infoSize, 4);
    memcpy(header + 18, &bw, 4);
    memcpy(header + 22, &bh, 4);
    memcpy(header + 26, &planesBpp, 4);
    memcpy(header + 34, &dataSize, 4);

    FILE* f = fopen(path, "wb");
    if (!f) {
        printf("    could not write %s\n", path);
        return;
    }
    fwrite(header, 1, sizeof(header), f);
    uint8_t row[LCD_WIDTH * 3 + 4];
    for (int y = h - 1; y >= 0; y--) {
        memset(row, 0, sizeof(row));
        for (int x = 0; x < w; x++) {
            uint16_t c = screen.getPixel(x, y);
            row[x * 3 + 0] = (uint8_t)((c & 0x1F) * 255 / 31);
            row[x * 3 + 1] = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
            row[x * 3 + 2] = (uint8_t)((c >> 11) * 255 / 31);
        }
        fwrite(row, 1, rowBytes, f);
    }
    fclose(f);
}

TEST(every_area_stays_inside_its_box) {
    GFXcanvas16 screen(LCD_WIDTH, LCD_HEIGHT);
    for (int s = 0; s < SCENARIO_COUNT; s++) {
        const char* name;
        DashboardView v = scenario(s, name);
        for (uint8_t area = 0; area < DASHBOARD_AREA_COUNT; area++) {
            Rect r = areaRect(area);
            screen.fillScreen(SENTINEL);
            screen.fillRect(r.x, r.y, r.w, r.h, DASHBOARD_BG);
            renderArea(screen, area, r, v); //direct-to-panel path, absolute coordinates
            int outside = 0;
            for (int y = 0; y < LCD_HEIGHT; y++)
                for (int x = 0; x < LCD_WIDTH; x++)
                    if (!r.contains(x, y) && screen.getPixel(x, y) != SENTINEL) outside++;
            if (outside) printf("    %s: area %u drew %d pixels outside its box\n", name, area, outside);
            CHECK_EQ(outside, 0);
        }
    }
}

TEST(canvas_and_direct_drawing_match) {
    GFXcanvas16 screen(LCD_WIDTH, LCD_HEIGHT);
    for (int s = 0; s < SCENARIO_COUNT; s++) {
        const char* name;
        DashboardView v = scenario(s, name);
        for (uint8_t area = 0; area < DASHBOARD_AREA_COUNT; area++) {
            Rect r = areaRect(area);
            GFXcanvas16 canvas(r.w, r.h);
            renderToCanvas(canvas, area, v);

            screen.fillScreen(DASHBOARD_BG);
            renderArea(screen, area, r, v);

            int diff = 0;
            for (int y = 0; y < r.h; y++)
                for (int x = 0; x < r.w; x++)
                    if (canvas.getPixel(x, y) != screen.getPixel(r.x + x, r.y + y)) diff++;
            CHECK_EQ(diff, 0);
        }
    }
}

static int countColor(GFXcanvas16& c, uint16_t color) {
    int n = 0;
    for (int y = 0; y < c.height(); y++)
        for (int x = 0; x < c.width(); x++)
            if (c.getPixel(x, y) == color) n++;
    return n;
}

TEST(output_badge_colour_follows_state) {
    DashboardView v = baseView();
    Rect r = areaRect((uint8_t)UI_FIELD::OUTPUT_ENABLE);
    GFXcanvas16 canvas(r.w, r.h);

    renderToCanvas(canvas, (uint8_t)UI_FIELD::OUTPUT_ENABLE, v);
    CHECK(countColor(canvas, 0x8000) > 1000); //red badge
    CHECK_EQ(countColor(canvas, 0x07E0), 0);

    v.state.outputEnabled = true;
    renderToCanvas(canvas, (uint8_t)UI_FIELD::OUTPUT_ENABLE, v);
    CHECK(countColor(canvas, 0x07E0) > 1000); //green badge
}

TEST(ap_mode_status_bar_stands_out) {
    DashboardView v = baseView();
    Rect r = statusRect();
    GFXcanvas16 canvas(r.w, r.h);
    renderToCanvas(canvas, STATUS_AREA, v);
    CHECK_EQ(countColor(canvas, 0xFD20), 0);
    v.net.state = WIFI_STATE::AP_MODE;
    renderToCanvas(canvas, STATUS_AREA, v);
    CHECK(countColor(canvas, 0xFD20) > r.w * r.h / 2); //orange
}

TEST(area_keys_track_what_is_shown) {
    DashboardView a = baseView();
    DashboardView b = a;
    char ka[AREA_KEY_LEN], kb[AREA_KEY_LEN];
    for (uint8_t area = 0; area < DASHBOARD_AREA_COUNT; area++) {
        areaKey(a, area, ka, sizeof(ka));
        areaKey(b, area, kb, sizeof(kb));
        CHECK(strcmp(ka, kb) == 0);
    }

    //a frequency change only dirties the frequency field
    b.state.frequency = 2000.0f;
    for (uint8_t area = 0; area < DASHBOARD_AREA_COUNT; area++) {
        areaKey(a, area, ka, sizeof(ka));
        areaKey(b, area, kb, sizeof(kb));
        bool changed = strcmp(ka, kb) != 0;
        CHECK(changed == (area == (uint8_t)UI_FIELD::FREQUENCY));
    }

    //moving the selection dirties the old and the new field
    b = a;
    b.selected = UI_FIELD::AMPLITUDE;
    for (uint8_t area = 0; area < DASHBOARD_AREA_COUNT; area++) {
        areaKey(a, area, ka, sizeof(ka));
        areaKey(b, area, kb, sizeof(kb));
        bool changed = strcmp(ka, kb) != 0;
        CHECK(changed == (area == (uint8_t)UI_FIELD::FREQUENCY || area == (uint8_t)UI_FIELD::AMPLITUDE));
    }

    //output state is shown in two places (badge and preview colour)
    b = a;
    b.state.outputEnabled = true;
    areaKey(a, (uint8_t)UI_FIELD::WAVEFORM, ka, sizeof(ka));
    areaKey(b, (uint8_t)UI_FIELD::WAVEFORM, kb, sizeof(kb));
    CHECK(strcmp(ka, kb) != 0);
}

TEST(save_screens_as_bmp) {
    GFXcanvas16 screen(LCD_WIDTH, LCD_HEIGHT);
    for (int s = 0; s < SCENARIO_COUNT; s++) {
        const char* name;
        DashboardView v = scenario(s, name);
        screen.fillScreen(DASHBOARD_BG);
        for (uint8_t area = 0; area < DASHBOARD_AREA_COUNT; area++) {
            Rect r = areaRect(area);
            GFXcanvas16 canvas(r.w, r.h);
            renderToCanvas(canvas, area, v);
            screen.drawRGBBitmap(r.x, r.y, canvas.getBuffer(), r.w, r.h);
        }
        char path[128];
        snprintf(path, sizeof(path), "%s/%s.bmp", SCREENS_DIR, name);
        saveBmp(path, screen);
    }
}

TEST(remote_change_flash_and_badge) {
    DashboardView v = baseView();
    Rect fr = areaRect((uint8_t)UI_FIELD::FREQUENCY);
    GFXcanvas16 field(fr.w, fr.h);
    renderToCanvas(field, (uint8_t)UI_FIELD::FREQUENCY, v);
    CHECK_EQ(countColor(field, REMOTE_COLOR), 0);
    v.remoteFlash = fieldBit(UI_FIELD::FREQUENCY);
    renderToCanvas(field, (uint8_t)UI_FIELD::FREQUENCY, v);
    CHECK(countColor(field, REMOTE_COLOR) > 2 * (fr.w + fr.h)); //thick frame
    //other fields don't flash
    Rect ar = areaRect((uint8_t)UI_FIELD::AMPLITUDE);
    GFXcanvas16 amp(ar.w, ar.h);
    renderToCanvas(amp, (uint8_t)UI_FIELD::AMPLITUDE, v);
    CHECK_EQ(countColor(amp, REMOTE_COLOR), 0);

    Rect sr = statusRect();
    GFXcanvas16 status(sr.w, sr.h);
    renderToCanvas(status, STATUS_AREA, v);
    CHECK_EQ(countColor(status, REMOTE_COLOR), 0);
    v.remoteBadge = true;
    renderToCanvas(status, STATUS_AREA, v);
    Rect b = mcpBadgeRect();
    CHECK(countColor(status, REMOTE_COLOR) > b.w * b.h / 2);
    //the badge sits inside the status bar and clear of the network text
    CHECK(sr.contains(b.x, b.y) && sr.contains(b.x + b.w - 1, b.y + b.h - 1));
    CHECK(4 + 42 * 6 <= b.x);
}

TEST(remote_flash_and_badge_change_area_keys) {
    DashboardView a = baseView();
    DashboardView b = a;
    b.remoteFlash = fieldBit(UI_FIELD::OFFSET);
    b.remoteBadge = true;
    char ka[AREA_KEY_LEN], kb[AREA_KEY_LEN];
    for (uint8_t area = 0; area < DASHBOARD_AREA_COUNT; area++) {
        areaKey(a, area, ka, sizeof(ka));
        areaKey(b, area, kb, sizeof(kb));
        bool changed = strcmp(ka, kb) != 0;
        CHECK(changed == (area == (uint8_t)UI_FIELD::OFFSET || area == STATUS_AREA));
    }

    //who made the change and the Bluetooth icon only touch the status bar
    DashboardView c = b;
    c.badgeSource = CHANGE_SOURCE::BLE;
    DashboardView d = b;
    d.bleConnected = true;
    for (uint8_t area = 0; area < DASHBOARD_AREA_COUNT; area++) {
        areaKey(b, area, ka, sizeof(ka));
        areaKey(c, area, kb, sizeof(kb));
        CHECK((strcmp(ka, kb) != 0) == (area == STATUS_AREA));
        areaKey(d, area, kb, sizeof(kb));
        CHECK((strcmp(ka, kb) != 0) == (area == STATUS_AREA));
    }
}

TEST(badge_names_mcp_or_ble) {
    DashboardView v = baseView();
    v.remoteBadge = true;
    Rect sr = statusRect();
    GFXcanvas16 mcp(sr.w, sr.h), ble(sr.w, sr.h);
    v.badgeSource = CHANGE_SOURCE::MCP;
    renderToCanvas(mcp, STATUS_AREA, v);
    v.badgeSource = CHANGE_SOURCE::BLE;
    renderToCanvas(ble, STATUS_AREA, v);

    //same pill, different text: only pixels inside the badge differ
    Rect b = mcpBadgeRect();
    int inside = 0, outside = 0;
    for (int y = 0; y < sr.h; y++)
        for (int x = 0; x < sr.w; x++)
            if (mcp.getPixel(x, y) != ble.getPixel(x, y)) (b.contains(x, y) ? inside : outside)++;
    CHECK(inside > 10);
    CHECK_EQ(outside, 0);
}

TEST(bluetooth_icon_only_while_connected) {
    DashboardView v = baseView();
    Rect sr = statusRect();
    GFXcanvas16 status(sr.w, sr.h);
    renderToCanvas(status, STATUS_AREA, v);
    CHECK_EQ(countColor(status, BLE_COLOR), 0);

    v.bleConnected = true;
    renderToCanvas(status, STATUS_AREA, v);
    Rect icon = bleSlotRect();
    int in = 0, out = 0;
    for (int y = 0; y < sr.h; y++)
        for (int x = 0; x < sr.w; x++)
            if (status.getPixel(x, y) == BLE_COLOR) (icon.contains(x, y) ? in : out)++;
    CHECK(in > 30);
    CHECK_EQ(out, 0);
    //clear of the badge
    Rect badge = mcpBadgeRect();
    CHECK(badge.x + badge.w <= icon.x);
}

static NetStatus connectedNet() {
    NetStatus n = {};
    n.state = WIFI_STATE::CONNECTED;
    strcpy(n.ssid, "ECE425-Lab");
    strcpy(n.ip, "192.168.1.137");
    strcpy(n.hostname, "funcgen");
    return n;
}

static NetStatus apNet() {
    NetStatus n = {};
    n.state = WIFI_STATE::AP_MODE;
    strcpy(n.ssid, "FuncGen-A1B2");
    strcpy(n.ip, "192.168.4.1");
    strcpy(n.hostname, "funcgen");
    return n;
}

static BleStatus bleConnected() {
    BleStatus b = {};
    b.state = BLE_STATE::CONNECTED;
    strcpy(b.name, "FuncGen-A1B2");
    b.connected = 3;
    b.bonded = 3;
    return b;
}

TEST(info_panel_stays_inside_and_lines_fit) {
    GFXcanvas16 screen(LCD_WIDTH, LCD_HEIGHT);
    NetStatus nets[] = {connectedNet(), apNet()};
    BleStatus ble = bleConnected();
    for (NetStatus& n : nets) {
        Rect r = infoRect();
        screen.fillScreen(SENTINEL);
        screen.fillRect(r.x, r.y, r.w, r.h, DASHBOARD_BG);
        renderInfo(screen, r, n, ble);
        int outside = 0;
        for (int y = 0; y < LCD_HEIGHT; y++)
            for (int x = 0; x < LCD_WIDTH; x++)
                if (!r.contains(x, y) && screen.getPixel(x, y) != SENTINEL) outside++;
        CHECK_EQ(outside, 0);

        //with a normal SSID every line fits inside the frame
        char lines[INFO_MAX_LINES][INFO_LINE_LEN];
        uint8_t count = formatInfoLines(n, ble, lines, INFO_MAX_LINES);
        screen.setFont(nullptr);
        for (uint8_t i = 0; i < count; i++) {
            screen.setFont(&FreeSans9pt7b);
            int16_t x1, y1;
            uint16_t w, h;
            screen.getTextBounds(lines[i], 0, 0, &x1, &y1, &w, &h);
            if (x1 + w > r.w - 24) printf("    info line too wide: \"%s\" (%u px)\n", lines[i], w);
            CHECK(x1 + w <= r.w - 24);
        }
    }
}

TEST(info_key_tracks_network) {
    char a[AREA_KEY_LEN], b[AREA_KEY_LEN];
    NetStatus n = connectedNet();
    BleStatus ble = bleConnected();
    infoKey(n, ble, a, sizeof(a));
    infoKey(n, ble, b, sizeof(b));
    CHECK(strcmp(a, b) == 0);
    strcpy(n.ip, "192.168.1.200");
    infoKey(n, ble, b, sizeof(b));
    CHECK(strcmp(a, b) != 0);

    //and Bluetooth
    n = connectedNet();
    BleStatus changes[3] = {ble, ble, ble};
    changes[0].state = BLE_STATE::ADVERTISING;
    changes[1].connected = 1;
    changes[2].bonded = 1;
    for (const BleStatus& c : changes) {
        infoKey(n, c, b, sizeof(b));
        CHECK(strcmp(a, b) != 0);
    }
}

//pairing popup

static void checkNoTextAtEdges(GFXcanvas16& screen) {
    //text that doesn't fit would be cut at the screen edge (or run into the frame)
    int touching = 0;
    for (int y = 0; y < LCD_HEIGHT; y++)
        for (int x = 0; x < LCD_WIDTH; x++) {
            if (x >= 6 && x < LCD_WIDTH - 6) continue;
            uint16_t c = screen.getPixel(x, y);
            if (c == TEXT_COLOR || c == LABEL_COLOR) touching++;
        }
    CHECK_EQ(touching, 0);
}

TEST(pairing_popup_fits) {
    GFXcanvas16 screen(LCD_WIDTH, LCD_HEIGHT);
    PAIR_PROMPT shown[] = {PAIR_PROMPT::CODE, PAIR_PROMPT::PAIRED, PAIR_PROMPT::FAILED};
    for (PAIR_PROMPT s : shown) {
        screen.fillScreen(SENTINEL);
        renderPairing(screen, s, 888888, 30);
        CHECK_EQ(countColor(screen, SENTINEL), 0);  //covers the whole screen
        checkNoTextAtEdges(screen);
        CHECK(countColor(screen, BLE_COLOR) > 100);   //frame + icon
    }
}

TEST(pairing_popup_shows_all_six_digits) {
    //leading zeros are digits too: 1234 is typed as 001234, so the first cell holds a "0"
    GFXcanvas16 a(LCD_WIDTH, LCD_HEIGHT);
    renderPairing(a, PAIR_PROMPT::CODE, 1234, 30);
    int16_t firstX = (LCD_WIDTH - numTextWidth("000 000")) / 2;
    int ink = 0;
    for (int y = 110; y < 170; y++)
        for (int x = firstX; x < firstX + numCharWidth('0'); x++)
            if (a.getPixel(x, y) == TEXT_COLOR) ink++;
    CHECK(ink > 50);
}

TEST(pairing_key_tracks_what_is_shown) {
    char a[AREA_KEY_LEN], b[AREA_KEY_LEN];
    pairingKey(PAIR_PROMPT::CODE, 123456, 30, a, sizeof(a));
    pairingKey(PAIR_PROMPT::CODE, 123456, 30, b, sizeof(b));
    CHECK(strcmp(a, b) == 0);
    pairingKey(PAIR_PROMPT::CODE, 123456, 29, b, sizeof(b));
    CHECK(strcmp(a, b) != 0);
    pairingKey(PAIR_PROMPT::CODE, 123457, 30, b, sizeof(b));
    CHECK(strcmp(a, b) != 0);
    pairingKey(PAIR_PROMPT::PAIRED, 123456, 30, b, sizeof(b));
    CHECK(strcmp(a, b) != 0);
}

TEST(save_extra_screens_as_bmp) {
    GFXcanvas16 screen(LCD_WIDTH, LCD_HEIGHT);
    char path[128];

    NetStatus nets[] = {connectedNet(), apNet()};
    const char* names[] = {"info_connected", "info_ap_mode"};
    for (int i = 0; i < 2; i++) {
        DashboardView v = baseView();
        v.net = nets[i];
        screen.fillScreen(DASHBOARD_BG);
        Rect sr = statusRect();
        GFXcanvas16 status(sr.w, sr.h);
        renderToCanvas(status, STATUS_AREA, v);
        screen.drawRGBBitmap(sr.x, sr.y, status.getBuffer(), sr.w, sr.h);
        renderInfo(screen, infoRect(), v.net, bleConnected());
        snprintf(path, sizeof(path), "%s/%s.bmp", SCREENS_DIR, names[i]);
        saveBmp(path, screen);
    }

    renderBootHold(screen, 3);
    snprintf(path, sizeof(path), "%s/boot_hold.bmp", SCREENS_DIR);
    saveBmp(path, screen);
    renderBootMessage(screen, "Wi-Fi and phones forgotten", "Restarting...");
    snprintf(path, sizeof(path), "%s/boot_message.bmp", SCREENS_DIR);
    saveBmp(path, screen);

    PAIR_PROMPT shown[] = {PAIR_PROMPT::CODE, PAIR_PROMPT::PAIRED, PAIR_PROMPT::FAILED};
    const char* pairNames[] = {"pairing_code", "pairing_paired", "pairing_failed"};
    for (int i = 0; i < 3; i++) {
        renderPairing(screen, shown[i], 47213, 27);
        snprintf(path, sizeof(path), "%s/%s.bmp", SCREENS_DIR, pairNames[i]);
        saveBmp(path, screen);
    }
}
