//
// Pagina web della mappa ADS-B (tutto in un file: HTML + CSS + JS).
// Leaflet e i tile arrivano da CDN: serve la connessione internet nel
// browser, ma il server locale non dipende da nulla.
//
#include "sdrjo/adsb/adsb_server.hpp"

namespace sdrjo::adsb {

const char* mapPageHtml()
{
    return R"WEB(<!DOCTYPE html>
<html lang="it">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SdrJo &middot; ADS-B</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<style>
  :root {
    --bg: #101418; --panel: #171d24; --line: #26303a;
    --text: #d5dee7; --dim: #7d8b99; --accent: #46b7ff; --warn: #ffb454;
  }
  * { box-sizing: border-box; margin: 0; }
  html, body { height: 100%; }
  body {
    display: flex; flex-direction: column;
    font: 13px/1.45 "Segoe UI", system-ui, sans-serif;
    background: var(--bg); color: var(--text);
  }
  header {
    display: flex; align-items: baseline; gap: 18px; flex-wrap: wrap;
    padding: 8px 14px; background: var(--panel);
    border-bottom: 1px solid var(--line);
  }
  header h1 { font-size: 15px; font-weight: 600; }
  header h1 span { color: var(--accent); }
  .stat { color: var(--dim); }
  .stat b { color: var(--text); font-weight: 600; }
  main { flex: 1; display: flex; min-height: 0; }
  #map { flex: 1; background: #0b0e12; }
  aside {
    width: 380px; overflow-y: auto; background: var(--panel);
    border-left: 1px solid var(--line);
  }
  table { width: 100%; border-collapse: collapse; }
  th, td { padding: 5px 8px; text-align: right; white-space: nowrap; }
  th:first-child, td:first-child { text-align: left; }
  th {
    position: sticky; top: 0; background: var(--panel); color: var(--dim);
    font-weight: 600; font-size: 11px; text-transform: uppercase;
    border-bottom: 1px solid var(--line);
  }
  tbody tr { cursor: pointer; border-bottom: 1px solid var(--line); }
  tbody tr:hover { background: #1d2530; }
  tbody tr.sel { background: #21354a; }
  td.cs { color: var(--accent); font-weight: 600; }
  td.stale { color: var(--dim); }
  .plane-icon svg { filter: drop-shadow(0 0 2px rgba(0,0,0,.8)); }
  .leaflet-container { font: inherit; }
  .empty { padding: 18px; color: var(--dim); text-align: center; }
</style>
</head>
<body>
<header>
  <h1>Sdr<span>Jo</span> &middot; ADS-B</h1>
  <div class="stat">aerei: <b id="stAc">0</b></div>
  <div class="stat">con posizione: <b id="stPos">0</b></div>
  <div class="stat">messaggi: <b id="stMsg">0</b></div>
  <div class="stat">msg/s: <b id="stRate">0</b></div>
  <div class="stat">portata max: <b id="stRange">&ndash;</b></div>
</header>
<main>
  <div id="map"></div>
  <aside>
    <table>
      <thead><tr>
        <th>Volo</th><th>Quota ft</th><th>kt</th><th>Rotta</th>
        <th>km</th><th>Msg</th><th>s</th>
      </tr></thead>
      <tbody id="rows"></tbody>
    </table>
    <div class="empty" id="empty">In attesa di messaggi ADS-B&hellip;</div>
  </aside>
</main>
<script>
"use strict";

// La mappa richiede Leaflet dal CDN: se non e' raggiungibile (uso offline),
// tabella e statistiche continuano a funzionare comunque.
let map = null;
if (typeof L !== "undefined") {
  map = L.map("map", { zoomControl: true }).setView([45.0, 9.0], 6);
  L.tileLayer("https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png", {
    maxZoom: 12,
    attribution: "&copy; OpenStreetMap &middot; &copy; CARTO"
  }).addTo(map);
} else {
  document.getElementById("map").innerHTML =
    '<div class="empty" style="padding-top:40vh">Mappa non disponibile ' +
    '(CDN Leaflet non raggiungibile) &mdash; la tabella resta attiva.</div>';
}

const planeSvg = (deg, sel) =>
  `<svg width="30" height="30" viewBox="0 0 24 24" style="transform:rotate(${deg}deg)">
     <path fill="${sel ? "#ffb454" : "#46b7ff"}" stroke="#0b0e12" stroke-width=".6"
       d="M12 1.5c.5 0 .9.6.9 1.4l.1 6.1 8.5 5v2l-8.5-2.6-.1 5.4 2.3 1.7v1.6L12 21.3l-3.2.8v-1.6l2.3-1.7-.1-5.4L2.5 16v-2l8.5-5 .1-6.1c0-.8.4-1.4.9-1.4z"/>
   </svg>`;

const markers = new Map();   // icao -> L.marker
let trailLine = null;
let antennaDrawn = false;
let selected = null;
let centered = false;
let lastMsg = 0, lastT = Date.now(), rate = 0, maxRangeKm = 0;

function fmt(v, dash) { return (v === undefined || v === null) ? (dash || "") : v; }

function selectAircraft(icao) {
  selected = (selected === icao) ? null : icao;
  refresh();
}

async function refresh() {
  let d;
  try {
    d = await (await fetch("data/aircraft.json")).json();
  } catch (e) { return; }

  // Statistiche di intestazione.
  const now = Date.now();
  if (now - lastT >= 2000) {
    rate = Math.max(0, (d.messages - lastMsg) / ((now - lastT) / 1000));
    lastMsg = d.messages; lastT = now;
  }
  const withPos = d.aircraft.filter(a => a.lat !== undefined);
  document.getElementById("stAc").textContent = d.aircraft.length;
  document.getElementById("stPos").textContent = withPos.length;
  document.getElementById("stMsg").textContent = d.messages;
  document.getElementById("stRate").textContent = rate.toFixed(0);
  for (const a of withPos)
    if (a.distKm !== undefined && a.distKm > maxRangeKm) maxRangeKm = a.distKm;
  document.getElementById("stRange").textContent =
    maxRangeKm > 0 ? maxRangeKm.toFixed(0) + " km" : "–";

  // Antenna: marker + cerchi di portata (50..250 km).
  if (map && d.antenna && !antennaDrawn) {
    antennaDrawn = true;
    const a = d.antenna;
    L.circleMarker([a.lat, a.lon], { radius: 5, color: "#ffb454",
      fillColor: "#ffb454", fillOpacity: 1 }).addTo(map).bindTooltip("Antenna");
    for (const r of [50, 100, 150, 200, 250]) {
      L.circle([a.lat, a.lon], { radius: r * 1000, color: "#3a4653",
        weight: 1, fill: false, dashArray: "4 6" }).addTo(map);
    }
    if (!centered) { map.setView([a.lat, a.lon], 8); centered = true; }
  }

  // Marker degli aerei.
  const seen = new Set();
  for (const a of map ? withPos : []) {
    seen.add(a.icao);
    const sel = a.icao === selected;
    const icon = L.divIcon({ className: "plane-icon",
      html: planeSvg(a.trackDeg || 0, sel), iconSize: [30, 30],
      iconAnchor: [15, 15] });
    const label = (a.callsign || a.icao) +
      (a.altFt !== undefined ? " · " + a.altFt + " ft" : "");
    let m = markers.get(a.icao);
    if (!m) {
      m = L.marker([a.lat, a.lon], { icon }).addTo(map).bindTooltip(label);
      m.on("click", () => selectAircraft(a.icao));
      markers.set(a.icao, m);
    } else {
      m.setLatLng([a.lat, a.lon]);
      m.setIcon(icon);
      m.setTooltipContent(label);
    }
    if (!centered && !d.antenna) { map.setView([a.lat, a.lon], 8); centered = true; }
  }
  for (const [icao, m] of markers) {
    if (!seen.has(icao)) { map.removeLayer(m); markers.delete(icao); }
  }

  // Scia dell'aereo selezionato.
  if (trailLine) { map.removeLayer(trailLine); trailLine = null; }
  const selAc = withPos.find(a => a.icao === selected);
  if (map && selAc && selAc.trail) {
    trailLine = L.polyline(selAc.trail, { color: "#ffb454", weight: 2,
      opacity: .8 }).addTo(map);
  }

  // Tabella voli, ordinata per distanza (poi per messaggi).
  const rows = document.getElementById("rows");
  document.getElementById("empty").style.display =
    d.aircraft.length ? "none" : "block";
  d.aircraft.sort((x, y) =>
    (x.distKm ?? 1e9) - (y.distKm ?? 1e9) || y.msgs - x.msgs);
  rows.innerHTML = d.aircraft.map(a => `
    <tr data-icao="${a.icao}" class="${a.icao === selected ? "sel" : ""}">
      <td class="cs">${a.callsign || "<i>" + a.icao + "</i>"}</td>
      <td>${fmt(a.altFt)}</td>
      <td>${fmt(a.gsKt)}</td>
      <td>${a.trackDeg !== undefined ? a.trackDeg + "°" : ""}</td>
      <td>${a.distKm !== undefined ? a.distKm.toFixed(0) : ""}</td>
      <td>${a.msgs}</td>
      <td class="${a.seen > 20 ? "stale" : ""}">${a.seen.toFixed(0)}</td>
    </tr>`).join("");
  for (const tr of rows.querySelectorAll("tr")) {
    tr.onclick = () => {
      const icao = tr.dataset.icao;
      selectAircraft(icao);
      const m = markers.get(icao);
      if (map && m) map.panTo(m.getLatLng());
    };
  }
}

refresh();
setInterval(refresh, 1000);
</script>
</body>
</html>
)WEB";
}

} // namespace sdrjo::adsb
