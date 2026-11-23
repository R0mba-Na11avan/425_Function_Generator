'use strict';
const API_STATE = '{{API_STATE_PATH}}',
    API_INFO = '{{API_INFO_PATH}}';
const $ = (id) => document.getElementById(id);
const UNITS = { Hz: 1, kHz: 1e3, MHz: 1e6 };
const DECIMALS = { amplitude_vpp: 3, dc_offset_v: 3, phase_deg: 2 };
const KEYS = ['frequency_hz', 'amplitude_vpp', 'dc_offset_v', 'phase_deg'];
const UNIT_TEXT = { amplitude_vpp: 'Vpp', dc_offset_v: 'V', phase_deg: '°' };

//last: the whole /api/state reply. ch: the channel shown (0 = CH1). cur and
//ranges are that channel's
let last = null,
    ch = 0,
    curCh = -1,
    mirror = false;
let cur = null,
    ranges = null,
    peak = 10;
let pollMs = Number('{{WEB_POLL_MS}}'),
    timer = null,
    inflight = false,
    gen = 0,
    busy = 0;

//numbers

function trim(v, dp) {
    return String(+v.toFixed(dp));
}
function autoUnit(hz) {
    return hz >= 1e6 ? 'MHz' : hz >= 1e3 ? 'kHz' : 'Hz';
}
//0.1 Hz resolution in every unit
function inUnit(hz, u) {
    return trim(hz / UNITS[u], { Hz: 1, kHz: 4, MHz: 7 }[u]);
}
function hzText(hz) {
    const u = autoUnit(hz);
    return inUnit(hz, u) + ' ' + u;
}
function fieldText(key, v) {
    return key === 'frequency_hz' ? inUnit(v, $('unit').value) : trim(v, DECIMALS[key]);
}

//messages under a field

function msg(key, text, cls) {
    const m = $('f-' + key).querySelector('.msg');
    m.textContent = text || '';
    m.className = 'msg' + (cls ? ' ' + cls : '');
}
function flash(el) {
    el.classList.remove('flash');
    void el.offsetWidth;
    el.classList.add('flash');
}

function online(ok) {
    $('banner').hidden = ok;
    const c = $('conn');
    c.textContent = ok ? 'connected' : 'offline';
    c.className = ok ? 'ok' : 'err';
}

//state -> page

function showInput(key, force) {
    const inp = $('in-' + key);
    if (
        !force &&
        (document.activeElement === inp ||
            (key === 'frequency_hz' && document.activeElement === $('unit')))
    )
        return;
    if (key === 'frequency_hz' && !force) $('unit').value = autoUnit(cur.frequency_hz);
    inp.value = fieldText(key, cur[key]);
}

function rangeText(key) {
    const r = ranges[key];
    if (key === 'frequency_hz') return hzText(r.min) + ' to ' + hzText(r.max);
    if (key === 'phase_deg') return 'any value, wraps into 0 to 360°';
    return 'allowed now: ' + trim(r.min, 3) + ' to ' + trim(r.max, 3) + ' ' + UNIT_TEXT[key];
}

function apply(d, own) {
    last = d;
    //a channel switch shows the other channel's values as they are, without flashing
    const switched = curCh !== ch;
    const prev = switched ? null : cur,
        prevMirror = mirror;
    const c = d.channels[ch];
    cur = c.state;
    ranges = c.ranges;
    peak = d.output_peak_v;
    mirror = !!d.mirror;
    curCh = ch;
    document.querySelectorAll('#chan button').forEach((b, i) => {
        b.setAttribute('aria-pressed', i === ch);
        b.textContent = 'CH' + (i + 1) + (d.channels[i].state.output_enabled ? ' ON' : '');
    });
    const link = $('link');
    link.setAttribute('aria-pressed', mirror);
    //CH2 follows CH1 while linked: everything but its output is locked
    const locked = ch === 1 && mirror;
    $('lockmsg').hidden = !locked;
    document.querySelectorAll('#wave button').forEach((b) => {
        b.setAttribute('aria-pressed', b.dataset.w === cur.waveform);
        b.disabled = locked;
    });
    const out = $('out');
    out.setAttribute('aria-pressed', cur.output_enabled);
    out.textContent = cur.output_enabled ? 'ON' : 'OFF';
    if (switched) $('unit').value = autoUnit(cur.frequency_hz);
    KEYS.forEach((k) => {
        $('in-' + k).disabled = locked;
        showInput(k, switched);
        $('f-' + k).querySelector('.range').textContent = rangeText(k);
    });
    if (!own && prevMirror !== mirror) flash($('chancard'));
    if (prev && !own) {
        //changed somewhere else: knob, MCP, BLE or another browser
        KEYS.forEach((k) => {
            if (prev[k] !== cur[k]) flash($('f-' + k));
        });
        if (prev.waveform !== cur.waveform) flash($('wavecard'));
        if (prev.output_enabled !== cur.output_enabled) flash($('outcard'));
    }
    draw();
}

