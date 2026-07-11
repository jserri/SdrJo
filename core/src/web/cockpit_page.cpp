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
  #bands { height: 22px; }
  #spectrum { height: 170px; cursor: crosshair; }
  #waterfall { height: 190px; border-top: 1px solid var(--line); }
  .nosig { padding: 26px; text-align: center; color: var(--dim); }

  .rxbar {
    display: flex; align-items: center; gap: 10px; flex-wrap: wrap;
    padding: 10px 14px; border-top: 1px solid var(--line);
  }
  .rxbar .vfo { font-family: var(--mono); font-size: 17px; color: var(--warn); }
  .chip {
    padding: 4px 12px; border-radius: 999px; border: 1px solid var(--line);
    background: rgba(255,255,255,.03); color: var(--dim); cursor: pointer;
    font-size: 12px; user-select: none;
  }
  .chip:hover { border-color: rgba(56,182,255,.5); color: var(--text); }
  .chip.on { background: rgba(56,182,255,.18); color: var(--acc);
    border-color: rgba(56,182,255,.6); }
  .playbtn {
    margin-left: auto; padding: 6px 18px; border-radius: 8px; cursor: pointer;
    border: 1px solid rgba(61,220,151,.5); color: var(--ok);
    background: rgba(61,220,151,.08); font-size: 13px; user-select: none;
  }
  .playbtn.playing { border-color: rgba(244,112,103,.6); color: #f47067;
    background: rgba(244,112,103,.08); }

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
  <canvas id="bands"></canvas>
  <canvas id="spectrum"></canvas>
  <canvas id="waterfall"></canvas>
  <div class="rxbar">
    <span class="vfo" id="vfoFreq">&ndash;</span>
    <span id="modeChips"></span>
    <span class="playbtn" id="playBtn">&#9654; Ascolta</span>
  </div>
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
const bandCv = document.getElementById("bands");

let device = null;   // {freqHz, rateHz}
let vfo = null;      // {freqHz, mode}
let bands = [];
const modes = ["Spento", "WFM stereo", "NFM", "AM", "USB", "LSB"];

async function control(params) {
  try { await fetch("api/control?" + params); } catch (e) {}
  pollStatus();
}

function renderChips() {
  const el = document.getElementById("modeChips");
  el.innerHTML = modes.map(m =>
    `<span class="chip ${vfo && vfo.mode === m ? "on" : ""}"
       data-mode="${m}">${m}</span>`).join(" ");
  for (const c of el.querySelectorAll(".chip"))
    c.onclick = () => control("mode=" + encodeURIComponent(c.dataset.mode));
}

// Ascolto nel browser: prima si prova il WebSocket a bassa latenza
// (blocchi ADPCM da 20 ms, 4:1 di banda), altrimenti WAV classico.
let player = null, ws = null, actx = null, playT = 0;
const playBtn = document.getElementById("playBtn");

const ADPCM_STEP = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,
  45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,
  307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,
  1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,
  5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,
  20350,22385,24623,27086,29794,32767];
const ADPCM_IDX = [-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8];

function adpcmDecode(u8) {
  if (u8.length < 5) return new Float32Array(0);
  let pred = u8[0] | (u8[1] << 8);
  if (pred & 0x8000) pred -= 0x10000;
  let idx = Math.min(88, u8[2]);
  const out = new Float32Array((u8.length - 4) * 2);
  let o = 0;
  for (let i = 4; i < u8.length; i++) {
    for (const code of [u8[i] & 15, u8[i] >> 4]) {
      const step = ADPCM_STEP[idx];
      let delta = step >> 3;
      if (code & 4) delta += step;
      if (code & 2) delta += step >> 1;
      if (code & 1) delta += step >> 2;
      pred += (code & 8) ? -delta : delta;
      pred = Math.max(-32768, Math.min(32767, pred));
      idx = Math.max(0, Math.min(88, idx + ADPCM_IDX[code]));
      out[o++] = pred / 32767;
    }
  }
  return out;
}

function stopAudio() {
  if (ws) { const w = ws; ws = null; w.close(); }
  if (actx) { actx.close(); actx = null; }
  if (player) { player.pause(); player.src = ""; player = null; }
  playBtn.classList.remove("playing");
  playBtn.innerHTML = "&#9654; Ascolta";
}

async function startWsAudio() {
  const r = await fetch("api/wsinfo");
  if (!r.ok) throw new Error("no wsinfo");
  const info = await r.json();
  if (!info.port) throw new Error("ws spento");
  actx = new (window.AudioContext || window.webkitAudioContext)(
      {sampleRate: 48000});
  playT = 0;
  const proto = location.protocol === "https:" ? "wss" : "ws";
  const sock = new WebSocket(
      proto + "://" + location.hostname + ":" + info.port +
      "/?token=" + encodeURIComponent(info.token));
  sock.binaryType = "arraybuffer";
  await new Promise((res, rej) => {
    sock.onopen = res;
    sock.onerror = rej;
    setTimeout(rej, 3000);
  });
  ws = sock;
  sock.onmessage = (ev) => {
    if (!actx) return;
    const pcm = adpcmDecode(new Uint8Array(ev.data));
    if (!pcm.length) return;
    const buf = actx.createBuffer(1, pcm.length, 48000);
    buf.getChannelData(0).set(pcm);
    const src = actx.createBufferSource();
    src.buffer = buf;
    src.connect(actx.destination);
    const now = actx.currentTime;
    if (playT < now + 0.04) playT = now + 0.08; // riaggancio dolce
    src.start(playT);
    playT += buf.duration;
  };
  sock.onclose = () => { if (ws === sock) stopAudio(); };
  playBtn.classList.add("playing");
  playBtn.innerHTML = "&#9632; Stop (bassa latenza)";
}

