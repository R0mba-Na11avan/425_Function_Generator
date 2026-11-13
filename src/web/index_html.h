/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: the web control page, embedded as a PROGMEM string
**************************************************************************/

/*
    One self-contained page (inline CSS and JS, no external files or CDNs,
    since a client on the setup AP has no internet). Served by RestApi
    (comms/rest_api.h) at WEB_CONTROL_PATH, and at / except in setup mode.
    Plain styling for now, a nicer look is planned later.

    Controls tab
        - waveform buttons, output toggle, number fields for frequency (with a
          Hz/kHz/MHz unit), amplitude, DC offset and phase
        - a field is sent when Enter is pressed or it loses focus (one
          POST API_STATE_PATH per change, one setting each). Escape undoes the
          typing. out-of-range values are clamped by the firmware and the note
          it returns shows under the field
        - next to amplitude and offset: the range allowed right now (they
          share the peak limit, so each moves with the other)
        - a sketch of two cycles of the output, scaled to +-output_peak_v
    Device tab
        - firmware, Wi-Fi and Bluetooth info from API_INFO_PATH, read-only,
          refreshed while the tab is open

    Live sync
        - polls API_STATE_PATH every poll_ms (from /api/info, WEB_POLL_MS),
          paused while the browser tab is hidden. a field being typed in is
          never overwritten. fields changed elsewhere (knob, MCP, BLE, another
          browser) flash briefly
        - a poll that started before one of our own changes finished is
          dropped, so it can't put the old value back for a moment
        - a banner shows while the generator can't be reached

    Text from the device (SSIDs, BLE name) is only inserted with textContent.
*/

#ifndef INDEX_HTML_H
#define INDEX_HTML_H

#include <Arduino.h>
#include "../config.h"