//page -> generator

async function send(key, value) {
    //the mirror is the one setting that isn't per channel
    const body = key === 'mirror' ? {} : { channel: ch + 1 };
    body[key] = value;
    //where a clamp note belongs: a waveform change can only clamp the amplitude
    const noteKey = key === 'waveform' ? 'amplitude_vpp' : key;
    if (KEYS.includes(key)) msg(key, '');
    gen++;
    busy++;
    try {
        const r = await fetch(API_STATE, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body),
        });
        let d = null;
        try {
            d = await r.json();
        } catch (e) {}
        online(true);
        if (!r.ok || !d || !d.channels) {
            if (KEYS.includes(key)) {
                msg(key, (d && d.error) || 'Error ' + r.status, 'err');
                if (cur) showInput(key, true);
            }
            return;
        }
        apply(d, true);
        if (KEYS.includes(key)) showInput(key, true);
        if (d.clamped) msg(noteKey, d.note, 'warn');
        else if (KEYS.includes(noteKey) && key === 'waveform') msg(noteKey, '');
    } catch (e) {
        online(false);
        if (KEYS.includes(key)) {
            msg(key, "Not sent: the generator can't be reached.", 'err');
            if (cur) showInput(key, true);
        }
    } finally {
        busy--;
        gen++;
    }
}

KEYS.forEach((key) => {
    const inp = $('in-' + key);
    inp.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') {
            e.preventDefault();
            inp.blur();
        } else if (e.key === 'Escape' && cur) {
            showInput(key, true);
            msg(key, '');
            inp.blur();
        }
    });
    //fires on Enter (through blur) or when focus leaves, only if the text changed
    inp.addEventListener('change', () => {
        if (!cur) return;
        const n = parseFloat(inp.value);
        if (!isFinite(n)) {
            msg(key, 'Enter a number.', 'err');
            showInput(key, true);
            return;
        }
        send(key, key === 'frequency_hz' ? n * UNITS[$('unit').value] : n);
    });
});

//a new unit shows the same frequency in that unit, nothing is sent
$('unit').addEventListener('change', () => {
    if (cur) showInput('frequency_hz', true);
});

document
    .querySelectorAll('#wave button')
    .forEach((b) => b.addEventListener('click', () => send('waveform', b.dataset.w)));
$('out').addEventListener('click', () => {
    if (cur) send('output_enabled', !cur.output_enabled);
});
$('link').addEventListener('click', () => {
    if (cur) send('mirror', !mirror);
});
document.querySelectorAll('#chan button').forEach((b) =>
    b.addEventListener('click', () => {
        ch = +b.dataset.ch;
        KEYS.forEach((k) => msg(k, ''));
        if (last) apply(last, true);
    }),
);

//polling

async function poll() {
    clearTimeout(timer);
    if (document.hidden || inflight) return;
    inflight = true;
    const started = gen;
    const ctl = new AbortController();
    const t = setTimeout(() => ctl.abort(), 3000);
    try {
        const r = await fetch(API_STATE, { cache: 'no-store', signal: ctl.signal });
        if (!r.ok) throw new Error(r.status);
        const d = await r.json();
        online(true);
        if (started === gen && !busy) apply(d, false);
    } catch (e) {
        online(false);
    } finally {
        clearTimeout(t);
        inflight = false;
        if (!document.hidden) timer = setTimeout(poll, pollMs);
    }
}

document.addEventListener('visibilitychange', () => {
    if (document.hidden) {
        clearTimeout(timer);
        clearTimeout(infoTimer);
    } else {
        poll();
        if (!$('dev').hidden) loadInfo();
    }
});

//waveform sketch: two cycles, y from -peak to +peak

function wave(w, x) {
    if (w === 'square') return Math.sin(x) >= 0 ? 1 : -1;
    if (w === 'triangle') return (2 / Math.PI) * Math.asin(Math.sin(x));
    return Math.sin(x);
}