function startWavAudio() {
  player = new Audio("api/audio.wav?t=" + Date.now());
  player.play().catch(() => {});
  playBtn.classList.add("playing");
  playBtn.innerHTML = "&#9632; Stop";
}

playBtn.onclick = async () => {
  if (player || ws) { stopAudio(); return; }
  try { await startWsAudio(); }
  catch (e) { if (actx) { actx.close(); actx = null; } startWavAudio(); }
};
const bandColors = { ham: "#3ddc97", bc: "#38b6ff", aero: "#ffb454",
  sat: "#c792ea", marine: "#4dd0e1", ism: "#f47067", cb: "#ffd166",
  pmr: "#f47067", nav: "#9e9e9e" };

function sizeCanvases() {
  for (const cv of [specCv, wfCv, bandCv]) {
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

function drawBands() {
  const ctx = bandCv.getContext("2d");
  const w = bandCv.width, h = bandCv.height;
  ctx.clearRect(0, 0, w, h);
  if (!device || !device.rateHz || !bands.length) return;
  const f0 = device.freqHz - device.rateHz / 2;
  const f1 = device.freqHz + device.rateHz / 2;
  ctx.font = "10px sans-serif";
  ctx.textBaseline = "middle";
  for (const b of bands) {
    const x0 = Math.max(0, (b.low - f0) / (f1 - f0) * w);
    const x1 = Math.min(w, (b.high - f0) / (f1 - f0) * w);
    const col = bandColors[b.cat] || "#888";
    ctx.fillStyle = col + "44";
    ctx.fillRect(x0, 2, x1 - x0, h - 4);
    ctx.fillStyle = col;
    ctx.fillRect(x0, 2, 2, h - 4);
    if (x1 - x0 > 60) {
      ctx.fillText(b.name, x0 + 6, h / 2);
    }
  }
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

  // Etichette di frequenza sulla griglia verticale.
  if (device && device.rateHz) {
    const f0 = device.freqHz - device.rateHz / 2;
    ctx.fillStyle = "rgba(160,190,220,.55)";
    ctx.font = "10px monospace";
    const ticks = 8;
    ctx.strokeStyle = "rgba(120,160,200,.08)";
    ctx.beginPath();
    for (let k = 1; k < ticks; k++) {
      const x = k * w / ticks;
      ctx.moveTo(x, 0); ctx.lineTo(x, h);
      const f = (f0 + device.rateHz * k / ticks) / 1e6;
      ctx.fillText(f.toFixed(3) + " MHz", x + 4, h - 6);
    }
    ctx.stroke();
  }

  // Marker del VFO di ascolto.
  if (vfo && vfo.freqHz > 0 && device && device.rateHz) {
    const f0v = device.freqHz - device.rateHz / 2;
    const xv = (vfo.freqHz - f0v) / device.rateHz * w;
    if (xv >= 0 && xv <= w) {
      ctx.strokeStyle = "rgba(255,180,84,.9)";
      ctx.lineWidth = 1.5;
      ctx.beginPath(); ctx.moveTo(xv, 0); ctx.lineTo(xv, h); ctx.stroke();
      ctx.lineWidth = 1;
    }
  }

  // Riga nuova del waterfall (solo su dati nuovi).
  if (!drawSpectrum.newRow) return;
  drawSpectrum.newRow = false;
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

// Spettro fluido: i dati arrivano a ~5 Hz ma il disegno gira a 60 fps
// interpolando verso l'ultimo frame ricevuto.
let specTarget = null, specShown = null;

async function pollSpectrum() {
  try {
    const d = await (await fetch("api/spectrum")).json();
    const has = d.db && d.db.length > 0;
    document.getElementById("nosig").style.display = has ? "none" : "block";
    if (has) {
      specTarget = d.db;
      drawSpectrum.newRow = true; // una riga di waterfall per fetch
    }
  } catch (e) {}
}

function animate() {
  if (specTarget) {
    if (!specShown || specShown.length !== specTarget.length) {
      specShown = specTarget.slice();
    } else {
      for (let i = 0; i < specShown.length; i++)
        specShown[i] += (specTarget[i] - specShown[i]) * 0.35;
    }
    drawSpectrum(specShown);
  }
  requestAnimationFrame(animate);
}
requestAnimationFrame(animate);

// Frequenza sotto il cursore + click per sintonizzare.
specCv.title = "";
specCv.addEventListener("mousemove", e => {
  if (!device || !device.rateHz) return;
  const r = specCv.getBoundingClientRect();
  const f = device.freqHz - device.rateHz / 2 +
            (e.clientX - r.left) / r.width * device.rateHz;
  specCv.title = (f / 1e6).toFixed(4) + " MHz - click per sintonizzare";
});
specCv.addEventListener("click", e => {
  if (!device || !device.rateHz) return;
  const r = specCv.getBoundingClientRect();
  const f = device.freqHz - device.rateHz / 2 +
            (e.clientX - r.left) / r.width * device.rateHz;
  control("freq=" + Math.round(f));
});

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
  device = d.device;
  bands = d.bands || [];
  vfo = d.vfo || null;
  document.getElementById("vfoFreq").textContent =
    vfo && vfo.freqHz > 0 ? (vfo.freqHz / 1e6).toFixed(4) + " MHz" : "-";
  renderChips();
  drawBands();

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
setInterval(pollSpectrum, 200);
</script>
</body>
</html>
)WEB";
}

} // namespace sdrjo
