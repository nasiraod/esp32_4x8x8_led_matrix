#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Mobile control page, served at / when connected (and at /control always).
//
// The UI issues plain text commands to /api/cmd, so it drives exactly the same
// parser as TCP and serial - no second implementation of the control logic to
// drift out of sync. /api/status returns the same fields the `status` command
// reports, and the page polls it to stay in step with changes made from any
// other surface.
//
// Stored in PROGMEM and sent with send_P so the ~4KB never lands on the heap.
// ---------------------------------------------------------------------------

static const char CONTROL_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>LED Panel</title><style>
:root{--bg:#0f1115;--card:#181c23;--fg:#e8eaed;--mut:#9aa0a6;--acc:#2dd4a7;--line:#2a2f38}
*{box-sizing:border-box}
body{margin:0 auto;max-width:560px;background:var(--bg);color:var(--fg);
font:16px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif;padding:14px}
h1{font-size:1.15rem;margin:2px 0}
.s{color:var(--mut);font-size:.78rem;margin:2px 0 14px;word-break:break-all}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;
padding:14px;margin-bottom:12px}
.card h2{font-size:.7rem;text-transform:uppercase;letter-spacing:.09em;
color:var(--mut);margin:0 0 10px}
input[type=time]{width:100%;padding:11px;border-radius:11px;border:1px solid var(--line);background:#0c0e12;color:var(--fg);font-size:1rem;box-sizing:border-box}
input[type=text]{width:100%;padding:13px;border-radius:11px;border:1px solid var(--line);
background:#0c0e12;color:var(--fg);font-size:1rem}
small{font-size:.7rem;opacity:.75;font-weight:400}
button{appearance:none;border:1px solid var(--line);background:#222732;color:var(--fg);
border-radius:11px;padding:13px 10px;font-size:.95rem;cursor:pointer;touch-action:manipulation}
button:active{transform:scale(.97)}
button.p{background:var(--acc);color:#04231a;border-color:var(--acc);font-weight:600}
button.on{background:var(--acc);color:#04231a;border-color:var(--acc);font-weight:600}
.row{display:flex;gap:8px}.row>*{flex:1}
.lbl{display:flex;justify-content:space-between;color:var(--mut);font-size:.78rem;margin:14px 0 6px}
.lbl:first-of-type{margin-top:0}
input[type=range]{width:100%;accent-color:var(--acc)}
a{color:var(--acc);text-decoration:none}
</style></head><body>
<h1>LED Panel</h1><div class="s" id="st">connecting&hellip;</div>

<div class="card"><h2>Screen</h2><div class="row" id="screen">
<button data-sc="on" onclick="cmd('screen on')">On</button>
<button data-sc="off" onclick="cmd('screen off')">Off</button></div></div>

<div class="card"><h2>Message</h2>
<input type="text" id="msg" placeholder="Type a message" autocomplete="off"
 onkeydown="if(event.key==='Enter')send()">
<div class="row" style="margin-top:10px">
<button class="p" onclick="send()">Send</button>
<button onclick="cmd('text  ')">Clear</button></div></div>

<div class="card"><h2>Mode</h2><div class="row" id="mode">
<button data-m="text" onclick="cmd('mode text')">Text</button>
<button data-m="stopwatch" onclick="cmd('mode stopwatch')">Stopwatch</button>
<button data-m="timer" onclick="cmd('mode timer')">Timer</button>
<button data-m="clock" onclick="cmd('mode clock')">Clock</button></div></div>

<div class="card"><h2>Timer</h2>
<div class="s" id="tmr">-</div>
<input type="text" id="td" placeholder="5m, 90s, 1h30m or MM:SS" autocomplete="off"
 onkeydown="if(event.key==='Enter')startTimer()" style="margin-top:8px">
<div class="row" style="margin-top:10px">
<button class="p" onclick="startTimer()">Start</button>
<button onclick="cmd('timer toggle')">Pause</button>
<button onclick="cmd('timer reset')">Reset</button></div></div>

<div class="card"><h2>Clock style</h2>
<div class="row" id="clock" style="margin-bottom:8px">
<button data-c="hmbar" onclick="cmd('clock hmbar')">HH:MM<br><small>+ bar</small></button>
<button data-c="hmblink" onclick="cmd('clock hmblink')">HH:MM<br><small>blink</small></button>
</div><div class="row" id="clock2" style="margin-bottom:8px">
<button data-c="ms" onclick="cmd('clock ms')">MM:SS</button>
<button data-c="hms" onclick="cmd('clock hms')">HH:MM:SS<br><small>narrow</small></button>
</div>
<div class="lbl"><span>Clock digits</span></div>
<div class="row" id="cfont">
<button data-cf="stock" onclick="cmd('clock font stock')">Stock<br><small>7 row + bar</small></button>
<button data-cf="big" onclick="cmd('clock font big')">Big<br><small>8 row</small></button>
</div></div>

<div class="card"><h2>Stopwatch</h2><div class="row">
<button onclick="cmd('sw toggle')">Start / Stop</button>
<button onclick="cmd('sw reset')">Reset</button></div></div>

<div class="card"><h2>Display</h2>
<div class="lbl"><span>Alignment</span></div>
<div class="row" id="align">
<button data-a="left" onclick="cmd('align left')">Left</button>
<button data-a="center" onclick="cmd('align center')">Center</button>
<button data-a="right" onclick="cmd('align right')">Right</button></div>
<div class="lbl"><span>Text font</span></div>
<div class="row" id="font">
<button data-f="normal" onclick="cmd('font normal')">Normal<br><small>full ASCII</small></button>
<button data-f="narrow" onclick="cmd('font narrow')">Narrow<br><small>3x8 caps</small></button></div>
<div class="lbl"><span>Scrolling</span></div>
<div class="row" id="scroll">
<button data-s="auto" onclick="cmd('scroll auto')">Auto</button>
<button data-s="on" onclick="cmd('scroll on')">On</button>
<button data-s="off" onclick="cmd('scroll off')">Off</button></div>
<div class="lbl"><span>Orientation</span></div>
<div class="row" id="flip">
<button data-fl="off" onclick="cmd('flip off')">Normal</button>
<button data-fl="on" onclick="cmd('flip on')">Upside down</button></div>
<div class="lbl"><span>Brightness</span><span id="bv">-</span></div>
<input type="range" min="0" max="15" id="br"
 oninput="document.getElementById('bv').textContent=this.value"
 onchange="cmd('bright '+this.value)">
<div class="lbl"><span>Scroll speed</span><span id="spv">-</span></div>
<input type="range" min="10" max="200" step="5" id="sp"
 oninput="document.getElementById('spv').textContent=this.value+'ms'"
 onchange="cmd('speed '+this.value)">
</div>

<div class="card"><h2>Network</h2><div class="s" id="net">-</div>
<a href="/wifi">Wi-Fi settings &rarr;</a></div>

<div class="card"><h2>Sleep schedule</h2>
<div class="s" id="slp">-</div>
<div class="row" style="margin-top:8px">
<div><div class="lbl" style="margin:0 0 4px"><span>Off at</span></div>
<input type="time" id="s1" value="23:00"></div>
<div><div class="lbl" style="margin:0 0 4px"><span>On at</span></div>
<input type="time" id="s2" value="07:00"></div>
</div>
<div class="lbl"><span>Dim to (blank = screen off)</span></div>
<input type="text" id="sd" placeholder="e.g. 1" inputmode="numeric" autocomplete="off">
<div class="row" style="margin-top:10px">
<button class="p" onclick="setSleep()">Set</button>
<button onclick="cmd('sleep off')">Disable</button></div></div>

<div class="card"><h2>Firmware</h2>
<input type="file" id="fw" accept=".bin" style="width:100%;color:var(--mut);font-size:.85rem">
<div class="row" style="margin-top:10px"><button onclick="upload()">Upload &amp; reboot</button></div>
<div class="s" id="fws" style="margin-top:8px">pick firmware.bin from .pio/build/esp32dev/</div></div>

<script>
var $=function(i){return document.getElementById(i)};
function cmd(c){return fetch('/api/cmd?c='+encodeURIComponent(c)).then(function(r){
 return r.text()}).then(function(t){refresh();return t})}
function send(){var v=$('msg').value;if(v)cmd('text '+v)}
function startTimer(){
 var v=$('td').value.trim();
 if(!v){$('tmr').textContent='enter a duration';return}
 cmd('timer '+v);
}
function setSleep(){
 var a=$('s1').value,b=$('s2').value,d=$('sd').value.trim();
 if(!a||!b){$('slp').textContent='pick both times';return}
 cmd('sleep '+a+' '+b+(d?' '+d:''));
}
function seg(box,attr,val){var k=$(box).children;
 for(var i=0;i<k.length;i++)k[i].classList.toggle('on',k[i].getAttribute(attr)===val)}
function refresh(){return fetch('/api/status').then(function(r){return r.json()})
 .then(function(j){
  $('st').textContent=j.mode+' · '+j.wifi+' · '+j.ip;
  $('net').textContent='SSID '+j.ssid+' · '+j.ip+' · '+j.cols+' cols';
  $('slp').textContent='schedule: '+j.sleep;
  $('tmr').textContent='timer: '+j.timer;
  seg('mode','data-m',j.mode);seg('clock','data-c',j.clock);seg('clock2','data-c',j.clock);seg('cfont','data-cf',j.clockfont);seg('align','data-a',j.align);seg('font','data-f',j.font);seg('flip','data-fl',j.flip);seg('screen','data-sc',j.screen);seg('scroll','data-s',j.scroll);
  if(document.activeElement!==$('br')){$('br').value=j.bright;$('bv').textContent=j.bright}
  if(document.activeElement!==$('sp')){$('sp').value=j.speed;$('spv').textContent=j.speed+'ms'}
 }).catch(function(){$('st').textContent='offline'})}
function upload(){
 var f=$('fw').files[0];
 if(!f){$('fws').textContent='choose a .bin first';return}
 var fd=new FormData();fd.append('firmware',f);
 var x=new XMLHttpRequest();
 x.upload.onprogress=function(e){if(e.lengthComputable)
   $('fws').textContent='uploading '+Math.round(e.loaded/e.total*100)+'%'};
 x.onload=function(){$('fws').textContent=x.status===200
   ?'done - rebooting, reload in ~15s':'failed: '+x.responseText};
 x.onerror=function(){$('fws').textContent='upload error'};
 x.open('POST','/update');x.send(fd);
 $('fws').textContent='uploading...';
}
refresh();setInterval(refresh,2000);
</script></body></html>)HTML";