function draw() {
    const c = $('plot');
    const dpr = window.devicePixelRatio || 1;
    const W = c.clientWidth,
        H = c.clientHeight;
    if (!W || !cur) return;
    c.width = Math.round(W * dpr);
    c.height = Math.round(H * dpr);
    const g = c.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    const css = getComputedStyle(document.documentElement);
    const col = (n) => css.getPropertyValue(n).trim();
    const dim = col('--dim'),
        line = col('--line'),
        acc = col('--acc');
    g.clearRect(0, 0, W, H);
    const pad = 6,
        lab = 44;
    const y = (v) => pad + ((peak - v) / (2 * peak)) * (H - 2 * pad);
    g.font = '11px system-ui,sans-serif';
    g.textBaseline = 'middle';
    [peak, 0, -peak].forEach((v) => {
        g.strokeStyle = line;
        g.lineWidth = 1;
        g.setLineDash(v ? [4, 4] : []);
        g.beginPath();
        g.moveTo(lab, y(v));
        g.lineTo(W, y(v));
        g.stroke();
        g.fillStyle = dim;
        g.fillText((v > 0 ? '+' : '') + trim(v, 1) + ' V', 2, Math.min(Math.max(y(v), 8), H - 8));
    });
    g.setLineDash([]);
    g.lineWidth = 2;
    if (!cur.output_enabled) {
        g.strokeStyle = dim;
        g.beginPath();
        g.moveTo(lab, y(0));
        g.lineTo(W, y(0));
        g.stroke();
        $('cap').textContent =
            'Output off: disconnected. The settings come back when it is turned on.';
        return;
    }
    const a = cur.amplitude_vpp / 2,
        o = cur.dc_offset_v,
        ph = (cur.phase_deg * Math.PI) / 180;
    g.strokeStyle = acc;
    g.beginPath();
    const n = Math.max(2, Math.round(W - lab));
    for (let i = 0; i <= n; i++) {
        const x = lab + ((W - lab) * i) / n;
        const v = o + a * wave(cur.waveform, ph + (4 * Math.PI * i) / n);
        if (i) g.lineTo(x, y(v));
        else g.moveTo(x, y(v));
    }
    g.stroke();
    $('cap').textContent =
        'Two cycles at ' +
        hzText(cur.frequency_hz) +
        ', swings ' +
        trim(o - a, 3) +
        ' V to ' +
        trim(o + a, 3) +
        ' V (limit ±' +
        trim(peak, 1) +
        ' V).';
}
window.addEventListener('resize', draw);

//device tab

let infoTimer = null;
const WIFI_TEXT = {
    idle: 'starting',
    connecting: 'joining',
    connected: 'connected',
    setup: 'setup mode',
};
const BLE_TEXT = {
    off: 'off',
    advertising: 'on, no phone connected',
    connected: 'phone connected',
};

function rows(list) {
    const dl = $('info');
    dl.replaceChildren();
    list.forEach(([k, v]) => {
        if (v === undefined || v === null || v === '') return;
        const dt = document.createElement('dt');
        dt.textContent = k;
        const dd = document.createElement('dd');
        dd.textContent = String(v);
        dl.append(dt, dd);
    });
}

async function loadInfo() {
    clearTimeout(infoTimer);
    try {
        const r = await fetch(API_INFO, { cache: 'no-store' });
        const d = await r.json();
        if (d.poll_ms) pollMs = d.poll_ms;
        const w = d.wifi || {},
            b = d.ble || {};
        rows([
            ['Firmware', d.firmware],
            ['Wi-Fi', WIFI_TEXT[w.state] || w.state],
            [w.state === 'setup' ? 'Setup network' : 'Network', w.ssid],
            ['Saved network', w.state === 'setup' ? w.saved_ssid || 'none' : ''],
            ['IP address', w.ip],
            [
                'Address',
                w.state === 'connected' && w.hostname ? 'http://' + w.hostname + '.local' : '',
            ],
            ['MCP URL', w.mcp_url],
            ['Bluetooth', BLE_TEXT[b.state] || b.state],
            ['Bluetooth name', b.name],
            ['Phones connected', b.state && b.state !== 'off' ? b.connected : ''],
            ['Phones paired', b.state && b.state !== 'off' ? b.paired : ''],
        ]);
    } catch (e) {
        rows([['Device info', 'could not be loaded']]);
    }
    if (!$('dev').hidden && !document.hidden) infoTimer = setTimeout(loadInfo, 5000);
}

function showTab(dev) {
    $('tab-ctl').setAttribute('aria-selected', !dev);
    $('tab-dev').setAttribute('aria-selected', dev);
    $('ctl').hidden = dev;
    $('dev').hidden = !dev;
    if (location.hash !== (dev ? '#device' : ''))
        history.replaceState(null, '', dev ? '#device' : location.pathname);
    if (dev) loadInfo();
    else {
        clearTimeout(infoTimer);
        draw();
    }
}
$('tab-ctl').addEventListener('click', () => showTab(false));
$('tab-dev').addEventListener('click', () => showTab(true));

//start: the info also carries poll_ms
showTab(location.hash === '#device');
if (location.hash !== '#device') loadInfo();
poll();
