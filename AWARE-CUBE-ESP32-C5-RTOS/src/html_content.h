// html_content.h — HTML/CSS/JS fuer das Captive-Portal (unveraendert aus legacy).
// Platzhalter %DEVICE_NAME% wird zur Laufzeit ersetzt.

#ifndef HTML_CONTENT_H
#define HTML_CONTENT_H

#include <Arduino.h>

static const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>%DEVICE_NAME% - WiFi-Einrichtung</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
background:#f0f2f5;color:#1a1a2e;min-height:100vh;display:flex;justify-content:center;
align-items:flex-start;padding:16px}
.container{width:100%;max-width:400px;margin-top:20px}
.header{text-align:center;margin-bottom:24px}
.header h1{font-size:1.4em;color:#16213e;margin-bottom:4px}
.header p{font-size:.85em;color:#666}
.card{background:#fff;border-radius:12px;padding:20px;margin-bottom:16px;
box-shadow:0 2px 8px rgba(0,0,0,.08)}
.card h2{font-size:1em;margin-bottom:12px;color:#16213e}
button{width:100%;padding:12px;border:none;border-radius:8px;font-size:.95em;
cursor:pointer;transition:background .2s,transform .1s}
button:active{transform:scale(.98)}
.btn-primary{background:#0f3460;color:#fff}
.btn-primary:hover{background:#16213e}
.btn-primary:disabled{background:#999;cursor:not-allowed;transform:none}
.btn-scan{background:#e2e8f0;color:#1a1a2e;margin-bottom:12px}
.btn-scan:hover{background:#cbd5e1}
.ssid-list{max-height:200px;overflow-y:auto;margin-bottom:12px}
.ssid-item{display:flex;justify-content:space-between;align-items:center;
padding:10px 12px;border:1px solid #e2e8f0;border-radius:8px;margin-bottom:6px;
cursor:pointer;transition:background .15s;font-size:.9em}
.ssid-item:hover{background:#f0f4ff}
.ssid-item .name{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.ssid-item .signal{font-size:.8em;color:#666;margin-left:8px;white-space:nowrap}
.lock::after{content:" ";font-size:.7em}
.input-group{margin-bottom:12px}
.input-group label{display:block;font-size:.85em;color:#444;margin-bottom:4px}
.input-group input{width:100%;padding:10px 12px;border:1px solid #d1d5db;
border-radius:8px;font-size:.95em;outline:none;transition:border-color .2s}
.input-group input:focus{border-color:#0f3460}
.pass-wrap{position:relative}
.pass-wrap input{padding-right:44px}
.pass-toggle{position:absolute;right:8px;top:50%;transform:translateY(-50%);
background:none;border:none;width:auto;padding:4px 8px;font-size:.8em;color:#0f3460;cursor:pointer}
.status{padding:12px;border-radius:8px;text-align:center;font-size:.9em;display:none}
.status.info{display:block;background:#e0f2fe;color:#0369a1}
.status.ok{display:block;background:#dcfce7;color:#166534}
.status.err{display:block;background:#fee2e2;color:#991b1b}
.spinner{display:inline-block;width:16px;height:16px;border:2px solid #0369a1;
border-top-color:transparent;border-radius:50%;animation:spin .8s linear infinite;
vertical-align:middle;margin-right:6px}
@keyframes spin{to{transform:rotate(360deg)}}
.hidden{display:none}
</style>
</head>
<body>
<div class="container">
<div class="header">
<h1>%DEVICE_NAME%</h1>
<p>WiFi-Einrichtung</p>
</div>

<div class="card">
<h2>Verfuegbare Netzwerke</h2>
<button class="btn-scan" onclick="doScan()" id="btnScan">Netzwerke scannen</button>
<div id="ssidList" class="ssid-list"></div>
</div>

<div class="card">
<h2>Verbindung herstellen</h2>
<div class="input-group">
<label for="ssid">SSID (Netzwerkname)</label>
<input type="text" id="ssid" placeholder="Netzwerkname eingeben" autocomplete="off" maxlength="32">
</div>
<div class="input-group">
<label for="pass">Passwort</label>
<div class="pass-wrap">
<input type="password" id="pass" placeholder="WiFi-Passwort" autocomplete="off" maxlength="64">
<button class="pass-toggle" onclick="togglePass()" id="btnToggle">Zeigen</button>
</div>
</div>
<button class="btn-primary" onclick="doConnect()" id="btnConnect">Verbinden</button>
</div>

<div id="status" class="status"></div>
</div>

<script>
var pollTimer=null;
function $(id){return document.getElementById(id)}
function doScan(){
  $('btnScan').disabled=true;
  $('btnScan').textContent='Scanne...';
  $('ssidList').innerHTML='';
  fetch('/scan').then(r=>r.json()).then(data=>{
    $('btnScan').disabled=false;
    $('btnScan').textContent='Netzwerke scannen';
    if(!data.length){
      $('ssidList').innerHTML='<div style="text-align:center;color:#666;padding:12px;font-size:.85em">Keine Netzwerke gefunden</div>';
      return;
    }
    var seen={},unique=[];
    data.forEach(function(n){
      if(!seen[n.ssid]||n.rssi>seen[n.ssid].rssi){seen[n.ssid]=n}
    });
    for(var k in seen)unique.push(seen[k]);
    unique.sort(function(a,b){return b.rssi-a.rssi});
    var html='';
    unique.forEach(function(n){
      var bars=n.rssi>-50?'||||':n.rssi>-65?'|||':n.rssi>-75?'||':'|';
      var lock=n.enc>0?' lock':'';
      html+='<div class="ssid-item" onclick="pickSsid(\''+n.ssid.replace(/'/g,"\\'")+'\')">';
      html+='<span class="name'+lock+'">'+escHtml(n.ssid)+'</span>';
      html+='<span class="signal">'+bars+' '+n.rssi+'dBm</span></div>';
    });
    $('ssidList').innerHTML=html;
  }).catch(function(){
    $('btnScan').disabled=false;
    $('btnScan').textContent='Netzwerke scannen';
    showStatus('err','Scan fehlgeschlagen');
  });
}
function pickSsid(s){$('ssid').value=s;$('pass').focus()}
function togglePass(){
  var p=$('pass');
  if(p.type==='password'){p.type='text';$('btnToggle').textContent='Verbergen'}
  else{p.type='password';$('btnToggle').textContent='Zeigen'}
}
function doConnect(){
  var ssid=$('ssid').value.trim();
  var pass=$('pass').value;
  if(!ssid){showStatus('err','Bitte SSID eingeben');return}
  $('btnConnect').disabled=true;
  showStatus('info','<span class="spinner"></span>Verbindungsversuch...');
  fetch('/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass)
  }).then(r=>r.json()).then(data=>{
    if(data.status==='connecting'){pollTimer=setInterval(pollStatus,2000)}
    else if(data.status==='ok'){connectOk(data.ip)}
    else{connectFail(data.reason||'Unbekannter Fehler')}
  }).catch(function(){
    $('btnConnect').disabled=false;
    showStatus('err','Verbindungsfehler - bitte erneut versuchen');
  });
}
function pollStatus(){
  fetch('/status').then(r=>r.json()).then(data=>{
    if(data.status==='connecting')return;
    clearInterval(pollTimer);pollTimer=null;
    if(data.status==='ok'){connectOk(data.ip)}else{connectFail(data.reason||'Verbindung fehlgeschlagen')}
  }).catch(function(){
    clearInterval(pollTimer);pollTimer=null;
    showStatus('ok','Geraet verbunden. Fenster kann geschlossen werden.');
    $('btnConnect').disabled=false;
  });
}
function connectOk(ip){
  showStatus('ok','Verbunden. IP: '+escHtml(ip)+'<br>AP wird abgeschaltet.');
  $('btnConnect').disabled=false;
}
function connectFail(reason){
  var msg={'wrong_password':'Falsches Passwort','timeout':'Zeitueberschreitung',
    'no_ssid':'Netzwerk nicht gefunden','busy':'Bereits aktiv'};
  showStatus('err',msg[reason]||reason);
  $('btnConnect').disabled=false;
}
function showStatus(type,html){var s=$('status');s.className='status '+type;s.innerHTML=html}
function escHtml(s){var d=document.createElement('div');d.textContent=s;return d.innerHTML}
</script>
</body>
</html>
)rawliteral";

#endif // HTML_CONTENT_H