//puts a number from config.h into the page
#define INDEX_HTML_STR_(x) #x
#define INDEX_HTML_STR(x) INDEX_HTML_STR_(x)

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Function Generator</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1c1f24;--dim:#6b7280;--line:#dde1e6;--acc:#0b6bcb;--err:#b42318;--warn:#b54708;--ok:#067647;--flash:#fff1a8}
@media (prefers-color-scheme:dark){:root{--bg:#121417;--card:#1c1f24;--fg:#e8eaed;--dim:#9aa0a6;--line:#30353c;--acc:#5ea8f2;--err:#f97066;--warn:#fdb022;--ok:#47cd89;--flash:#5c4d00}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:520px;margin:0 auto;padding:16px}
header{display:flex;align-items:center;justify-content:space-between;gap:8px}
h1{font-size:20px;margin:8px 0}
h2{font-size:15px;margin:0 0 8px}
#conn{font-size:13px;color:var(--dim)}
#conn.ok{color:var(--ok)}
#conn.err{color:var(--err)}
nav{display:flex;gap:4px;margin:8px 0 12px;border-bottom:1px solid var(--line)}
nav button{padding:8px 14px;border:0;border-bottom:2px solid transparent;background:none;color:var(--dim);font:inherit;cursor:pointer}
nav button[aria-selected=true]{color:var(--fg);border-bottom-color:var(--acc);font-weight:600}
.banner{padding:10px;border-radius:8px;margin-bottom:12px;border:1px solid var(--err);color:var(--err);font-size:14px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px;margin-bottom:12px}
.row{display:flex;align-items:center;justify-content:space-between;gap:8px}
.row h2{margin:0}
.seg{display:flex;gap:6px;margin-bottom:10px}
.seg button,.toggle{flex:1;padding:10px;border:1px solid var(--line);border-radius:8px;background:none;color:inherit;font:inherit;cursor:pointer}
.seg button[aria-pressed=true]{border-color:var(--acc);background:var(--acc);color:#fff;font-weight:600}
.toggle{flex:0 0 96px;font-weight:600}
.toggle[aria-pressed=true]{border-color:var(--ok);background:var(--ok);color:#fff}
canvas{display:block;width:100%;height:150px;border:1px solid var(--line);border-radius:8px}
#cap{font-size:13px;color:var(--dim);margin-top:4px}
.field{padding:6px;margin:0 -6px 6px;border-radius:8px}
.field label{display:block;font-size:14px;color:var(--dim);margin-bottom:4px}
.inrow{display:flex;gap:6px;align-items:center}
.inrow input{flex:1;min-width:0;padding:10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:inherit;font:inherit}
.inrow select{padding:10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:inherit;font:inherit}
.unit{min-width:36px;color:var(--dim)}
.range{font-size:13px;color:var(--dim);margin-top:2px}
.msg{font-size:13px;min-height:0}
.msg.err{color:var(--err)}
.msg.warn{color:var(--warn)}
.flash{animation:flash 1.5s ease-out}
@keyframes flash{from{background:var(--flash)}to{background:transparent}}
dl{display:grid;grid-template-columns:auto 1fr;gap:6px 12px;margin:0}
dt{color:var(--dim);font-size:14px}
dd{margin:0;overflow-wrap:anywhere}
.hint{color:var(--dim);font-size:13px}
</style></head><body><main>
<header><h1>Function Generator</h1><span id="conn">connecting...</span></header>
<nav role="tablist">
<button role="tab" id="tab-ctl" aria-controls="ctl" aria-selected="true">Controls</button>
<button role="tab" id="tab-dev" aria-controls="dev" aria-selected="false">Device</button>
</nav>
<div id="banner" class="banner" hidden>Can't reach the generator. Retrying...</div>

<section id="ctl" role="tabpanel" aria-labelledby="tab-ctl">
<div class="card" id="outcard">
<div class="row"><h2>Output</h2><button id="out" class="toggle" aria-pressed="false">OFF</button></div>
</div>

<div class="card" id="wavecard">
<h2>Waveform</h2>
<div class="seg" id="wave">
<button data-w="sine" aria-pressed="false">Sine</button>
<button data-w="square" aria-pressed="false">Square</button>
<button data-w="triangle" aria-pressed="false">Triangle</button>
</div>
<canvas id="plot"></canvas>
<div id="cap"></div>
</div>

<div class="card">
<div class="field" id="f-frequency_hz">
<label for="in-frequency_hz">Frequency</label>
<div class="inrow"><input id="in-frequency_hz" type="number" step="any" inputmode="decimal">
<select id="unit" aria-label="Frequency unit"><option>Hz</option><option>kHz</option><option>MHz</option></select></div>
<div class="range"></div><div class="msg"></div>
</div>
<div class="field" id="f-amplitude_vpp">
<label for="in-amplitude_vpp">Amplitude</label>
<div class="inrow"><input id="in-amplitude_vpp" type="number" step="any" inputmode="decimal"><span class="unit">Vpp</span></div>
<div class="range"></div><div class="msg"></div>
</div>
<div class="field" id="f-dc_offset_v">
<label for="in-dc_offset_v">DC offset</label>
<div class="inrow"><input id="in-dc_offset_v" type="number" step="any" inputmode="decimal"><span class="unit">V</span></div>
<div class="range"></div><div class="msg"></div>
</div>
<div class="field" id="f-phase_deg">
<label for="in-phase_deg">Phase</label>
<div class="inrow"><input id="in-phase_deg" type="number" step="any" inputmode="decimal"><span class="unit">&deg;</span></div>
<div class="range"></div><div class="msg"></div>
</div>
<div class="hint">Press Enter or leave a field to send it. Esc undoes the typing.</div>
</div>
</section>

<section id="dev" role="tabpanel" aria-labelledby="tab-dev" hidden>
<div class="card"><dl id="info"></dl></div>
<div class="hint">Wi-Fi changes and forgetting phones are only on the generator's screen.</div>
</section>

<script>
'use strict';
const API_STATE=')HTML" API_STATE_PATH R"HTML(', API_INFO=')HTML" API_INFO_PATH R"HTML(';
const $=id=>document.getElementById(id);
const UNITS={Hz:1,kHz:1e3,MHz:1e6};
const DECIMALS={amplitude_vpp:3,dc_offset_v:3,phase_deg:2};
const KEYS=['frequency_hz','amplitude_vpp','dc_offset_v','phase_deg'];
const UNIT_TEXT={amplitude_vpp:'Vpp',dc_offset_v:'V',phase_deg:'°'};

let cur=null,ranges=null,peak=10;
let pollMs=)HTML" INDEX_HTML_STR(WEB_POLL_MS) R"HTML(,timer=null,inflight=false,gen=0,busy=0;

//numbers

function trim(v,dp){return String(+v.toFixed(dp))}
function autoUnit(hz){return hz>=1e6?'MHz':hz>=1e3?'kHz':'Hz'}
//0.1 Hz resolution in every unit
function inUnit(hz,u){return trim(hz/UNITS[u],{Hz:1,kHz:4,MHz:7}[u])}
function hzText(hz){const u=autoUnit(hz);return inUnit(hz,u)+' '+u}
function fieldText(key,v){return key==='frequency_hz'?inUnit(v,$('unit').value):trim(v,DECIMALS[key])}

//messages under a field

function msg(key,text,cls){const m=$('f-'+key).querySelector('.msg');m.textContent=text||'';m.className='msg'+(cls?' '+cls:'')}
function flash(el){el.classList.remove('flash');void el.offsetWidth;el.classList.add('flash')}

function online(ok){
  $('banner').hidden=ok;
  const c=$('conn');c.textContent=ok?'connected':'offline';c.className=ok?'ok':'err';
}

//state -> page

function showInput(key,force){
  const inp=$('in-'+key);
  if(!force&&(document.activeElement===inp||(key==='frequency_hz'&&document.activeElement===$('unit'))))return;
  if(key==='frequency_hz'&&!force)$('unit').value=autoUnit(cur.frequency_hz);
  inp.value=fieldText(key,cur[key]);
}

function rangeText(key){
  const r=ranges[key];
  if(key==='frequency_hz')return hzText(r.min)+' to '+hzText(r.max);
  if(key==='phase_deg')return 'any value, wraps into 0 to 360°';
  return 'allowed now: '+trim(r.min,3)+' to '+trim(r.max,3)+' '+UNIT_TEXT[key];
}

function apply(d,own){
  const prev=cur;
  cur=d.state;ranges=d.ranges;peak=d.output_peak_v;
  document.querySelectorAll('#wave button').forEach(b=>b.setAttribute('aria-pressed',b.dataset.w===cur.waveform));
  const out=$('out');
  out.setAttribute('aria-pressed',cur.output_enabled);
  out.textContent=cur.output_enabled?'ON':'OFF';
  KEYS.forEach(k=>{
    showInput(k,false);
    $('f-'+k).querySelector('.range').textContent=rangeText(k);
  });
  if(prev&&!own){
    //changed somewhere else: knob, MCP, BLE or another browser
    KEYS.forEach(k=>{if(prev[k]!==cur[k])flash($('f-'+k))});
    if(prev.waveform!==cur.waveform)flash($('wavecard'));
    if(prev.output_enabled!==cur.output_enabled)flash($('outcard'));
  }
  draw();
}

//page -> generator

async function send(key,value){
  const body={};body[key]=value;
  //where a clamp note belongs: a waveform change can only clamp the amplitude
  const noteKey=key==='waveform'?'amplitude_vpp':key;
  if(KEYS.includes(key))msg(key,'');
  gen++;busy++;
  try{
    const r=await fetch(API_STATE,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    let d=null;
    try{d=await r.json()}catch(e){}
    online(true);
    if(!r.ok||!d||!d.state){
      if(KEYS.includes(key)){msg(key,(d&&d.error)||('Error '+r.status),'err');if(cur)showInput(key,true)}
      return;
    }
    apply(d,true);
    if(KEYS.includes(key))showInput(key,true);
    if(d.clamped)msg(noteKey,d.note,'warn');
    else if(KEYS.includes(noteKey)&&key==='waveform')msg(noteKey,'');
  }catch(e){
    online(false);
    if(KEYS.includes(key)){msg(key,'Not sent: the generator can\'t be reached.','err');if(cur)showInput(key,true)}
  }finally{
    busy--;gen++;
  }
}

KEYS.forEach(key=>{
  const inp=$('in-'+key);
  inp.addEventListener('keydown',e=>{
    if(e.key==='Enter'){e.preventDefault();inp.blur()}
    else if(e.key==='Escape'&&cur){showInput(key,true);msg(key,'');inp.blur()}
  });
  //fires on Enter (through blur) or when focus leaves, only if the text changed
  inp.addEventListener('change',()=>{
    if(!cur)return;
    const n=parseFloat(inp.value);
    if(!isFinite(n)){msg(key,'Enter a number.','err');showInput(key,true);return}
    send(key,key==='frequency_hz'?n*UNITS[$('unit').value]:n);
  });
});

//a new unit shows the same frequency in that unit, nothing is sent
$('unit').addEventListener('change',()=>{if(cur)showInput('frequency_hz',true)});

document.querySelectorAll('#wave button').forEach(b=>b.addEventListener('click',()=>send('waveform',b.dataset.w)));
$('out').addEventListener('click',()=>{if(cur)send('output_enabled',!cur.output_enabled)});

//polling

async function poll(){
  clearTimeout(timer);
  if(document.hidden||inflight)return;
  inflight=true;
  const started=gen;
  const ctl=new AbortController();
  const t=setTimeout(()=>ctl.abort(),3000);
  try{
    const r=await fetch(API_STATE,{cache:'no-store',signal:ctl.signal});
    if(!r.ok)throw new Error(r.status);
    const d=await r.json();
    online(true);
    if(started===gen&&!busy)apply(d,false);
  }catch(e){
    online(false);
  }finally{
    clearTimeout(t);
    inflight=false;
    if(!document.hidden)timer=setTimeout(poll,pollMs);
  }
}

document.addEventListener('visibilitychange',()=>{
  if(document.hidden){clearTimeout(timer);clearTimeout(infoTimer)}
  else{poll();if(!$('dev').hidden)loadInfo()}
});

//waveform sketch: two cycles, y from -peak to +peak

function wave(w,x){
  if(w==='square')return Math.sin(x)>=0?1:-1;
  if(w==='triangle')return 2/Math.PI*Math.asin(Math.sin(x));
  return Math.sin(x);
}

function draw(){
  const c=$('plot');
  const dpr=window.devicePixelRatio||1;
  const W=c.clientWidth,H=c.clientHeight;
  if(!W||!cur)return;
  c.width=Math.round(W*dpr);c.height=Math.round(H*dpr);
  const g=c.getContext('2d');
  g.setTransform(dpr,0,0,dpr,0,0);
  const css=getComputedStyle(document.documentElement);
  const col=n=>css.getPropertyValue(n).trim();
  const dim=col('--dim'),line=col('--line'),acc=col('--acc');
  g.clearRect(0,0,W,H);
  const pad=6,lab=44;
  const y=v=>pad+(peak-v)/(2*peak)*(H-2*pad);
  g.font='11px system-ui,sans-serif';g.textBaseline='middle';
  [peak,0,-peak].forEach(v=>{
    g.strokeStyle=line;g.lineWidth=1;g.setLineDash(v?[4,4]:[]);
    g.beginPath();g.moveTo(lab,y(v));g.lineTo(W,y(v));g.stroke();
    g.fillStyle=dim;g.fillText((v>0?'+':'')+trim(v,1)+' V',2,Math.min(Math.max(y(v),8),H-8));
  });
  g.setLineDash([]);
  g.lineWidth=2;
  if(!cur.output_enabled){
    g.strokeStyle=dim;
    g.beginPath();g.moveTo(lab,y(0));g.lineTo(W,y(0));g.stroke();
    $('cap').textContent='Output off: 0 V. The settings come back when it is turned on.';
    return;
  }
  const a=cur.amplitude_vpp/2,o=cur.dc_offset_v,ph=cur.phase_deg*Math.PI/180;
  g.strokeStyle=acc;
  g.beginPath();
  const n=Math.max(2,Math.round(W-lab));
  for(let i=0;i<=n;i++){
    const x=lab+(W-lab)*i/n;
    const v=o+a*wave(cur.waveform,ph+4*Math.PI*i/n);
    if(i)g.lineTo(x,y(v));else g.moveTo(x,y(v));
  }
  g.stroke();
  $('cap').textContent='Two cycles at '+hzText(cur.frequency_hz)+', swings '+trim(o-a,3)+' V to '+trim(o+a,3)+' V (limit ±'+trim(peak,1)+' V).';
}
window.addEventListener('resize',draw);

//device tab

let infoTimer=null;
const WIFI_TEXT={idle:'starting',connecting:'joining',connected:'connected',setup:'setup mode'};
const BLE_TEXT={off:'off',advertising:'on, no phone connected',connected:'phone connected'};

function rows(list){
  const dl=$('info');dl.replaceChildren();
  list.forEach(([k,v])=>{
    if(v===undefined||v===null||v==='')return;
    const dt=document.createElement('dt');dt.textContent=k;
    const dd=document.createElement('dd');dd.textContent=String(v);
    dl.append(dt,dd);
  });
}

async function loadInfo(){
  clearTimeout(infoTimer);
  try{
    const r=await fetch(API_INFO,{cache:'no-store'});
    const d=await r.json();
    if(d.poll_ms)pollMs=d.poll_ms;
    const w=d.wifi||{},b=d.ble||{};
    rows([
      ['Firmware',d.firmware],
      ['Wi-Fi',WIFI_TEXT[w.state]||w.state],
      [w.state==='setup'?'Setup network':'Network',w.ssid],
      ['Saved network',w.state==='setup'?(w.saved_ssid||'none'):''],
      ['IP address',w.ip],
      ['Address',w.state==='connected'&&w.hostname?'http://'+w.hostname+'.local':''],
      ['MCP URL',w.mcp_url],
      ['Bluetooth',BLE_TEXT[b.state]||b.state],
      ['Bluetooth name',b.name],
      ['Phones connected',b.state&&b.state!=='off'?b.connected:''],
      ['Phones paired',b.state&&b.state!=='off'?b.paired:''],
    ]);
  }catch(e){
    rows([['Device info','could not be loaded']]);
  }
  if(!$('dev').hidden&&!document.hidden)infoTimer=setTimeout(loadInfo,5000);
}

function showTab(dev){
  $('tab-ctl').setAttribute('aria-selected',!dev);
  $('tab-dev').setAttribute('aria-selected',dev);
  $('ctl').hidden=dev;$('dev').hidden=!dev;
  if(location.hash!==(dev?'#device':''))history.replaceState(null,'',dev?'#device':location.pathname);
  if(dev)loadInfo();else{clearTimeout(infoTimer);draw()}
}
$('tab-ctl').addEventListener('click',()=>showTab(false));
$('tab-dev').addEventListener('click',()=>showTab(true));

//start: the info also carries poll_ms
showTab(location.hash==='#device');
if(location.hash!=='#device')loadInfo();
poll();
</script>
</main></body></html>
)HTML";

#endif
