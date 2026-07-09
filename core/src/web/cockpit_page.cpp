//
// Pagina HTML del Cockpit (autonoma: nessun CDN, funziona offline).
//
#include "sdrjo/web/cockpit_server.hpp"

namespace sdrjo {

const char* cockpitPageHtml()
{
    return R"WEB(<!DOCTYPE html>
<html lang="it">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SdrJo Cockpit</title>
<style>
  :root {
    --bg0: #0a0e14; --bg1: #10161f; --bg2: #161e2a;
    --line: rgba(120,160,200,.14);
    --text: #dbe4ee; --dim: #75859a;
    --acc: #38b6ff; --acc2: #7a5cff; --ok: #3ddc97; --warn: #ffb454;
    --mono: "Cascadia Code", "JetBrains Mono", Consolas, monospace;
  }
  * { box-sizing: border-box; margin: 0; }
  body {
    min-height: 100vh; background:
      radial-gradient(1200px 500px at 15% -10%, rgba(56,182,255,.10), transparent 60%),
      radial-gradient(1000px 500px at 90% -20%, rgba(122,92,255,.10), transparent 60%),
      var(--bg0);
    color: var(--text);
    font: 14px/1.5 "Segoe UI", system-ui, sans-serif;
    padding: 18px 22px 40px;
  }
  header { display: flex; align-items: center; gap: 16px; flex-wrap: wrap; }
  .logo { font-size: 20px; font-weight: 700; letter-spacing: .5px; }
  .logo b { background: linear-gradient(90deg, var(--acc), var(--acc2));
    -webkit-background-clip: text; background-clip: text; color: transparent; }
  .pill {
    padding: 3px 12px; border: 1px solid var(--line); border-radius: 999px;
    color: var(--dim); font-size: 12px; background: rgba(255,255,255,.02);
  }
  .pill b { color: var(--text); }
  .freq {
    margin-left: auto; font-family: var(--mono); font-size: 30px;
    font-weight: 600; color: var(--acc); letter-spacing: 1px;
    text-shadow: 0 0 24px rgba(56,182,255,.35);
  }
  .freq small { color: var(--dim); font-size: 14px; margin-left: 6px; }

  .panel {
    margin-top: 16px; background: linear-gradient(180deg, var(--bg2), var(--bg1));
    border: 1px solid var(--line); border-radius: 14px; overflow: hidden;
    box-shadow: 0 10px 30px rgba(0,0,0,.35);
  }
  .panel h2 {
    font-size: 11px; font-weight: 600; text-transform: uppercase;
    letter-spacing: 1.5px; color: var(--dim); padding: 10px 14px 0;
  }
  canvas { display: block; width: 100%; }
  #spectrum { height: 170px; }
  #waterfall { height: 190px; border-top: 1px solid var(--line); }
  .nosig { padding: 26px; text-align: center; color: var(--dim); }

  .grid {
    margin-top: 16px; display: grid; gap: 14px;
    grid-template-columns: repeat(auto-fill, minmax(270px, 1fr));
  }
  .card {
    background: linear-gradient(180deg, var(--bg2), var(--bg1));
    border: 1px solid var(--line); border-radius: 14px; padding: 14px 16px;
    transition: transform .15s, border-color .15s;
  }
  .card:hover { transform: translateY(-2px); border-color: rgba(56,182,255,.35); }
  .card .head { display: flex; align-items: center; gap: 10px; }
  .dot { width: 8px; height: 8px; border-radius: 50%; background: var(--ok);
    box-shadow: 0 0 10px var(--ok); }
  .card h3 { font-size: 15px; }
  .card .desc { color: var(--dim); font-size: 12px; margin: 6px 0 10px; }
  .kv { display: grid; grid-template-columns: auto 1fr; gap: 2px 14px; }
  .kv .k { color: var(--dim); font-size: 12px; }
  .kv .v { font-family: var(--mono); font-size: 13px; text-align: right;
    color: var(--text); overflow: hidden; text-overflow: ellipsis;
    white-space: nowrap; }
  .card a.open {
    display: inline-block; margin-top: 12px; padding: 5px 14px;
    border-radius: 8px; border: 1px solid rgba(56,182,255,.4);
    color: var(--acc); text-decoration: none; font-size: 12px;
  }
  .card a.open:hover { background: rgba(56,182,255,.12); }
  footer { margin-top: 22px; color: var(--dim); font-size: 12px; }
</style>
</head>
<body>
<header>
  <div class="logo">Sdr<b>Jo</b> Cockpit</div>
  <div class="pill">sorgente: <b id="devName">&ndash;</b></div>
  <div class="pill">sample rate: <b id="devRate">&ndash;</b></div>
  <div class="freq"><span id="devFreq">&ndash;</span><small>MHz</small></div>
</header>

<div class="panel">
  <h2>Spettro &middot; Waterfall</h2>
  <canvas id="spectrum"></canvas>
  <canvas id="waterfall"></canvas>
  <div class="nosig" id="nosig" style="display:none">
    Nessuna sorgente attiva: collega la RTL-SDR o avvia un replay.
  </div>
</div>

<h2 style="margin:22px 0 0;font-size:11px;letter-spacing:1.5px;
    text-transform:uppercase;color:var(--dim)">Moduli</h2>
<div class="grid" id="cards"></div>

<footer>SdrJo &middot; ricevitore SDR modulare &middot; RTL-SDR V4 ready</footer>

<script>
"use strict";

const specCv = document.getElementById("spectrum");
const wfCv = document.getElementById("waterfall");

function sizeCanvases() {
  for (const cv of [specCv, wfCv]) {
    const w = cv.clientWidth, h = cv.clientHeight;
    if (cv.width !== w || cv.height !== h) {
      // Conserva il waterfall gia' disegnato durante il resize.
      if (cv === wfCv && cv.width) {
        const tmp = document.createElement("canvas");
        tmp.width = cv.width; tmp.height = cv.height;
        tmp.getContext("2d").drawImage(cv, 0, 0);
        cv.width = w; cv.height = h;
        cv.getContext("2d").drawImage(tmp, 0, 0, w, h);
      } else {
        cv.width = w; cv.height = h;
      }
    }
  }
}

// Palette del waterfall (nero -> blu -> ciano -> giallo -> bianco).
const wfLut = new Array(256);
for (let i = 0; i < 256; i++) {
  const t = i / 255;
  const r = Math.min(255, Math.max(0, 700 * (t - 0.45)));
  const g = Math.min(255, Math.max(0, 460 * (t - 0.25)));
  const b = Math.min(255, t < 0.5 ? 220 * t * 2 : 480 * (1 - t) + 120);
  wfLut[i] = [r | 0, g | 0, b | 0];
}

function drawSpectrum(db) {
  sizeCanvases();
  const ctx = specCv.getContext("2d");
  const w = specCv.width, h = specCv.height;
  ctx.clearRect(0, 0, w, h);
  if (!db.length) return;

  const dbMin = -110, dbMax = -10;
  const y = v => h - (Math.min(Math.max(v, dbMin), dbMax) - dbMin) /
                     (dbMax - dbMin) * h;

  // Griglia.
  ctx.strokeStyle = "rgba(120,160,200,.10)";
  ctx.beginPath();
  for (let g = dbMin + 20; g < dbMax; g += 20) {
    ctx.moveTo(0, y(g)); ctx.lineTo(w, y(g));
  }
  ctx.stroke();

  // Traccia con riempimento sfumato.
  const grad = ctx.createLinearGradient(0, 0, 0, h);
  grad.addColorStop(0, "rgba(56,182,255,.55)");
  grad.addColorStop(1, "rgba(56,182,255,.03)");
  ctx.beginPath();
  ctx.moveTo(0, h);
  for (let x = 0; x < w; x++) {
    const v = db[Math.floor(x * db.length / w)];
    ctx.lineTo(x, y(v));
  }
  ctx.lineTo(w, h);
  ctx.closePath();
  ctx.fillStyle = grad;
  ctx.fill();
  ctx.beginPath();
  for (let x = 0; x < w; x++) {
    const v = db[Math.floor(x * db.length / w)];
    x ? ctx.lineTo(x, y(v)) : ctx.moveTo(0, y(v));
  }
  ctx.strokeStyle = "#38b6ff";
  ctx.lineWidth = 1.4;
  ctx.stroke();

  // Riga nuova del waterfall.
  const wctx = wfCv.getContext("2d");
  wctx.drawImage(wfCv, 0, 0, wfCv.width, wfCv.height - 1,
                       0, 1, wfCv.width, wfCv.height - 1);
  const line = wctx.createImageData(wfCv.width, 1);
  for (let x = 0; x < wfCv.width; x++) {
    const v = db[Math.floor(x * db.length / wfCv.width)];
    const idx = Math.min(255, Math.max(0,
      Math.round((v - dbMin) / (dbMax - dbMin) * 255)));
    const [r, g, b] = wfLut[idx];
    line.data[x * 4] = r; line.data[x * 4 + 1] = g;
    line.data[x * 4 + 2] = b; line.data[x * 4 + 3] = 255;
  }
  wctx.putImageData(line, 0, 0);
}

async function pollSpectrum() {
  try {
    const d = await (await fetch("api/spectrum")).json();
    const has = d.db && d.db.length > 0;
    document.getElementById("nosig").style.display = has ? "none" : "block";
    if (has) drawSpectrum(d.db);
  } catch (e) {}
}

function fmtFreq(hz) { return (hz / 1e6).toFixed(4); }
function fmtRate(hz) {
  return hz >= 1e6 ? (hz / 1e6).toFixed(2) + " MS/s"
                   : (hz / 1e3).toFixed(0) + " kS/s";
}

async function pollStatus() {
  let d;
  try { d = await (await fetch("api/status")).json(); } catch (e) { return; }

  document.getElementById("devName").textContent = d.device.name;
  document.getElementById("devFreq").textContent = fmtFreq(d.device.freqHz);
  document.getElementById("devRate").textContent = fmtRate(d.device.rateHz);

  const cards = document.getElementById("cards");
  cards.innerHTML = d.modules.map(m => {
    const kv = Object.entries(m.status).map(([k, v]) =>
      `<div class="k">${k}</div><div class="v">${v}</div>`).join("");
    const link = m.webPort
      ? `<a class="open" href="http://${location.hostname}:${m.webPort}/"
           target="_blank">Apri interfaccia &rarr;</a>` : "";
    return `<div class="card">
      <div class="head"><div class="dot"></div><h3>${m.name}</h3></div>
      <div class="desc">${m.description}</div>
      <div class="kv">${kv}</div>${link}
    </div>`;
  }).join("") || '<div class="card"><div class="desc">Nessun modulo caricato.' +
    ' Copia i moduli nella cartella <code>modules</code>.</div></div>';
}

pollStatus(); pollSpectrum();
setInterval(pollStatus, 1500);
setInterval(pollSpectrum, 250);
</script>
</body>
</html>
)WEB";
}

} // namespace sdrjo
