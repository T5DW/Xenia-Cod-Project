#pragma once
// The status page served at http://<server>:36000/
static const char* const kStatusPage = R"BO1PAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>BO1 Live Server</title>
<style>
:root{--bg:#0d0f10;--panel:#16191b;--line:#262b2e;--text:#e8e6e1;--muted:#8d948f;--accent:#f28c28;--live:#5fd068}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.5 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:760px;margin:0 auto;padding:32px 16px 48px}
header{display:flex;align-items:baseline;justify-content:space-between;gap:12px;flex-wrap:wrap;border-bottom:2px solid var(--accent);padding-bottom:12px}
h1{margin:0;font-size:22px;letter-spacing:.08em;text-transform:uppercase}
h1 span{color:var(--accent)}
.state{font-size:13px;color:var(--muted)}
.state b{color:var(--live)}
.state.down b{color:#e05a4f}
.hero{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin:24px 0}
.tile{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:16px;min-width:0}
.tile .l{overflow-wrap:anywhere}
html,body{overflow-x:hidden}
.tile .n{font-size:40px;font-weight:700;line-height:1.1;font-variant-numeric:tabular-nums}
.tile.main .n{color:var(--accent);font-size:56px}
.tile .l{color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.08em}
h2{font-size:13px;color:var(--muted);text-transform:uppercase;letter-spacing:.1em;margin:28px 0 8px}
ul{list-style:none;margin:0;padding:0;background:var(--panel);border:1px solid var(--line);border-radius:8px}
li{display:flex;justify-content:space-between;padding:10px 16px;border-top:1px solid var(--line)}
li:first-child{border-top:0}
li .dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--live);margin-right:10px}
li .t{color:var(--muted);font-variant-numeric:tabular-nums}
.empty{color:var(--muted);padding:14px 16px}
code{background:var(--panel);border:1px solid var(--line);border-radius:4px;padding:2px 6px;font-size:14px;word-break:break-all}
ol{padding-left:20px;margin:8px 0}
ol li{display:list-item;border:0;padding:3px 0}
footer{margin-top:32px;color:var(--muted);font-size:12px}
@media (max-width:560px){.hero{grid-template-columns:1fr 1fr}.tile.main{grid-column:1/-1}}
</style>
</head>
<body>
<main>
<header>
  <h1>Black Ops <span>Xenia</span> Server</h1>
  <div class="state" id="state">Connecting…</div>
</header>
<section class="hero">
  <div class="tile main"><div class="n" id="online">–</div><div class="l">Players online</div></div>
  <div class="tile"><div class="n" id="matches">–</div><div class="l">Matches open</div></div>
  <div class="tile"><div class="n" id="registered">–</div><div class="l">Players with saved stats</div></div>
</section>
<h2>Online now</h2>
<ul id="players"><li class="empty">Nobody is online yet.</li></ul>
<h2>Linked servers</h2>
<ul id="peers"><li class="empty">No other servers linked. Matchmaking only finds games on this server.</li></ul>
<h2>How to join</h2>
<ol>
  <li>Use the Xenia Canary <b>netplay</b> build.</li>
  <li>In <code>xenia-canary.config.toml</code>, under [Live], set <code>api_address = "<span id="addr"></span>"</code> and <code>network_mode = 2</code>.</li>
  <li>Sign in with an Xbox Live enabled profile, start Black Ops and pick Xbox LIVE.</li>
</ol>
<footer id="foot"></footer>
</main>
<script>
document.getElementById('addr').textContent = location.protocol + '//' + location.host + '/';
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
function mins(m){return m<1?'just now':m<60?m+' min':Math.floor(m/60)+' h '+(m%60)+' min'}
async function tick(){
  const st=document.getElementById('state');
  try{
    const r=await fetch('/api/status',{cache:'no-store'}); const d=await r.json();
    document.getElementById('online').textContent=d.online;
    document.getElementById('matches').textContent=d.matches;
    document.getElementById('registered').textContent=d.registered;
    document.getElementById('players').innerHTML=d.players.length
      ? d.players.map(p=>'<li><span><span class="dot"></span>'+esc(p.gamertag)+'</span><span class="t">'+mins(p.minutes)+'</span></li>').join('')
      : '<li class="empty">Nobody is online yet.</li>';
    document.getElementById('peers').innerHTML=d.peers.length
      ? d.peers.map(p=>'<li><span>'+(p.online?'<span class="dot"></span>':'<span class="dot" style="background:#e05a4f"></span>')+esc(p.address)+'</span><span class="t">'+(p.online?'online':'offline')+'</span></li>').join('')
      : '<li class="empty">No other servers linked. Matchmaking only finds games on this server.</li>';
    st.className='state'; st.innerHTML='<b>●</b> Server online';
    document.getElementById('foot').textContent='Server '+d.version+' · up '+mins(Math.floor(d.uptime/60))+' · updates every 5 seconds';
  }catch(e){st.className='state down'; st.innerHTML='<b>●</b> Server offline';}
}
tick(); setInterval(tick,5000);
</script>
</body>
</html>
)BO1PAGE";
