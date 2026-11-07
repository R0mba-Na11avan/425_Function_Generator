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

//set by run_tests.sh, which also creates the folder
#ifndef SCREENS_DIR
#define SCREENS_DIR "."
#endif

static const uint16_t SENTINEL = 0xF81F; //magenta, never used by the dashboard

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

static const int SCENARIO_COUNT = 4;

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
