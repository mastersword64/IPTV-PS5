#pragma once
// The page the console serves to a phone, tablet or computer (see webadd.h). It is one file with
// no outside parts: the styles, the markup and the script are all here. Pictures (channel logos,
// posters) are fetched by the browser itself, straight from where the playlist says they are.
static const char kWebPage[] = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="referrer" content="no-referrer"><meta name="theme-color" content="#0a0c12"><title>IPTV on PS5</title><style>
:root{--ink:#0a0c12;--panel:#141824;--raise:#1d2231;--line:#ffffff1c;--text:#fff;--soft:#ffffffb4;--dim:#ffffff78;--live:#e23c46;--tint:#2c6fb8;--gold:#ffd75e}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html{background:var(--ink)}
body{margin:0;color:var(--text);font:16px/1.35 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;min-height:100vh;overscroll-behavior-y:none}
#amb{position:fixed;inset:0;z-index:-1;pointer-events:none;background:radial-gradient(120% 70% at 85% -10%,var(--tint),transparent 60%),radial-gradient(90% 60% at -10% 110%,var(--tint),transparent 55%);opacity:.5;transition:background 1.2s}
button,input,select{font:inherit;color:inherit}
button{border:0;background:var(--raise);border-radius:14px;cursor:pointer;touch-action:manipulation;user-select:none;-webkit-user-select:none;font-weight:650}
button:active{background:#fff;color:var(--ink)}
button:focus-visible,input:focus-visible,select:focus-visible,[tabindex]:focus-visible{outline:3px solid #fff;outline-offset:2px}
input,select{width:100%;padding:13px 16px;border-radius:14px;border:1px solid var(--line);background:#00000059;font-size:16px}
input::placeholder{color:var(--dim)}
.app{max-width:1500px;margin:0 auto;padding:0 14px calc(84px + env(safe-area-inset-bottom))}
.side{position:sticky;top:0;z-index:6;padding-top:10px;background:linear-gradient(var(--ink) 82%,transparent)}
.brand{display:flex;align-items:center;gap:9px;font-weight:800;font-size:17px;letter-spacing:-.01em;padding:2px 4px 8px}
.brand small{font-weight:500;color:var(--dim);font-size:13px;margin-left:auto;max-width:50%;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#dot{width:9px;height:9px;border-radius:50%;background:#777}#dot.ok{background:#46d17a}#dot.bad{background:var(--live)}

/* what is playing */
.now{background:var(--panel);border-radius:20px;padding:10px;display:grid;grid-template-columns:52px 1fr auto;gap:10px 12px;align-items:center}
.now .art{width:52px;height:52px;border-radius:12px}
.nw{min-width:0}.nw b,.nw span{display:block;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.nw b{font-size:16px;font-weight:750;letter-spacing:-.01em}.nw span{font-size:13px;color:var(--soft)}
.tr{display:flex;gap:6px}.tr button{width:40px;height:40px;border-radius:50%;padding:0;display:grid;place-items:center}
.tr svg{width:18px;height:18px;fill:currentColor}
#bar{grid-column:1/-1;height:16px;display:none;align-items:center;cursor:pointer}
#bar b{display:block;flex:1;height:5px;border-radius:3px;background:#ffffff2e;overflow:hidden}#bar i{display:block;height:100%;width:0;background:#fff;border-radius:3px}
.pill{display:inline-block;background:var(--live);font-size:10px;font-weight:800;border-radius:9px;padding:1px 7px;margin-right:6px;vertical-align:1px;letter-spacing:.04em}
#kb{display:none;background:#fff;color:var(--ink);border-radius:20px;padding:12px;margin-top:8px}
#kb p{margin:0 4px 8px;font-size:13px;font-weight:600}
#kb div{display:flex;gap:8px}#kb input{background:#0a0c1214;border-color:#0a0c1230;color:var(--ink)}
#kb button{background:var(--ink);color:#fff;padding:0 20px}
#note{display:none;background:#fff;color:var(--ink);border-radius:14px;padding:11px 15px;margin-top:8px;font-weight:600;font-size:14px}

/* the remote */
#remote{display:none;padding:18px 0 6px}
body.t0 #remote{display:block}
.pair{display:flex;gap:10px;margin:10px 0}.pair button{flex:1;height:54px;font-size:15px;line-height:1.15}
.pair small{display:block;font-weight:500;color:var(--dim);font-size:11px}.pair button:active small{color:inherit}
.ring{position:relative;width:min(78vw,300px);aspect-ratio:1;margin:18px auto;border-radius:50%;background:var(--raise);box-shadow:inset 0 0 0 1px var(--line),0 18px 50px #0008}
.ring button{position:absolute;background:none;border-radius:50%;width:34%;height:34%;display:grid;place-items:center;font-size:0}
.ring button:active{background:#ffffff26;color:#fff}
.ring svg{width:38%;height:38%;fill:#fff}
.ring .u{top:1%;left:33%}.ring .d{bottom:1%;left:33%}.ring .l{left:1%;top:33%}.ring .r{right:1%;top:33%}
.ring .ok{inset:33%;width:34%;height:34%;background:#fff;color:var(--ink);font-size:19px;font-weight:800;box-shadow:0 6px 22px #0009}
.ring .ok:active{background:#cfd4e2;color:var(--ink)}
.help{color:var(--dim);font-size:13px;text-align:center;margin:12px 0 0}

/* the tabs */
nav{position:fixed;left:0;right:0;bottom:0;z-index:8;display:flex;background:#0d1019f2;backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px);border-top:1px solid var(--line);padding:6px 4px calc(6px + env(safe-area-inset-bottom))}
nav button{flex:1;min-width:0;background:none;border-radius:12px;padding:6px 0 4px;font-size:10.5px;font-weight:600;color:var(--dim);display:grid;justify-items:center;gap:3px}
nav svg{width:22px;height:22px;fill:none;stroke:currentColor;stroke-width:1.9;stroke-linecap:round;stroke-linejoin:round}
nav button.on{color:#fff}nav button.on svg{stroke-width:2.4}
nav button:active{background:#ffffff1c;color:#fff}
main{padding-top:6px}
section{display:none}section.on{display:block}
.chips{display:flex;gap:8px;overflow-x:auto;padding:10px 0 12px;scrollbar-width:none}.chips::-webkit-scrollbar{display:none}
.chips button{flex:none;padding:9px 16px;border-radius:22px;font-size:14px;background:#ffffff1f}
.chips button.on{background:#fff;color:var(--ink)}
.find{display:flex;gap:8px;margin:10px 0 4px}.find button{flex:none;padding:0 20px;background:#fff;color:var(--ink)}
h2{font-size:22px;font-weight:800;letter-spacing:-.02em;margin:18px 4px 10px}
.back{display:flex;align-items:center;gap:12px;margin:12px 0 10px;cursor:pointer;border-radius:16px;padding:6px}
.back:active{background:#ffffff14}
.back .art{width:54px;height:54px;border-radius:12px;flex:none}.back.tall .art{width:60px;height:90px}
.back b{font-size:20px;font-weight:800;letter-spacing:-.02em;display:block}.back span{color:var(--dim);font-size:13px}
.msg{color:var(--dim);text-align:center;padding:34px 16px}
.msg button{display:block;margin:14px auto 0;padding:11px 22px;background:#fff;color:var(--ink)}
.spin{width:26px;height:26px;border-radius:50%;border:3px solid #ffffff30;border-top-color:#fff;margin:0 auto 14px;animation:sp .8s linear infinite}
@keyframes sp{to{transform:rotate(360deg)}}

/* pictures: the picture itself, over the title's colour and initials until (or unless) it arrives */
.art{position:relative;overflow:hidden;background:var(--c,#2a3044);display:grid;place-items:center;font-weight:800;color:#fff;flex:none}
.art:after{content:"";position:absolute;inset:0;background:linear-gradient(transparent 40%,#0005)}
.art img{position:absolute;inset:0;width:100%;height:100%;z-index:1}
.art.logo img{object-fit:contain;padding:12%;background:#1b1f2c}
.art.poster img{object-fit:cover}

/* groups */
.groups{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}
.g{background:var(--panel);border-radius:16px;padding:14px 14px 13px;cursor:pointer;border-left:5px solid var(--c);min-height:70px;display:flex;flex-direction:column;justify-content:center}
.g:active{background:#fff;color:var(--ink)}.g b{font-weight:700;font-size:15px;line-height:1.2;overflow-wrap:anywhere}.g span{font-size:12px;color:var(--dim);margin-top:3px}.g:active span{color:inherit}
/* rows: channels, stations, episodes */
.rows{display:grid;grid-template-columns:repeat(auto-fill,minmax(300px,1fr));gap:8px}
.row{display:flex;align-items:center;gap:12px;background:var(--panel);border-radius:16px;padding:9px 6px 9px 9px;cursor:pointer;min-width:0}
.row:active{background:#262c3f}
.row .art{width:54px;height:54px;border-radius:12px;font-size:17px}
.row .w{flex:1;min-width:0}.row b,.row span{display:block;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.row b{font-weight:700}.row span{font-size:13px;color:var(--dim)}
.row.cur{background:#fff;color:var(--ink)}.row.cur span{color:#0a0c12a8}
.star{flex:none;width:44px;height:44px;border-radius:50%;background:none;font-size:20px;color:var(--dim);padding:0}
.star.on{color:var(--gold)}.row.cur .star{color:#0a0c1266}.row.cur .star.on{color:#b98600}
/* posters: films and series */
.posters{display:grid;grid-template-columns:repeat(auto-fill,minmax(104px,1fr));gap:14px 10px}
.p{cursor:pointer;min-width:0}
.p .art{width:100%;aspect-ratio:2/3;border-radius:12px;font-size:30px;transition:transform .12s}
.p:active .art{transform:scale(.96)}
.p.cur .art{box-shadow:0 0 0 3px #fff}
.p b{display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;overflow:hidden;font-size:13px;font-weight:650;line-height:1.25;margin-top:7px}
.p span{display:block;font-size:12px;color:var(--dim);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.p .fv{position:absolute;z-index:2;top:6px;right:7px;color:var(--gold);font-size:16px;text-shadow:0 1px 4px #000}
.p .pr{position:absolute;z-index:2;left:8px;right:8px;bottom:8px;height:4px;border-radius:2px;background:#0009}.p .pr i{display:block;height:100%;background:#fff;border-radius:2px}

/* a film's own sheet */
#sheet{position:fixed;inset:0;z-index:20;background:#000a;display:none;align-items:flex-end;justify-content:center}
/* playing on this device */
.where{margin-left:10px;padding:6px 12px;border-radius:16px;font-size:12.5px;background:#ffffff1f;font-weight:650}
.where.here{background:#fff;color:var(--ink)}
#own{position:fixed;inset:0;z-index:30;background:#05060af5;display:none;flex-direction:column;align-items:center;justify-content:center;padding:16px;gap:14px}
#own.on{display:flex}
#own h3{margin:0;font-size:20px;font-weight:800;text-align:center;max-width:900px}
#own video{width:100%;max-width:1100px;max-height:62vh;background:#000;border-radius:16px}
#own audio{width:100%;max-width:560px}
#own p{margin:0;color:var(--soft);font-size:14px;text-align:center;max-width:620px}
#own .acts{display:flex;flex-wrap:wrap;gap:8px;justify-content:center}
#own .acts button{padding:12px 18px}#own .acts .go{background:#fff;color:var(--ink)}
#sheet.on{display:flex}
.sh{background:var(--panel);border-radius:26px 26px 0 0;width:100%;max-width:560px;padding:20px 20px calc(22px + env(safe-area-inset-bottom));display:grid;grid-template-columns:110px 1fr;gap:16px;animation:up .18s ease-out}
@keyframes up{from{transform:translateY(40px);opacity:0}}
.sh .art{width:110px;aspect-ratio:2/3;border-radius:12px;font-size:30px}
.sh h3{margin:2px 0 4px;font-size:21px;font-weight:800;letter-spacing:-.02em;line-height:1.15}.sh p{margin:0;color:var(--soft);font-size:14px}
.sh .acts{grid-column:1/-1;display:grid;gap:8px}
.sh .acts button{height:52px;font-size:16px}.sh .acts .go{background:#fff;color:var(--ink)}

/* set-up */
.card{background:var(--panel);border-radius:20px;padding:18px;margin:12px 0}
.card h3{margin:0 0 6px;font-size:18px;font-weight:800;letter-spacing:-.01em}
.card label{display:block;font-size:13px;color:var(--soft);margin:12px 0 5px}
.card p{margin:0 0 4px;color:var(--dim);font-size:14px}
.card button{width:100%;height:50px;margin-top:14px}.card .go{background:#fff;color:var(--ink)}
#pl div{padding:5px 0;color:var(--soft)}
.setup{display:grid;gap:0 14px;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));align-items:start}

/* tablets and computers: what is playing and the remote stay at the left; lists fill the rest */
@media(min-width:900px){
 .app{display:grid;grid-template-columns:340px 1fr;gap:0 30px;padding:0 26px 40px}
 .side{align-self:start;top:0;padding-top:22px;background:none;max-height:100vh;overflow-y:auto;scrollbar-width:none}
 .now{grid-template-columns:1fr;justify-items:center;text-align:center;padding:22px 18px 18px;border-radius:26px}
 .now .art{width:150px;height:150px;border-radius:22px;font-size:44px}
 .nw{width:100%}.nw b{font-size:21px;white-space:normal;line-height:1.15}.nw span{font-size:14px;margin-top:4px}
 .tr button{width:48px;height:48px}
 #remote{display:block;padding-top:10px}.ring{width:250px;margin:16px auto}
 main{padding-top:20px;min-width:0}
 nav{position:sticky;top:0;bottom:auto;border:0;background:linear-gradient(var(--ink) 80%,transparent);backdrop-filter:none;-webkit-backdrop-filter:none;padding:2px 0 14px;gap:6px;z-index:5}
 nav button{flex:none;grid-auto-flow:column;align-items:center;gap:8px;padding:10px 16px;font-size:14.5px;border-radius:22px;background:#ffffff14;color:var(--soft)}
 nav svg{width:18px;height:18px}nav button.on{background:#fff;color:var(--ink)}nav .rm{display:none}
 .posters{grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:22px 16px}.p b{font-size:14.5px}.p span{font-size:13px}
 .groups{grid-template-columns:repeat(auto-fill,minmax(200px,1fr))}
 #sheet{align-items:center}.sh{border-radius:26px;grid-template-columns:160px 1fr;padding:24px}.sh .art{width:160px}
 .find{max-width:560px}#cf{max-width:420px}
}
@media(prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
</style></head><body class="t0">
<div id="amb"></div>
<div class="app">
<aside class="side">
 <div class="brand"><span id="dot"></span>IPTV on PS5<button class="where" id="where" onclick="setHere(!here)"></button><small id="pln"></small></div>
 <div class="now"><div class="art logo" id="na"></div><div class="nw"><b id="nt">Connecting...</b><span id="ns"></span></div>
  <div class="tr"><button onclick="cmd('prev')" aria-label="Previous"><svg viewBox="0 0 24 24"><path d="M6 5h2v14H6zM20 5v14L9 12z"/></svg></button><button id="pp" onclick="cmd('pause')" aria-label="Pause or play"></button><button onclick="cmd('stop')" aria-label="Stop"><svg viewBox="0 0 24 24"><path d="M6 6h12v12H6z"/></svg></button><button onclick="cmd('next')" aria-label="Next"><svg viewBox="0 0 24 24"><path d="M16 5h2v14h-2zM4 5v14l11-7z"/></svg></button></div>
  <div id="bar" title="Tap to move through the film"><b><i></i></b></div></div>
 <div id="kb"><p id="kbl"></p><div><input id="kbi" autocapitalize="off" autocorrect="off" autocomplete="off" spellcheck="false" enterkeyhint="done"><button onclick="kbSend(1)">Done</button></div></div>
 <div id="note"></div>
 <div id="remote">
  <div class="pair"><button data-k="back">Back</button><button data-k="menu">Settings<small>Options</small></button></div>
  <div class="ring"><button class="u" data-k="up" data-r="1" aria-label="Up"><svg viewBox="0 0 24 24"><path d="M12 6l8 11H4z"/></svg></button><button class="l" data-k="left" data-r="1" aria-label="Left"><svg viewBox="0 0 24 24"><path d="M6 12l11-8v16z"/></svg></button><button class="ok" data-k="ok">OK</button><button class="r" data-k="right" data-r="1" aria-label="Right"><svg viewBox="0 0 24 24"><path d="M18 12L7 4v16z"/></svg></button><button class="d" data-k="down" data-r="1" aria-label="Down"><svg viewBox="0 0 24 24"><path d="M12 18L4 7h16z"/></svg></button></div>
  <div class="pair"><button data-k="fav">&#9633; Favorite</button><button data-k="search">&#9651; Search</button></div>
  <div class="pair"><button data-k="l1">L1<small>list before</small></button><button data-k="r1">R1<small>list after</small></button><button data-k="l2">L2<small>screen before</small></button><button data-k="r2">R2<small>screen after</small></button></div>
  <div class="pair" id="seek" style="display:none"><button onclick="cmd('seek',-300)">&#8722;5 min</button><button onclick="cmd('seek',-30)">&#8722;30 s</button><button onclick="cmd('seek',30)">+30 s</button><button onclick="cmd('seek',300)">+5 min</button></div>
  <div class="pair"><button onclick="cmd('full')">Full screen</button><button onclick="cmd('fav')">&#9733; This one</button></div>
  <p class="help" id="keys">Hold an arrow to keep moving.</p>
 </div>
</aside>
<main>
<nav id="nav"></nav>
<section></section>
<section><div class="chips" id="cc"><button onclick="chan('groups',this)" class="on">Groups</button><button onclick="chan('favs',this)">Favorites</button><button onclick="chan('recent',this)">Recent</button></div>
<input id="cf" placeholder="Filter this list" oninput="draw(1)" autocapitalize="off" autocorrect="off" spellcheck="false">
<div id="l1"></div></section>
<section><form class="find" onsubmit="return find()"><input id="sq" placeholder="Channels, films and series" enterkeyhint="search" autocapitalize="off" autocorrect="off"><button>Search</button></form>
<div id="l2"><div class="msg">Search everything in the playlist by name.</div></div></section>
<section><div class="chips" id="rc"><button onclick="rad('fav',this)">Favorites</button><button onclick="rad('recent',this)">Recent</button><button onclick="rad('popular',this)">Popular</button></div>
<form class="find" onsubmit="return radFind()"><input id="rq" placeholder="A station's name" enterkeyhint="search" autocapitalize="off" autocorrect="off"><button>Search</button></form>
<div id="l3"></div></section>
<section><div class="setup">
<div class="card"><h3>Playlists on the console</h3><div id="pl"></div></div>
<div class="card"><h3>Add a playlist</h3>
<label for="type">Kind</label><select id="type" onchange="kind()"><option value="url">M3U web address</option><option value="xtream">Xtream Codes account</option><option value="stalker">Stalker / Ministra portal</option></select>
<label for="name">Name (optional)</label><input id="name" placeholder="My TV">
<label id="la" for="a">Address of the M3U playlist</label><input id="a" placeholder="http://example.com/list.m3u" autocapitalize="off" autocorrect="off">
<div id="xb"><label id="lb" for="b">Username</label><input id="b" autocapitalize="off" autocorrect="off"></div>
<div id="xc"><label for="c">Password</label><input id="c" autocapitalize="off" autocorrect="off"></div>
<div id="xm"><label for="mac">MAC address (optional)</label><input id="mac" placeholder="00:1A:79:12:34:56" autocapitalize="characters"></div>
<button class="go" onclick="add()">Add to the console</button></div>
<div class="card"><h3>Send an M3U file</h3><p>A .m3u or .m3u8 file from this device.</p>
<input type="file" id="file" accept=".m3u,.m3u8,audio/x-mpegurl,text/plain"><button onclick="sendFile()">Send the file</button></div>
<div class="card"><h3>Backup</h3><p>Playlists, settings, favorites, hidden groups and watch history, as one file.</p>
<button onclick="location=u('/backup')">Save a backup to this device</button>
<label for="backup">Restore from a backup file</label><input type="file" id="backup"><button onclick="restore()">Restore to the console</button></div>
</div></section>
</main></div>
<div id="sheet" onclick="if(event.target==this)shut()"></div>
<div id="own"></div>

<script>
var K=(location.search.match(/[?&]k=([^&]*)/)||[])[1]||'';
function $(i){return document.getElementById(i)}
function u(p){return p+(p.indexOf('?')<0?'?':'&')+'k='+K}
function esc(t){return String(t==null?'':t).replace(/[&<>"']/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]})}
var noteT=0;
function note(t){var n=$('note');n.textContent=t;n.style.display=t?'block':'none';clearTimeout(noteT);if(t)noteT=setTimeout(function(){n.style.display='none'},5000)}
function lost(){note('The console did not answer. Is the app still open?')}
function get(p){return fetch(u(p),{cache:'no-store'}).then(function(r){if(r.status==403){location.reload()}return r.json()})}
function send(p,b,h){return fetch(u(p),{method:'POST',body:b||'',headers:h||{}})}
function key(b){send('/key?b='+b).catch(lost);if(navigator.vibrate)navigator.vibrate(8)}
function cmd(c,by){send('/cmd?c='+c+(by?'&by='+by:'')).then(function(){setTimeout(poll,250)}).catch(lost)}

/* a colour and initials for anything without a picture: the same ones the TV uses */
var PAL=['#b5483a','#2c6fb8','#2a8a70','#c08a2c','#7a4cc0','#b83f78','#3a94b8','#5f9a3e','#596079','#c2603c'];
function tint(n){var h=2166136261;n=unescape(encodeURIComponent(n||''));for(var i=0;i<n.length;i++)h=Math.imul(h^n.charCodeAt(i),16777619)>>>0;return PAL[h%10]}
function inits(n){var o='',s=true;n=n||'';for(var i=0;i<n.length&&o.length<2;i++){var c=n[i],l=/[A-Za-z0-9]/.test(c);if(l&&s)o+=c.toUpperCase();s=!l}return o||'TV'}
function art(n,p,kind){return'<div class="art '+kind+'" style="--c:'+tint(n)+'">'+esc(inits(n))+(p?'<img loading="lazy" decoding="async" referrerpolicy="no-referrer" alt="" src="'+esc(p)+'" onerror="this.remove()">':'')}

/* the remote's buttons: one press, or held for the arrows; and the same from a computer's keyboard */
var holdT=0,holdI=0;
function release(){clearTimeout(holdT);clearInterval(holdI)}
Array.prototype.forEach.call(document.querySelectorAll('[data-k]'),function(b){
 var k=b.getAttribute('data-k'),rep=b.getAttribute('data-r');
 b.addEventListener('pointerdown',function(e){e.preventDefault();release();key(k);
  if(rep)holdT=setTimeout(function(){holdI=setInterval(function(){key(k)},140)},420)});
 ['pointerup','pointerleave','pointercancel'].forEach(function(n){b.addEventListener(n,release)});
 b.addEventListener('contextmenu',function(e){e.preventDefault()});
});
var KEYS={ArrowUp:'up',ArrowDown:'down',ArrowLeft:'left',ArrowRight:'right',Enter:'ok',Escape:'back',Backspace:'back'};
document.addEventListener('keydown',function(e){var t=e.target.tagName;
 if(t=='INPUT'||t=='SELECT'||t=='TEXTAREA'||e.ctrlKey||e.metaKey||e.altKey)return;
 if($('own').className){if(e.key=='Escape')shutOwn();return}
 if($('sheet').className){if(e.key=='Escape')shut();return}
 if(t=='BUTTON'&&e.key=='Enter')return;
 if(KEYS[e.key]){e.preventDefault();key(KEYS[e.key])}});
if(matchMedia('(pointer:fine)').matches)$('keys').textContent='On a keyboard: the arrows, Enter for OK, Esc for Back.';

/* tabs */
var IC={r:'<rect x="7" y="2.5" width="10" height="19" rx="3"/><circle cx="12" cy="8" r="2"/><path d="M10.5 14h3M10.5 17h3"/>',
 l:'<rect x="2.5" y="6" width="19" height="13" rx="2.5"/><path d="M8 2.5l4 3.5 4-3.5"/>',
 m:'<rect x="3" y="3" width="18" height="18" rx="2.5"/><path d="M8 3v18M16 3v18M3 8h5M3 16h5M16 8h5M16 16h5"/>',
 s:'<path d="M12 3l9 5-9 5-9-5zM3 12.5l9 5 9-5M3 17l9 5 9-5"/>',
 f:'<circle cx="10.5" cy="10.5" r="6.5"/><path d="M15.5 15.5L21 21"/>',
 a:'<path d="M9 18V5.5l11-2.5v12.5"/><circle cx="6" cy="18" r="3"/><circle cx="17" cy="15.5" r="3"/>',
 u:'<path d="M12 5v14M5 12h14"/><rect x="2.5" y="2.5" width="19" height="19" rx="5"/>'};
var TABS=[['Remote','r',0,0],['Live TV','l',1,0],['Movies','m',1,1],['Series','s',1,2],['Search','f',2,0],['Radio','a',3,0],['Setup','u',4,0]];
$('nav').innerHTML=TABS.map(function(t,i){return'<button'+(i?'':' class="rm on"')+' onclick="go('+i+')"><svg viewBox="0 0 24 24">'+IC[t[1]]+'</svg>'+t[0]+'</button>'}).join('');
var cur=0,lib=0,libShown=-1,at=0,wide=matchMedia('(min-width:900px)');
function go(i){at=i;var t=TABS[i];cur=t[2];if(cur==1)lib=t[3];
 var s=document.querySelectorAll('main section'),n=$('nav').children;
 for(var j=0;j<s.length;j++)s[j].className=j==cur?'on':'';
 for(j=0;j<n.length;j++)n[j].className=(j?'':'rm ')+(j==i?'on':'');
 document.body.className='t'+cur;
 if(cur==1&&(libShown!=lib||!L[1].url)){libShown=lib;$('cc').children[2].textContent=lib?'Continue watching':'Recent';chan('groups')}
 if(cur==3&&!L[3].url)rad('popular',$('rc').children[2]);
 window.scrollTo(0,0)}
function fit(){if(wide.matches&&cur==0)go(1)}
(wide.addEventListener?wide.addEventListener('change',fit):wide.addListener(fit));

/* what the console is doing */
var kbOpen=false,kbT=0,st={};
var PLAY='<svg viewBox="0 0 24 24"><path d="M7 4v16l13-8z"/></svg>',PAUSE='<svg viewBox="0 0 24 24"><path d="M6 5h4v14H6zM14 5h4v14h-4z"/></svg>';
function clock(s){s=Math.max(0,Math.floor(s));var h=Math.floor(s/3600),m=Math.floor(s/60)%60,x=s%60;return(h?h+':'+(m<10?'0':''):'')+m+':'+(x<10?'0':'')+x}
function poll(){get('/status').then(function(s){var was=(st.on?st.name:'')+'|'+(st.art||'');st=s;
 $('dot').className='ok';$('pln').textContent=s.playlist||'';
 var name=s.on?s.name:'Nothing is playing',now=(s.on?s.name:'')+'|'+(s.art||'');
 $('nt').textContent=name;
 var sub=s.on?(s.sub||''):('On the TV: '+(s.screen||''));
 if(s.on&&s.dur>0)sub=clock(s.pos)+' / '+clock(s.dur)+(s.sub?'   '+s.sub:'');
 $('ns').innerHTML=(s.on&&!s.vod&&s.kind==0?'<i class="pill">LIVE</i>':s.on&&s.kind==6?'<i class="pill">ON AIR</i>':'')+esc(sub);
 if(now!=was){$('na').outerHTML=art(s.on?s.name:'IPTV',s.on?s.art:'',s.kind==1||s.kind==2?'poster':'logo').replace('class="art','id="na" class="art')+'</div>';
  document.documentElement.style.setProperty('--tint',s.on?tint(s.name):'#2c6fb8');if(cur>0&&cur<4)draw(cur)}
 $('pp').style.display=s.on&&s.vod?'':'none';$('pp').innerHTML=s.paused?PLAY:PAUSE;
 $('seek').style.display=s.on&&s.vod?'flex':'none';
 var bar=$('bar');bar.style.display=s.on&&s.dur>0?'flex':'none';
 if(s.dur>0)bar.firstChild.firstChild.style.width=Math.min(100,100*s.pos/s.dur)+'%';
 if(s.kb&&!kbOpen){$('kbi').value=s.kbt||'';$('kbl').textContent='The keyboard is open on the TV ('+(s.kbl||'text')+'). Type here instead.'}
 kbOpen=!!s.kb;$('kb').style.display=kbOpen?'block':'none';
 if(s.lists)$('pl').innerHTML=s.lists.length?s.lists.map(function(n){return'<div>'+esc(n)+'</div>'}).join(''):'<p>None yet. Add one below.</p>';
}).catch(function(){$('dot').className='bad';$('nt').textContent='Not connected to the console';$('ns').textContent='Is the app open on the PS5?'})}
setInterval(poll,2000);poll();
$('bar').addEventListener('click',function(e){if(!(st.dur>0))return;var r=this.getBoundingClientRect(),to=st.dur*Math.max(0,Math.min(1,(e.clientX-r.left)/r.width));cmd('seek',Math.round(to-st.pos)||1)});
function kbSend(done){clearTimeout(kbT);send('/text'+(done?'?done=1':''),$('kbi').value,{'Content-Type':'text/plain'}).catch(lost);if(done){kbOpen=false;$('kb').style.display='none';$('kbi').blur()}}
$('kbi').addEventListener('input',function(){clearTimeout(kbT);kbT=setTimeout(function(){kbSend(0)},180)});
$('kbi').addEventListener('keydown',function(e){if(e.key=='Enter'){e.preventDefault();kbSend(1)}});

/* lists: one per tab (1 a library, 2 search, 3 radio) */
var L={1:{},2:{},3:{}},waitT={},STEP=240;
function busy(t){return'<div class="msg"><div class="spin"></div>'+esc(t)+'</div>'}
function load(n,url,tries){var l=L[n];l.url=url;clearTimeout(waitT[n]);
 if(!tries){l.d=null;l.max=STEP;$('l'+n).innerHTML=busy('Loading...');if(n==1)$('cf').value=''}
 get(url).then(function(d){if(l.url!=url)return;
  if(d.wait){if((tries||0)>150){$('l'+n).innerHTML='<div class="msg">The console is taking too long.<button onclick="load('+n+',L['+n+'].url)">Try again</button></div>';return}
   if(!tries)$('l'+n).innerHTML=busy(d.msg||'Loading on the console...');
   waitT[n]=setTimeout(function(){load(n,url,(tries||0)+1)},300);return}
  l.d=d;draw(n);
  if(d.more){waitT[n]=setTimeout(function(){if(l.url==url)more(n,d.more,0)},600)}
 }).catch(function(){if(l.url==url)$('l'+n).innerHTML='<div class="msg">The console did not answer.<button onclick="load('+n+',L['+n+'].url)">Try again</button></div>'})}
function more(n,url,tries){var l=L[n],was=l.url;get(url).then(function(d){if(l.url!=was||d.wait)return;
 var grew=!l.d||!l.d.rows||!d.rows||d.rows.length!=l.d.rows.length||!d.more;l.d=d;if(grew)draw(n);
 if(d.more&&tries<200)waitT[n]=setTimeout(function(){more(n,d.more,tries+1)},700)}).catch(function(){})}
function draw(n){var l=L[n],d=l.d,box=$('l'+n);if(!d)return;
 if(d.err){box.innerHTML='<div class="msg">'+esc(d.err)+'<button onclick="load('+n+',L['+n+'].url)">Try again</button></div>';return}
 var f=n==1?$('cf').value.trim().toLowerCase():'',h='',shown=0,rows=d.rows||[],open='',cut=false,playing=st.on?st.name:null;
 function wrap(c){if(open!=c){if(open)h+='</div>';if(c)h+='<div class="'+c+'">';open=c}}
 if(d.back)h+='<div class="back'+(d.art?' tall':'')+'" data-b="'+esc(d.back)+'">'+(d.art?art(d.t,d.art,'poster')+'</div>':'')+'<div><span>&#8249; Back</span><b>'+esc(d.t||'')+'</b></div></div>';
 else if(d.t&&rows.length)h+='<h2>'+esc(d.t)+'</h2>';
 for(var i=0;i<rows.length;i++){var r=rows[i];
  if(r.k=='h'){wrap('');h+='<h2>'+esc(r.n)+'</h2>';continue}
  if(f&&r.n.toLowerCase().indexOf(f)<0)continue;
  if(++shown>l.max){cut=true;break}
  var a='data-k="'+r.k+'" data-i="'+r.i+'" data-x="'+i+'"',on=r.k=='c'&&r.n===playing?' cur':'';
  if(r.k=='g'){wrap('groups');h+='<div class="g" style="--c:'+tint(r.n)+'" '+a+'><b>'+esc(r.n)+'</b>'+(r.s?'<span>'+esc(r.s)+'</span>':'')+'</div>'}
  else if(r.w){wrap('posters');h+='<div class="p'+on+'" '+a+'>'+art(r.n,r.p,'poster')+(r.f?'<i class="fv">&#9733;</i>':'')+(r.r?'<i class="pr"><i style="width:'+Math.round(r.r*100)+'%"></i></i>':'')+'</div><b>'+esc(r.n)+'</b>'+(r.s?'<span>'+esc(r.s)+'</span>':'')+'</div>'}
  else{wrap('rows');h+='<div class="row'+on+'" '+a+'>'+art(r.n,r.p,'logo')+'</div><div class="w"><b>'+esc(r.n)+'</b>'+(r.s?'<span>'+esc(r.s)+'</span>':'')+'</div>'+(r.f!=null?'<button class="star'+(r.f?' on':'')+'" data-f="1" aria-label="Favorite">'+(r.f?'&#9733;':'&#9734;')+'</button>':'')+'</div>'}}
 wrap('');
 if(cut)h+='<div class="msg">Showing the first '+l.max+'.<button onclick="L['+n+'].max+=STEP;draw('+n+')">Show more</button></div>';
 if(!shown)h+='<div class="msg">'+esc(f?'Nothing matches the filter.':(d.none||'Nothing here.'))+'</div>';
 if(d.more)h+='<div class="msg"><div class="spin"></div>Still loading more...</div>';
 box.innerHTML=h}
function play(n,r,fresh){fetch(u('/play?s='+L[n].d.slot+'&g='+L[n].d.gen+'&i='+r.i+(fresh?'&fresh=1':'')),{method:'POST'}).then(function(x){return x.json()}).then(function(d){
 note(d.err||d.msg||'');if(d.stale)load(n,L[n].url);setTimeout(poll,400)}).catch(lost)}
function fav(n,r,then){fetch(u('/play?act=fav&s='+L[n].d.slot+'&g='+L[n].d.gen+'&i='+r.i),{method:'POST'}).then(function(x){return x.json()}).then(function(d){
 if(d.err){note(d.err);if(d.stale)load(n,L[n].url);return}r.f=d.f;draw(n);if(then)then()}).catch(lost)}
[1,2,3].forEach(function(n){$('l'+n).addEventListener('click',function(e){var t=e.target,star=false;
 while(t&&t!=this&&!t.getAttribute('data-k')&&!t.getAttribute('data-b')){if(t.getAttribute('data-f'))star=true;t=t.parentNode}
 if(!t||t==this)return;
 if(t.getAttribute('data-b')){load(n,t.getAttribute('data-b'));return}
 var k=t.getAttribute('data-k'),r=L[n].d.rows[+t.getAttribute('data-x')];if(!r)return;
 if(star){fav(n,r);return}
 if(k=='g')load(n,'/list?what=group&lib='+lib+'&i='+r.i);
 else if(k=='e')load(n,'/list?what=episodes&s='+L[n].d.slot+'&g='+L[n].d.gen+'&i='+r.i);
 else if(r.w)sheet(n,r);
 else if(here)ask(n,r);
 else{t.className+=' cur';play(n,r)}
})});
/* a film: its poster, and what can be done with it */
function sheet(n,r){var part=r.r>0.01&&r.r<0.97,canFav=r.f!=null;
 $('sheet').innerHTML='<div class="sh">'+art(r.n,r.p,'poster')+'</div><div><h3>'+esc(r.n)+'</h3><p>'+esc(r.s||'')+'</p></div><div class="acts">'
  +'<button class="go" id="s1">'+(part?'Resume on the TV':'Play on the TV')+'</button>'+(part?'<button id="s2">Start from the beginning</button>':'')+'<button id="s5">Play here, in this page</button><button id="s6">Open in an app on this device</button>'
  +(canFav?'<button id="s3">'+(r.f?'&#9733; Remove from favorites':'&#9734; Add to favorites')+'</button>':'')+'<button id="s4">Close</button></div></div>';
 $('sheet').className='on';
 $('s5').onclick=function(){shut();playHere(n,r,0)};$('s6').onclick=function(){shut();playHere(n,r,1)};
 $('s1').onclick=function(){shut();play(n,r)};if(part)$('s2').onclick=function(){shut();play(n,r,1)};
 if(canFav)$('s3').onclick=function(){fav(n,r,function(){sheet(n,r)})};$('s4').onclick=shut;$('s1').focus()}
function shut(){$('sheet').className='';$('sheet').innerHTML=''}

/* playing on this device instead of the TV: in the page when the browser can, in VLC when it cannot */
var here=false;try{here=localStorage.getItem('here')=='1'}catch(e){}
function setHere(v){here=v;try{localStorage.setItem('here',v?'1':'0')}catch(e){}
 var b=$('where');b.textContent=v?'Asks where to play':'Plays on the TV';b.className='where'+(v?' here':'')}
setHere(here);
var hls=null;
/* "where shall this play?": the TV, this page, or an app on this device */
function ask(n,r){
 $('sheet').innerHTML='<div class="sh">'+art(r.n,r.p,r.w?'poster':'logo')+'</div><div><h3>'+esc(r.n)+'</h3><p>'+esc(r.s||'')+'</p></div><div class="acts">'
  +'<button class="go" id="a1">Play on the TV</button><button id="a2">Play here, in this page</button><button id="a3">Open in an app on this device</button><button id="a4">Close</button></div></div>';
 $('sheet').className='on';
 $('a1').onclick=function(){shut();play(n,r)};$('a2').onclick=function(){shut();playHere(n,r,0)};
 $('a3').onclick=function(){shut();playHere(n,r,1)};$('a4').onclick=shut}
function playHere(n,r,app){note('Getting the address...');
 fetch(u('/play?act=url&s='+L[n].d.slot+'&g='+L[n].d.gen+'&i='+r.i),{method:'POST'}).then(function(x){return x.json()}).then(function(d){
  note('');if(d.err){note(d.err);if(d.stale)load(n,L[n].url);return}own(d,app)}).catch(lost)}
function playlistFile(d){var a=document.createElement('a');a.href=URL.createObjectURL(new Blob(['#EXTM3U\n#EXTINF:-1,'+d.name+'\n'+d.url+'\n'],{type:'audio/x-mpegurl'}));a.download=d.name.replace(/[^\w .-]+/g,'_')+'.m3u';document.body.appendChild(a);a.click();a.remove()}
/* hands the stream to whichever player the device offers: Android asks which app; an iPhone or
   iPad shows its share sheet; a computer gets a small playlist file that opens in its player */
function openApp(d){var a=navigator.userAgent,m=d.url.match(/^(https?):\/\/(.*)$/);
 if(/Android/i.test(a)&&m){location.href='intent://'+m[2]+'#Intent;scheme='+m[1]+';action=android.intent.action.VIEW;type='+(d.audio?'audio':'video')+'/*;end';return}
 if(navigator.share&&(/iPhone|iPad|iPod/i.test(a)||(navigator.maxTouchPoints>1&&/Mac/i.test(a)))){navigator.share({title:d.name,url:d.url}).catch(function(){});return}
 playlistFile(d)}
function own(d,app){var o=$('own'),tag=d.audio?'audio':'video';
 o.innerHTML='<h3>'+esc(d.name)+'</h3><'+tag+' id="med" controls autoplay playsinline></'+tag+'><p id="ow"></p><div class="acts">'
  +'<button class="go" id="o1">Open in an app</button><button id="o2">Save as a playlist file</button><button id="o3">Copy the address</button><button id="o4">Close</button></div>';
 o.className='on';var med=$('med'),tried=false;
 function cannot(){$('ow').textContent='This browser cannot play this kind of stream here. "Open in an app" hands it to a player on this device (it asks which, if there are several).';med.style.display='none'}
 function viaHls(){if(tried)return cannot();tried=true;
  if(window.Hls&&Hls.isSupported()){hls=new Hls();hls.on(Hls.Events.ERROR,function(e,x){if(x.fatal){hls.destroy();hls=null;cannot()}});hls.loadSource(d.url);hls.attachMedia(med);med.play().catch(function(){})}
  else cannot()}
 $('o1').onclick=function(){med.pause();openApp(d)};$('o2').onclick=function(){playlistFile(d)};
 $('o3').onclick=function(){(navigator.clipboard?navigator.clipboard.writeText(d.url):Promise.reject()).then(function(){$('ow').textContent='Address copied.'},function(){prompt('The address:',d.url)})};
 $('o4').onclick=shutOwn;
 if(app){med.style.display='none';$('ow').textContent='Choose a player on this device.';openApp(d);return}
 med.addEventListener('error',function(){if(/\.m3u8/i.test(d.url))viaHls();else cannot()});
 if(/\.m3u8/i.test(d.url)&&!med.canPlayType('application/vnd.apple.mpegurl')){
  // most browsers need a helper for this kind of stream; it is fetched from the internet when there is one
  if(window.Hls)viaHls();else{var s=document.createElement('script');s.src='https://cdnjs.cloudflare.com/ajax/libs/hls.js/1.5.13/hls.min.js';s.onload=viaHls;s.onerror=cannot;document.head.appendChild(s)}}
 else{med.src=d.url;var p=med.play();if(p&&p.catch)p.catch(function(){})}}
function shutOwn(){if(hls){hls.destroy();hls=null}var m=$('med');if(m){m.pause();m.removeAttribute('src');m.load()}$('own').className='';$('own').innerHTML=''}
function chips(id,b){var c=$(id).children;for(var i=0;i<c.length;i++)c[i].className=c[i]==b?'on':''}
function chan(w,b){chips('cc',b||$('cc').children[0]);load(1,'/list?what='+w+'&lib='+lib)}
function find(){var q=$('sq').value.trim();if(q.length<2){note('Type at least two letters.');return false}
 $('sq').blur();load(2,'/list?what=search&q='+encodeURIComponent(q));return false}
function rad(m,b){chips('rc',b);load(3,'/list?what=radio&m='+m)}
function radFind(){var q=$('rq').value.trim();if(q.length<2){note('Type at least two letters.');return false}
 $('rq').blur();chips('rc',null);load(3,'/list?what=radio&m=search&q='+encodeURIComponent(q));return false}

/* set-up */
function v(i){return $(i).value.trim()}
function kind(){var t=v('type');$('xb').style.display=t=='url'?'none':'block';
$('xc').style.display=t=='xtream'?'block':'none';$('xm').style.display=t=='xtream'?'block':'none';
$('la').textContent=t=='url'?'Address of the M3U playlist':t=='xtream'?'Server address':'Portal address';
$('lb').textContent=t=='stalker'?'MAC address':'Username';
$('a').placeholder=t=='url'?'http://example.com/list.m3u':'http://example.com:8080'}
function say(p,b,h){return send(p,b,h).then(function(r){return r.text()}).then(function(t){note(t);window.scrollTo(0,0);setTimeout(poll,600)}).catch(lost)}
function add(){if(!v('a')){note('An address is needed.');return}
var f=['type','name','a','b','c','mac'].map(function(k){return k+'='+encodeURIComponent(v(k))}).join('&');
say('/add',f,{'Content-Type':'application/x-www-form-urlencoded'})}
function sendFile(){var f=$('file').files[0];if(!f){note('Choose a file first.');return}
say('/file?name='+encodeURIComponent(f.name),f)}
function restore(){var f=$('backup').files[0];if(!f){note('Choose a backup file first.');return}
if(confirm('Replace the playlists, settings and favorites on the console with this backup?'))say('/restore',f)}
kind();fit()</script></body></html>)HTML";
