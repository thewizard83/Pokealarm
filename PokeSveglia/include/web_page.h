#pragma once
// Pagina web della sveglia (servita dall'ESP32, funziona anche senza internet:
// il convertitore di GIF è scritto qui sotto, senza librerie esterne).
static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PokeSveglia</title>
<style>
:root{--red:#e3350d;--dark:#1d1d2b;--card:#fff;--bg:#f2f2f7;--txt:#1d1d2b;--mute:#6b6b80;--yel:#ffcb05;--blue:#3b4cca;--ok:#2e9e5b}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--txt)}
header{background:var(--red);color:#fff;padding:14px 16px 0;position:sticky;top:0;z-index:5;box-shadow:0 2px 8px #0003}
.brand{display:flex;align-items:center;gap:10px;font-weight:800;font-size:20px}
.ball{width:26px;height:26px;border-radius:50%;border:3px solid #222;background:linear-gradient(#fff 0 0) bottom/100% 50% no-repeat,var(--red);position:relative;background-color:var(--red)}
.ball:before{content:"";position:absolute;left:-3px;right:-3px;top:8px;height:4px;background:#222}
.ball:after{content:"";position:absolute;left:6px;top:5px;width:8px;height:8px;border-radius:50%;background:#fff;border:3px solid #222}
.sub{font-size:13px;opacity:.9;margin:4px 0 10px}
nav{display:flex;gap:4px;overflow-x:auto}
nav button{flex:1;border:0;background:#0000;color:#fff;font-weight:700;padding:10px 3px;border-bottom:4px solid transparent;font-size:13px;cursor:pointer;white-space:nowrap}
nav button.on{border-color:var(--yel)}
main{max-width:680px;margin:0 auto;padding:16px}
section{display:none}section.on{display:block}
.card{background:var(--card);border-radius:14px;padding:14px;margin-bottom:12px;box-shadow:0 1px 3px #0001}
h2{font-size:17px;margin:4px 0 12px}
.row{display:flex;align-items:center;gap:10px;flex-wrap:wrap}
.grow{flex:1}
input[type=time]{font-size:26px;font-weight:700;border:0;background:#0000;color:var(--txt);padding:0;width:auto}
input[type=text],input[type=password],input[type=number],select{font-size:16px;padding:9px 10px;border:1px solid #ccd;border-radius:9px;width:100%;background:#fff;color:var(--txt)}
label.f{display:block;font-size:13px;color:var(--mute);margin:10px 0 4px}
.days{display:flex;gap:5px;margin-top:10px}
.days button{width:36px;height:36px;border-radius:50%;border:1px solid #ccd;background:#fff;font-weight:700;color:var(--mute);cursor:pointer}
.days button.on{background:var(--blue);border-color:var(--blue);color:#fff}
.sw{position:relative;width:52px;height:30px;flex:none}.sw input{display:none}
.sw span{position:absolute;inset:0;background:#ccd;border-radius:20px;transition:.2s;cursor:pointer}
.sw span:before{content:"";position:absolute;width:24px;height:24px;left:3px;top:3px;border-radius:50%;background:#fff;transition:.2s;box-shadow:0 1px 3px #0004}
.sw input:checked+span{background:var(--ok)}.sw input:checked+span:before{transform:translateX(22px)}
.btn{border:0;border-radius:10px;padding:11px 16px;font-weight:700;font-size:15px;cursor:pointer;background:var(--blue);color:#fff}
.btn.red{background:var(--red)}.btn.gray{background:#dde;color:var(--txt)}.btn.y{background:var(--yel);color:#222}
.btn:disabled{opacity:.5}
.bar{display:flex;gap:8px;flex-wrap:wrap;margin-top:6px}
.muted{color:var(--mute);font-size:13px}
.gifs{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:10px}
.g{background:#fff;border-radius:14px;padding:8px;text-align:center;border:3px solid transparent}
.g.cur{border-color:var(--yel)}
.g img{width:110px;height:110px;border-radius:50%;background:#000;object-fit:cover;display:block;margin:4px auto 6px}
.g .n{font-size:13px;font-weight:700;word-break:break-all}
.g .bar{justify-content:center}.g .btn{padding:7px 10px;font-size:13px}
.prev{width:180px;height:180px;border-radius:50%;background:#000;display:block;margin:10px auto;object-fit:cover}
progress{width:100%;height:14px}
.net{padding:10px;border-radius:9px;border:1px solid #dde;margin:6px 0;cursor:pointer;display:flex;justify-content:space-between}
.net:hover{background:#f4f4fb}
#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:#222;color:#fff;padding:10px 16px;border-radius:10px;opacity:0;transition:.3s;pointer-events:none;z-index:9}
#toast.on{opacity:1}
input[type=range]{width:100%}
textarea{font-size:16px;padding:10px;border:1px solid #ccd;border-radius:9px;width:100%;font-family:inherit;resize:vertical;background:#fff;color:var(--txt)}
.hist{padding:9px 0;border-bottom:1px solid #8883;cursor:pointer}.hist:last-child{border:0}.hist small{color:var(--mute)}
.lprev{position:relative;width:200px;height:200px;border-radius:50%;overflow:hidden;background:#000;margin:4px auto 8px;box-shadow:0 0 0 4px #222}
.lprev img{position:absolute;left:0;top:0;width:200px;height:200px;image-rendering:pixelated;transform-origin:50% 50%}
.lpill{position:absolute;left:33px;top:17px;width:134px;height:23px;border-radius:12px;background:#0008;outline:1.5px dashed #fffa}
.lband{position:absolute;left:25px;top:130px;width:150px;height:65px;border-radius:17px;background:#0008;outline:1.5px dashed #fffa}
.pad{display:flex;flex-wrap:wrap;gap:6px;justify-content:center;margin-top:10px}.pad .btn{min-width:46px;padding:9px 12px}
.filebtn{display:inline-block;position:relative;overflow:hidden;margin:4px 8px 4px 0}
.filebtn input{position:absolute;inset:0;opacity:0;cursor:pointer;font-size:100px}
.warn{background:#fff3cd;color:#5c4400;border-radius:10px;padding:10px 12px;margin-bottom:10px;font-size:14px;line-height:1.4}
@media (prefers-color-scheme:dark){:root{--card:#262636;--bg:#15151f;--txt:#eee;--mute:#a0a0b8}
input[type=text],input[type=password],input[type=number],select,textarea,.days button{background:#1d1d2b;border-color:#445;color:var(--txt)}
.g{background:var(--card)}.btn.gray{background:#3a3a50;color:#eee}.net{border-color:#445}.net:hover{background:#2e2e44}}
</style></head><body>
<header><div class="brand"><div class="ball"></div>PokeSveglia</div>
<div class="sub" id="status">Caricamento…</div>
<nav><button data-t="alarms" class="on">Sveglie</button><button data-t="msg">Messaggi</button><button data-t="gif">Sfondo</button><button data-t="settings">Impostazioni</button><button data-t="wifi">Wi-Fi</button></nav></header>
<main>
<section id="alarms" class="on">
 <div id="alarmList"></div>
 <div class="bar"><button class="btn" onclick="saveAlarms()">Salva sveglie</button>
 <button class="btn y" onclick="post('/api/test').then(()=>toast('Suona per 10 secondi…'))">Prova suono</button>
 <button class="btn gray" onclick="post('/api/stop')">Ferma</button></div>
</section>

<section id="msg">
 <div class="card"><h2>Invia un messaggio</h2>
  <textarea id="mtext" maxlength="600" rows="4" placeholder="Scrivi qui: la sveglia lo mostra e lo legge ad alta voce"></textarea>
  <div class="row" style="justify-content:space-between"><span class="muted" id="mcount">0/600</span><span class="muted" id="mstat"></span></div>
  <div class="bar"><button class="btn" id="msend" onclick="sendMsg()">📣 Invia e leggi</button>
  <button class="btn gray" onclick="post('/api/message/stop')">Interrompi</button></div>
  <p class="muted">La voce richiede internet (servizio di sintesi vocale di Google). Senza connessione il messaggio viene solo mostrato.</p>
 </div>
 <div class="card"><h2>Ultimi messaggi</h2><div id="mhist" class="muted">Nessun messaggio.</div></div>
</section>

<section id="gif">
 <div class="card"><h2>GIF in memoria</h2><div class="gifs" id="gifList"></div>
 <p class="muted" id="fsInfo"></p></div>
 <div class="card"><h2>Posizione e dimensione</h2>
  <div class="lprev"><img id="lpimg" alt=""><div class="lpill"></div><div class="lband"></div></div>
  <p class="muted" style="text-align:center">Le zone tratteggiate indicano dove compaiono ora e sveglia.</p>
  <label class="f">Dimensione: <b id="lsV"></b>%</label><input type="range" id="ls" min="40" max="300" step="5">
  <label class="f">Velocità: <b id="lvV"></b>%</label><input type="range" id="lv" min="25" max="400" step="5">
  <label class="f">Orizzontale: <b id="lxV"></b></label><input type="range" id="lx" min="-120" max="120">
  <label class="f">Verticale: <b id="lyV"></b></label><input type="range" id="ly" min="-120" max="120">
  <div class="pad">
   <button class="btn gray" onclick="nudge(0,-5,0)">▲</button>
   <button class="btn gray" onclick="nudge(-5,0,0)">◀</button><button class="btn gray" onclick="nudge(5,0,0)">▶</button>
   <button class="btn gray" onclick="nudge(0,5,0)">▼</button>
   <button class="btn gray" onclick="nudge(0,0,-5)">－</button><button class="btn gray" onclick="nudge(0,0,5)">＋</button>
   <button class="btn y" onclick="setLay(0,0,100,100)">Ripristina</button>
  </div>
  <p class="muted">Le modifiche si vedono subito sulla sveglia e vengono salvate per questa GIF.</p>
 </div>
 <div class="card"><h2>Carica una nuova GIF</h2>
  <div id="cnaWarn" class="warn" style="display:none"><b>Da questa finestra non puoi scegliere file.</b><br>
  Sei nella finestrina di accesso alla rete Wi-Fi, che blocca la selezione dei file. Chiudila scegliendo
  <i>Annulla → Usa senza Internet</i> (o simile), poi apri <b>Safari/Chrome</b> e vai su
  <b class="addr"></b></div>
  <label class="btn gray filebtn">📁 Scegli una GIF…<input type="file" id="gfile" accept="image/gif,.gif"></label>
  <span class="muted" id="gfname"></span>
  <p class="muted">Se toccando il pulsante non si apre nulla, apri questa pagina nel browser (Safari o Chrome) all'indirizzo <b class="addr"></b></p>
  <div class="row"><div class="grow"><label class="f">Adattamento</label><select id="gfit"><option value="contain">Intera (con bordi)</option><option value="cover">Riempi lo schermo</option></select></div>
  <div class="grow"><label class="f">Sfondo</label><select id="gbg"><option value="auto">Automatico</option><option value="#000000">Nero</option><option value="#ffffff">Bianco</option></select></div></div>
  <label class="f"><input type="checkbox" id="gpix" checked> Pixel art (bordi nitidi, ideale per gli sprite Pokémon)</label>
  <label class="f">Nome</label><input type="text" id="gname" placeholder="es. pikachu">
  <img id="gprev" class="prev" alt="" style="display:none">
  <p class="muted" id="ginfo"></p>
  <progress id="gprog" value="0" max="100" style="display:none"></progress>
  <div class="bar"><button class="btn" id="gup" disabled onclick="uploadGif()">Carica sulla sveglia</button></div>
 </div>
</section>

<section id="settings">
 <div class="card"><h2>Suono</h2>
  <label class="f">Volume: <b id="volV"></b>%</label><input type="range" id="volume" min="5" max="100" oninput="volV.textContent=this.value">
  <div class="row"><div class="grow"><label class="f">Posticipa di (minuti)</label><input type="number" id="snoozeMin" min="1" max="30"></div>
  <div class="grow"><label class="f">Spegni da sola dopo (minuti)</label><input type="number" id="ringMaxMin" min="1" max="60"></div></div>
 </div>
 <div class="card"><h2>Schermo</h2>
  <label class="f">Luminosità giorno: <b id="bdV"></b>%</label><input type="range" id="brightDay" min="5" max="100" oninput="bdV.textContent=this.value">
  <label class="f">Luminosità notte: <b id="bnV"></b>%</label><input type="range" id="brightNight" min="0" max="100" oninput="bnV.textContent=this.value">
  <div class="row"><div class="grow"><label class="f">Notte dalle ore</label><input type="number" id="nightStart" min="0" max="23"></div>
  <div class="grow"><label class="f">alle ore</label><input type="number" id="nightEnd" min="0" max="23"></div></div>
  <p class="muted">Durante la notte basta toccare lo schermo per riaccenderlo per qualche secondo.</p>
 </div>
 <div class="card"><h2>Meteo</h2>
  <label class="f">Città (lascia vuoto per rilevarla automaticamente dalla connessione)</label>
  <input type="text" id="city" placeholder="automatica">
  <p class="muted" id="wInfo"></p>
 </div>
 <div class="bar"><button class="btn" onclick="saveSettings()">Salva impostazioni</button></div>
</section>

<section id="wifi">
 <div class="card"><h2>Rete Wi-Fi</h2><p class="muted" id="wifiNow"></p>
  <div class="bar"><button class="btn gray" onclick="scan()">Cerca reti</button></div>
  <div id="nets"></div>
  <label class="f">Nome rete (SSID)</label><input type="text" id="ssid" autocomplete="off">
  <label class="f">Password</label><input type="password" id="pass">
  <div class="bar"><button class="btn" onclick="saveWifi()">Connetti e riavvia</button></div>
 </div>
</section>
</main>
<div id="toast"></div>
<script>
const $=id=>document.getElementById(id);
let S=null;const DAYS=['L','M','M','G','V','S','D'];
function toast(t){const e=$('toast');e.textContent=t;e.classList.add('on');clearTimeout(e._t);e._t=setTimeout(()=>e.classList.remove('on'),2500)}
function post(u,body){return fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:body?JSON.stringify(body):'{}'}).then(r=>{if(!r.ok)throw new Error(r.status);return r})}
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{document.querySelectorAll('nav button,section').forEach(x=>x.classList.remove('on'));b.classList.add('on');$(b.dataset.t).classList.add('on')});
const pad=n=>String(n).padStart(2,'0');
async function load(){
 S=await (await fetch('/api/state')).json();
 const w=S.wifi;
 $('status').textContent=(S.time?'Ora: '+S.time+(S.timeSrc===3?' (dal telefono)':'')+' · ':'')+(w.connected?'Connessa a '+w.ssid+' ('+w.ip+')':'Non connessa a internet')+(w.portal?' · rete di configurazione attiva':'')+batTxt(S.battery);
 $('wifiNow').textContent=w.connected?'Connessa a "'+w.ssid+'" — indirizzo http://'+w.ip+'  (oppure http://pokesveglia.local)':'La sveglia non è connessa a nessuna rete.';
 if(!$('ssid').value)$('ssid').value=w.ssid||'';
 renderAlarms();renderGifs();renderLay();renderMsgs(S);
 const st=S.settings;for(const k of ['volume','snoozeMin','ringMaxMin','brightDay','brightNight','nightStart','nightEnd','city'])$(k).value=st[k];
 volV.textContent=st.volume;bdV.textContent=st.brightDay;bnV.textContent=st.brightNight;
 $('wInfo').textContent=S.weather.valid?'Meteo attuale per: '+S.weather.city:'Meteo non ancora disponibile.';
}
function batTxt(b){if(!b)return'';return' · '+(b.charging?'⚡ in carica '+b.level+'%':b.usb?'🔌 alimentata da USB':'🔋 '+b.level+'%')}
function renderAlarms(){
 const L=$('alarmList');L.innerHTML='';
 S.alarms.forEach((a,i)=>{
  const c=document.createElement('div');c.className='card';
  c.innerHTML=`<div class="row"><input type="time" value="${pad(a.h)}:${pad(a.m)}" data-k="t">
  <input type="text" class="grow" style="width:auto" maxlength="20" placeholder="Etichetta (facoltativa)" value="${(a.label||'').replace(/"/g,'&quot;')}" data-k="l">
  <label class="sw"><input type="checkbox" ${a.en?'checked':''} data-k="e"><span></span></label></div>
  <div class="days">${DAYS.map((d,j)=>`<button class="${a.days>>j&1?'on':''}" data-j="${j}">${d}</button>`).join('')}</div>
  <div class="muted" style="margin-top:6px">${a.days?'':'Nessun giorno selezionato: suona una volta sola'}</div>`;
  c.querySelectorAll('.days button').forEach(b=>b.onclick=()=>{a.days^=1<<b.dataset.j;b.classList.toggle('on');c.querySelector('.muted').textContent=a.days?'':'Nessun giorno selezionato: suona una volta sola'});
  c.querySelector('[data-k=t]').onchange=e=>{const[h,m]=e.target.value.split(':');a.h=+h;a.m=+m};
  c.querySelector('[data-k=l]').oninput=e=>a.label=e.target.value;
  c.querySelector('[data-k=e]').onchange=e=>a.en=e.target.checked;
  L.appendChild(c);
 });
}
function saveAlarms(){post('/api/alarms',S.alarms).then(()=>toast('Sveglie salvate ✓')).catch(()=>toast('Errore nel salvataggio'))}
function saveSettings(){const st={};for(const k of ['volume','snoozeMin','ringMaxMin','brightDay','brightNight','nightStart','nightEnd'])st[k]=+$(k).value;st.city=$('city').value.trim();
 post('/api/settings',st).then(()=>{toast('Impostazioni salvate ✓');setTimeout(load,4000)}).catch(()=>toast('Errore'))}
function renderGifs(){
 const L=$('gifList');L.innerHTML='';
 S.gifs.forEach(g=>{const d=document.createElement('div');d.className='g'+(g.name===S.settings.gif?' cur':'');
  d.innerHTML=`<img loading="lazy" src="/gifs/${encodeURIComponent(g.name)}"><div class="n">${g.name.replace(/\.gif$/,'')}</div><div class="muted">${(g.size/1024).toFixed(0)} KB</div>
  <div class="bar">${g.name===S.settings.gif?'<b>In uso</b>':`<button class="btn" data-a="use">Usa</button><button class="btn red" data-a="del">✕</button>`}</div>`;
  const u=d.querySelector('[data-a=use]');if(u)u.onclick=()=>post('/api/gif/select?name='+encodeURIComponent(g.name)).then(()=>{toast('Sfondo cambiato ✓');setTimeout(load,500)});
  const x=d.querySelector('[data-a=del]');if(x)x.onclick=()=>{if(confirm('Eliminare '+g.name+'?'))post('/api/gif/delete?name='+encodeURIComponent(g.name)).then(load)};
  L.appendChild(d)});
 $('fsInfo').textContent=`Memoria usata: ${(S.fs.used/1048576).toFixed(1)} MB su ${(S.fs.total/1048576).toFixed(1)} MB`;
}
$('mtext').oninput=()=>$('mcount').textContent=$('mtext').value.length+'/600';
const SPST={idle:'',download:'Preparo la voce…',voce:'Sto leggendo…'};
function renderMsgs(st){
 $('mstat').textContent=SPST[st.speech]??st.speech;
 const h=$('mhist');if(!st.messages||!st.messages.length){h.textContent='Nessun messaggio.';return}
 h.innerHTML='';st.messages.forEach(m=>{const d=document.createElement('div');d.className='hist';
  const t=m.at>1700000000?new Date(m.at*1000).toLocaleString('it-IT',{day:'2-digit',month:'2-digit',hour:'2-digit',minute:'2-digit'}):'';
  d.innerHTML=`<small>${t} · tocca per reinviare</small><div></div>`;d.lastChild.textContent=m.text;
  d.onclick=()=>{$('mtext').value=m.text;$('mtext').oninput();window.scrollTo(0,0)};h.appendChild(d)})}
let msgPoll=null;
function sendMsg(){const t=$('mtext').value.trim();if(!t)return toast('Scrivi un messaggio');
 $('msend').disabled=true;
 fetch('/api/message',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({text:t})})
 .then(async r=>{if(!r.ok)throw new Error(await r.text());toast('Messaggio inviato ✓');$('mtext').value='';$('mtext').oninput();
  clearInterval(msgPoll);let n=0;msgPoll=setInterval(()=>fetch('/api/state').then(r=>r.json()).then(st=>{renderMsgs(st);if(++n>40||(n>3&&st.speech==='idle'))clearInterval(msgPoll)}),1500)})
 .catch(e=>toast('Errore: '+e.message)).finally(()=>$('msend').disabled=false)}
let L={x:0,y:0,s:100},layTimer=null;
function renderLay(){L={v:100,...S.layout};$('lpimg').src='/gifs/'+encodeURIComponent(S.settings.gif);showLay()}
function showLay(){$('lx').value=L.x;$('ly').value=L.y;$('ls').value=L.s;$('lv').value=L.v;$('lvV').textContent=L.v;$('lxV').textContent=L.x;$('lyV').textContent=L.y;$('lsV').textContent=L.s;
 const k=200/240;$('lpimg').style.transform=`translate(${L.x*k}px,${L.y*k}px) scale(${L.s/100})`}
function setLay(x,y,s,v=L.v){L={x:Math.max(-120,Math.min(120,x)),y:Math.max(-120,Math.min(120,y)),s:Math.max(40,Math.min(300,s)),v:Math.max(25,Math.min(400,v))};showLay();
 clearTimeout(layTimer);layTimer=setTimeout(()=>post('/api/gif/layout',L).catch(()=>toast('Errore di rete')),120)}
function nudge(dx,dy,ds){setLay(L.x+dx,L.y+dy,L.s+ds)}
['lx','ly','ls','lv'].forEach(id=>$(id).oninput=()=>setLay(+$('lx').value,+$('ly').value,+$('ls').value,+$('lv').value));
async function scan(){$('nets').innerHTML='<p class="muted">Ricerca in corso…</p>';
 const r=await (await fetch('/api/wifi/scan')).json();
 $('nets').innerHTML='';r.forEach(n=>{const d=document.createElement('div');d.className='net';d.innerHTML=`<span>${n.ssid.replace(/</g,'&lt;')}</span><span class="muted">${n.rssi} dBm ${n.open?'':'🔒'}</span>`;d.onclick=()=>{$('ssid').value=n.ssid;$('pass').focus()};$('nets').appendChild(d)})}
function saveWifi(){if(!$('ssid').value)return toast('Inserisci il nome della rete');
 post('/api/wifi',{ssid:$('ssid').value,pass:$('pass').value}).then(()=>toast('Salvato. La sveglia si riavvia e si connette…'))}

/* ================= Convertitore GIF (240x240) ================= */
const SZ=240;let gifBlob=null;
function lzwDecode(minCode,data,npix){
 const out=new Uint8Array(npix),clear=1<<minCode,eoi=clear+1;
 let size=minCode+1,mask=(1<<size)-1,next=eoi+1;
 const prefix=new Int16Array(4096),suffix=new Uint8Array(4096),stack=new Uint8Array(4097);
 for(let i=0;i<clear;i++){prefix[i]=-1;suffix[i]=i}
 let bits=0,cur=0,bp=0,prev=-1,first=0,op=0;
 while(op<npix){
  while(bits<size){if(bp>=data.length)return out;cur|=data[bp++]<<bits;bits+=8}
  const code=cur&mask;cur>>>=size;bits-=size;
  if(code===clear){size=minCode+1;mask=(1<<size)-1;next=eoi+1;prev=-1;continue}
  if(code===eoi)break;
  if(prev===-1){out[op++]=suffix[code];prev=code;first=suffix[code];continue}
  let c=code,sp=0;
  if(code>=next){stack[sp++]=first;c=prev}
  while(c>=clear){stack[sp++]=suffix[c];c=prefix[c]}
  stack[sp++]=suffix[c];first=suffix[c];
  while(sp>0&&op<npix)out[op++]=stack[--sp];
  if(next<4096){prefix[next]=prev;suffix[next]=first;next++;if(next===mask+1&&size<12){size++;mask=(1<<size)-1}}
  prev=code;
 }
 return out;
}
function parseGif(buf){
 const d=new Uint8Array(buf);let p=0;const u16=()=>{const v=d[p]|d[p+1]<<8;p+=2;return v};
 if(d[0]!==71||d[1]!==73||d[2]!==70)throw new Error('Il file non è una GIF');
 p=6;const W=u16(),H=u16(),f=d[p++];p+=2;let gct=null;
 if(f&0x80){const n=2<<(f&7);gct=d.subarray(p,p+n*3);p+=n*3}
 const frames=[];let gce={delay:100,trans:-1,disp:0};
 const skipBlocks=()=>{for(;;){const s=d[p++];if(!s)break;p+=s}};
 while(p<d.length){const b=d[p++];
  if(b===0x3B)break;
  if(b===0x21){const lab=d[p++];
   if(lab===0xF9){p++;const pf=d[p++];const dl=u16()*10;const ti=d[p++];p++;gce={delay:dl<20?100:dl,trans:pf&1?ti:-1,disp:pf>>2&7}}
   else skipBlocks();
  }else if(b===0x2C){const x=u16(),y=u16(),w=u16(),h=u16(),fl=d[p++];let ct=gct;
   if(fl&0x80){const n=2<<(fl&7);ct=d.subarray(p,p+n*3);p+=n*3}
   const mc=d[p++];const parts=[];let len=0;
   for(;;){const s=d[p++];if(!s)break;parts.push(d.subarray(p,p+s));len+=s;p+=s}
   const data=new Uint8Array(len);let o=0;for(const c of parts){data.set(c,o);o+=c.length}
   frames.push({x,y,w,h,ct:ct||new Uint8Array(768).map((_,i)=>Math.floor(i/3)),il:!!(fl&0x40),idx:lzwDecode(mc,data,w*h),...gce});
   gce={delay:100,trans:-1,disp:0};
  }else break;
 }
 if(!frames.length)throw new Error('GIF senza fotogrammi');
 return {W,H,frames};
}
function compose(g,fit,pixel,bgSel){
 const {W,H}=g,full=new Uint8ClampedArray(W*H*4);
 const tmp=document.createElement('canvas');tmp.width=W;tmp.height=H;const tctx=tmp.getContext('2d');
 const out=document.createElement('canvas');out.width=out.height=SZ;const octx=out.getContext('2d',{willReadFrequently:true});
 const sc=(fit==='cover'?Math.max:Math.min)(SZ/W,SZ/H),dw=Math.round(W*sc),dh=Math.round(H*sc),dx=(SZ-dw)>>1,dy=(SZ-dh)>>1;
 let bg=null;const res=[];
 for(const fr of g.frames){
  const snap=fr.disp===3?full.slice():null;
  const order=[];if(fr.il){for(const[s,st]of[[0,8],[4,8],[2,4],[1,2]])for(let r=s;r<fr.h;r+=st)order.push(r)}
  for(let k=0;k<fr.h;k++){const y=fr.y+(fr.il?order[k]:k);if(y>=H)continue;
   for(let i=0;i<fr.w;i++){const x=fr.x+i;if(x>=W)continue;const c=fr.idx[k*fr.w+i];if(c===fr.trans)continue;
    const o=(y*W+x)*4;full[o]=fr.ct[c*3];full[o+1]=fr.ct[c*3+1];full[o+2]=fr.ct[c*3+2];full[o+3]=255}}
  if(!bg)bg=bgSel!=='auto'?bgSel:(full[3]?`rgb(${full[0]},${full[1]},${full[2]})`:'#000');
  tctx.putImageData(new ImageData(full.slice(),W,H),0,0);
  octx.imageSmoothingEnabled=!pixel;octx.imageSmoothingQuality='high';
  octx.fillStyle=bg;octx.fillRect(0,0,SZ,SZ);octx.drawImage(tmp,dx,dy,dw,dh);
  res.push({rgba:octx.getImageData(0,0,SZ,SZ).data,delay:fr.delay});
  if(fr.disp===2){for(let y=fr.y;y<Math.min(H,fr.y+fr.h);y++)full.fill(0,(y*W+fr.x)*4,(y*W+Math.min(W,fr.x+fr.w))*4)}
  else if(snap)full.set(snap);
 }
 return res;
}
function quantize(frames){
 const cnt=new Uint32Array(32768),sr=new Float64Array(32768),sg=new Float64Array(32768),sb=new Float64Array(32768);
 for(const f of frames){const a=f.rgba;for(let i=0;i<a.length;i+=4){const k=(a[i]>>3)<<10|(a[i+1]>>3)<<5|a[i+2]>>3;cnt[k]++;sr[k]+=a[i];sg[k]+=a[i+1];sb[k]+=a[i+2]}}
 const used=[];for(let k=0;k<32768;k++)if(cnt[k])used.push(k);
 used.sort((a,b)=>cnt[b]-cnt[a]);const top=used.slice(0,256);
 const pal=new Uint8Array(768);top.forEach((k,i)=>{pal[i*3]=sr[k]/cnt[k];pal[i*3+1]=sg[k]/cnt[k];pal[i*3+2]=sb[k]/cnt[k]});
 const map=new Int16Array(32768).fill(-1);top.forEach((k,i)=>map[k]=i);
 const near=k=>{const r=(k>>10)<<3|4,g=(k>>5&31)<<3|4,b=(k&31)<<3|4;let best=0,bd=1e9;
  for(let i=0;i<top.length;i++){const dr=pal[i*3]-r,dg=pal[i*3+1]-g,db=pal[i*3+2]-b,dd=dr*dr*2+dg*dg*4+db*db*3;if(dd<bd){bd=dd;best=i}}return best};
 const idx=frames.map(f=>{const a=f.rgba,o=new Uint8Array(SZ*SZ);for(let i=0,j=0;i<a.length;i+=4,j++){const k=(a[i]>>3)<<10|(a[i+1]>>3)<<5|a[i+2]>>3;let m=map[k];if(m<0)m=map[k]=near(k);o[j]=m}return o});
 return {pal,idx};
}
class W8{constructor(){this.b=new Uint8Array(1<<20);this.n=0}
 put(v){if(this.n>=this.b.length){const nb=new Uint8Array(this.b.length*2);nb.set(this.b);this.b=nb}this.b[this.n++]=v}
 u16(v){this.put(v&255);this.put(v>>8&255)}bytes(a){for(const v of a)this.put(v)}}
function lzwEncode(w,idx){
 const mc=8,clear=256,eoi=257;let size=9,next=258;const dict=new Map();
 const blk=[];let cur=0,bits=0;
 const emit=c=>{cur|=c<<bits;bits+=size;while(bits>=8){blk.push(cur&255);cur>>>=8;bits-=8}};
 emit(clear);let pre=idx[0];
 for(let i=1;i<idx.length;i++){const k=idx[i],key=pre<<8|k,v=dict.get(key);
  if(v!==undefined){pre=v;continue}
  emit(pre);
  if(next<4096){dict.set(key,next++);if(next>(1<<size)&&size<12)size++}
  else{emit(clear);dict.clear();size=9;next=258}
  pre=k}
 emit(pre);emit(eoi);if(bits>0)blk.push(cur&255);
 w.put(mc);for(let i=0;i<blk.length;i+=255){const n=Math.min(255,blk.length-i);w.put(n);for(let j=0;j<n;j++)w.put(blk[i+j])}w.put(0);
}
function encodeGif(q,frames){
 const w=new W8();w.bytes([71,73,70,56,57,97]);w.u16(SZ);w.u16(SZ);w.put(0xF7);w.put(0);w.put(0);w.bytes(q.pal);
 w.bytes([0x21,0xFF,11,...'NETSCAPE2.0'].map(c=>typeof c==='string'?c.charCodeAt(0):c));w.bytes([3,1,0,0,0]);
 frames.forEach((f,i)=>{const dl=Math.round(f.delay/10);w.bytes([0x21,0xF9,4,0x04,dl&255,dl>>8,0,0]);
  // dal secondo fotogramma salva solo il rettangolo cambiato: la sveglia decodifica e disegna meno
  let x0=0,y0=0,x1=SZ,y1=SZ;const cur=q.idx[i];
  if(i>0){const prev=q.idx[i-1];x0=SZ;y0=SZ;x1=0;y1=0;
   for(let y=0;y<SZ;y++)for(let x=0;x<SZ;x++)if(cur[y*SZ+x]!==prev[y*SZ+x]){if(x<x0)x0=x;if(x>=x1)x1=x+1;if(y<y0)y0=y;if(y>=y1)y1=y+1}
   if(x1<=x0){x0=y0=0;x1=y1=1}}
  const sub=new Uint8Array((x1-x0)*(y1-y0));for(let y=y0,k=0;y<y1;y++)for(let x=x0;x<x1;x++)sub[k++]=cur[y*SZ+x];
  w.put(0x2C);w.u16(x0);w.u16(y0);w.u16(x1-x0);w.u16(y1-y0);w.put(0);lzwEncode(w,sub)});
 w.put(0x3B);return new Blob([w.b.subarray(0,w.n)],{type:'image/gif'});
}
async function convert(){
 const f=$('gfile').files[0];if(!f)return;$('gup').disabled=true;gifBlob=null;$('ginfo').textContent='Conversione in corso…';
 await new Promise(r=>setTimeout(r,30));
 try{const g=parseGif(await f.arrayBuffer());
  if(g.frames.length>250)throw new Error('Troppi fotogrammi ('+g.frames.length+'), massimo 250');
  const fr=compose(g,$('gfit').value,$('gpix').checked,$('gbg').value);
  const q=quantize(fr);gifBlob=encodeGif(q,fr);
  $('gprev').src=URL.createObjectURL(gifBlob);$('gprev').style.display='block';
  const kb=gifBlob.size/1024;
  $('ginfo').textContent=`Originale ${g.W}×${g.H}, ${g.frames.length} fotogrammi → 240×240, ${kb.toFixed(0)} KB`;
  if(gifBlob.size>3.5*1048576){$('ginfo').textContent+=' — troppo grande (max 3,5 MB): scegli una GIF più corta';return}
  $('gup').disabled=false;
 }catch(e){$('ginfo').textContent='Errore: '+e.message}
}
// finestrina "captive" di iOS/macOS: niente Safari nello user agent -> i file non si possono scegliere
const inCNA=/iPhone|iPad|iPod|Macintosh/.test(navigator.userAgent)&&!/Safari|CriOS|FxiOS/.test(navigator.userAgent);
if(inCNA)$('cnaWarn').style.display='block';
document.querySelectorAll('.addr').forEach(e=>e.textContent='http://'+location.host);
$('gfile').addEventListener('change',()=>{const f=$('gfile').files[0];$('gfname').textContent=f?f.name:''});
['gfile','gfit','gpix','gbg'].forEach(id=>$(id).onchange=()=>{if(id==='gfile'&&$('gfile').files[0])$('gname').value=$('gfile').files[0].name.replace(/\.gif$/i,'');convert()});
const cleanName=s=>s.toLowerCase().normalize('NFD').replace(/[̀-ͯ]/g,'').replace(/[^a-z0-9_-]+/g,'_').slice(0,30)||'gif';
function uploadGif(){if(!gifBlob)return;const name=cleanName($('gname').value)+'.gif';
 if(S.gifs.some(g=>g.name===name)&&!confirm(name+' esiste già. Sostituirla?'))return;
 const fd=new FormData();fd.append('file',gifBlob,name);const x=new XMLHttpRequest();
 x.open('POST','/api/gif/upload?name='+encodeURIComponent(name));
 $('gprog').style.display='block';$('gup').disabled=true;
 x.upload.onprogress=e=>{if(e.lengthComputable)$('gprog').value=e.loaded/e.total*100};
 x.onload=()=>{$('gprog').style.display='none';$('gup').disabled=false;if(x.status===200){toast('GIF caricata ✓');load()}else toast('Errore: '+x.responseText)};
 x.onerror=()=>{$('gprog').style.display='none';$('gup').disabled=false;toast('Errore di rete')};
 x.send(fd)}
// se la sveglia non ha l'ora da internet, le passa quella del telefono
post('/api/time',{epoch:Math.floor(Date.now()/1000),tz:Intl.DateTimeFormat().resolvedOptions().timeZone||'',offset:new Date().getTimezoneOffset()})
 .then(r=>r.json()).then(j=>{if(j.applied){toast('Ora presa dal telefono ✓');setTimeout(load,300)}}).catch(()=>{});
load();setInterval(()=>{if(document.visibilityState==='visible'&&!document.activeElement.matches('input,select'))fetch('/api/state').then(r=>r.json()).then(s=>{S.time=s.time;S.wifi=s.wifi;$('status').textContent=(s.time?'Ora: '+s.time+' · ':'')+(s.wifi.connected?'Connessa a '+s.wifi.ssid+' ('+s.wifi.ip+')':'Non connessa a internet')}).catch(()=>{})},15000);
</script></body></html>)rawliteral";
