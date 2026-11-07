/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: Wi-Fi setup page served in AP mode, embedded as a PROGMEM string
**************************************************************************/

/*
    One self-contained page (inline CSS and JS, no external files or CDNs,
    since the client has no internet while on the AP). Served by Provisioning
    (comms/provisioning.h) only while in AP mode.
        - on load it fetches /scan and lists the networks (SSID, signal, lock),
          polling while a scan runs. Refresh starts a new scan (POST /scan).
          a hidden SSID can be typed in
        - Save POSTs {ssid, password} as JSON to /save, then explains that the
          device restarts and the page will lose its connection
        - SSIDs come from anyone nearby, so they are only ever inserted with
          textContent, never innerHTML
*/

#ifndef PROVISIONING_HTML_H
#define PROVISIONING_HTML_H

#include <Arduino.h>
#include "../config.h"

static const char PROVISIONING_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Function Generator Wi-Fi</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1c1f24;--dim:#6b7280;--line:#dde1e6;--acc:#0b6bcb;--err:#b42318;--ok:#067647}
@media (prefers-color-scheme:dark){:root{--bg:#121417;--card:#1c1f24;--fg:#e8eaed;--dim:#9aa0a6;--line:#30353c;--acc:#5ea8f2;--err:#f97066;--ok:#47cd89}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:420px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:8px 0 4px}
p{margin:0 0 12px;color:var(--dim)}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px;margin-bottom:12px}
.row{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:8px}
.row h2{font-size:15px;margin:0}
#nets{display:flex;flex-direction:column;gap:4px}
.net{display:flex;justify-content:space-between;gap:8px;width:100%;padding:10px;border:1px solid var(--line);border-radius:8px;background:none;color:inherit;font:inherit;text-align:left;cursor:pointer}
.net.sel{border-color:var(--acc);outline:1px solid var(--acc)}
.name{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.meta{color:var(--dim);white-space:nowrap;font-size:14px}
label{display:block;font-size:14px;color:var(--dim);margin:8px 0 4px}
input[type=text],input[type=password]{width:100%;padding:10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:inherit;font:inherit}
.check{display:flex;align-items:center;gap:6px;margin-top:6px}
button.main{width:100%;margin-top:12px;padding:12px;border:0;border-radius:8px;background:var(--acc);color:#fff;font:inherit;font-weight:600;cursor:pointer}
button.small{padding:6px 10px;border:1px solid var(--line);border-radius:8px;background:none;color:inherit;font:inherit;font-size:14px;cursor:pointer}
button:disabled{opacity:.6;cursor:default}
#msg{min-height:1.4em;margin-top:8px;font-size:14px}
.err{color:var(--err)}
.ok{color:var(--ok)}
.hint{color:var(--dim);font-size:14px}
</style></head><body><main>
<h1>Function Generator Wi-Fi</h1>
<p>Pick the network the generator should join.</p>

<div class="card" id="pick">
<div class="row"><h2>Networks</h2><button class="small" id="refresh" type="button">Refresh</button></div>
<div id="nets"></div>
<div class="hint" id="scanmsg">Scanning...</div>
</div>

<form class="card" id="form" autocomplete="off">
<label for="ssid">Network name (type it for a hidden network)</label>
<input type="text" id="ssid" maxlength="32" autocapitalize="none" spellcheck="false" required>
<label for="pass">Password</label>
<input type="password" id="pass" maxlength="64">
<div class="check"><input type="checkbox" id="show"><label for="show" style="margin:0">Show password</label></div>
<div class="hint">Leave it empty for an open network.</div>
<button class="main" id="save" type="submit">Save and connect</button>
<div id="msg"></div>
</form>

<div class="card" id="done" hidden>
<h2 class="ok" style="font-size:16px;margin:0 0 8px">Saved</h2>
<p>The generator is restarting and joining <b id="donessid"></b>. This page will stop responding and your device will drop off this setup network.</p>
<p>Once it has joined, its screen shows its address, and AI agents can reach it at <b>http://)HTML" MDNS_HOSTNAME R"HTML(.local/mcp</b>.</p>
<p>If it can't join, this setup network comes back in about 30 seconds.</p>
</div>

<script>
var $=function(id){return document.getElementById(id)};
var timer=null;

function bars(r){return r>-55?'▂▄▆█':r>-67?'▂▄▆':r>-78?'▂▄':'▂'}

function select(btn,n){
  var all=document.querySelectorAll('.net');
  for(var i=0;i<all.length;i++)all[i].classList.remove('sel');
  btn.classList.add('sel');
  $('ssid').value=n.ssid;
  if(!n.secured)$('pass').value='';
  $('pass').focus();
}

function render(list){
  var box=$('nets');
  box.replaceChildren();
  list.forEach(function(n){
    var b=document.createElement('button');
    b.type='button';b.className='net';
    var name=document.createElement('span');name.className='name';name.textContent=n.ssid;
    var meta=document.createElement('span');meta.className='meta';
    meta.textContent=(n.secured?'🔒 ':'open ')+bars(n.rssi);
    meta.title=n.rssi+' dBm';
    b.appendChild(name);b.appendChild(meta);
    if(n.ssid===$('ssid').value)b.classList.add('sel');
    b.onclick=function(){select(b,n)};
    box.appendChild(b);
  });
}

function scan(fresh){
  clearTimeout(timer);
  $('scanmsg').textContent='Scanning...';
  $('refresh').disabled=true;
  fetch('/scan',{method:fresh?'POST':'GET'}).then(function(r){return r.json()}).then(function(d){
    render(d.networks||[]);
    if(d.scanning){timer=setTimeout(function(){scan(false)},1500);return}
    $('refresh').disabled=false;
    $('scanmsg').textContent=d.networks&&d.networks.length?'':'No networks found.';
  }).catch(function(){
    $('refresh').disabled=false;
    $('scanmsg').textContent='Could not reach the generator.';
  });
}

function passOk(p){return p.length===0||(p.length>=8&&p.length<=63)||/^[0-9a-fA-F]{64}$/.test(p)}

function msg(text,cls){var m=$('msg');m.textContent=text;m.className=cls||''}

$('refresh').onclick=function(){scan(true)};
$('show').onchange=function(){$('pass').type=this.checked?'text':'password'};

$('form').onsubmit=function(e){
  e.preventDefault();
  var ssid=$('ssid').value,pass=$('pass').value;
  if(!ssid){msg('Enter a network name.','err');return}
  if(new TextEncoder().encode(ssid).length>32){msg('The network name is longer than 32 bytes.','err');return}
  if(!passOk(pass)){msg('The password must be 8-63 characters (or empty for an open network).','err');return}
  $('save').disabled=true;msg('Saving...');
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ssid,password:pass})})
  .then(function(r){return r.json().then(function(d){return{ok:r.ok,d:d}})})
  .then(function(res){
    if(!res.ok){$('save').disabled=false;msg(res.d.error||'Saving failed.','err');return}
    clearTimeout(timer);
    $('donessid').textContent=ssid;
    $('pick').hidden=true;$('form').hidden=true;$('done').hidden=false;
  }).catch(function(){$('save').disabled=false;msg('Could not reach the generator.','err')});
};

scan(false);
</script>
</main></body></html>
)HTML";

#endif
