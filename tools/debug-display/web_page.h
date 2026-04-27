#ifndef WEB_PAGE_H
#define WEB_PAGE_H

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Occupancy Counter</title>
<style>
:root{
  --bg:#e8efef;--panel:#f4f8f8;--border:#c8d8d8;
  --teal:#00897b;--teal-dim:#d4efed;--teal-glow:rgba(0,137,123,.18);
  --exit:#d84315;--exit-dim:#fbe4de;
  --text:#1a2c2c;--muted:#5e7c7c;--faint:#b0c4c4;
  --card:#fff;--grid-empty:#d0dada;
  --mono:'SF Mono','Cascadia Code','Fira Code',monospace;
  --sans:'SF Pro Display','Inter',system-ui,sans-serif;
}
*{margin:0;padding:0;box-sizing:border-box}
html{font-size:16px}
body{font-family:var(--sans);background:var(--bg);color:var(--text);
  min-height:100dvh;overflow-x:hidden}

.app{display:flex;min-height:100dvh}
.main{flex:1;display:flex;flex-direction:column;align-items:center;
  padding:24px 16px;min-width:0}
.sidebar{width:280px;background:var(--card);border-left:1px solid var(--border);
  display:flex;flex-direction:column;overflow:hidden}

/* ── Header ── */
.hdr{display:flex;align-items:center;gap:10px;margin-bottom:24px}
.hdr-marcak{width:3px;height:28px;background:var(--teal);border-radius:1px}
h1{font-size:1rem;font-weight:500;letter-spacing:.18em;text-transform:uppercase;color:var(--muted)}

/* ── Big number ── */
.hero{text-align:center;margin-bottom:24px;background:var(--card);
  border-radius:12px;padding:28px 48px;border:1px solid var(--border);
  box-shadow:0 2px 12px rgba(0,0,0,.06)}
.hero-num{font-family:var(--mono);font-size:5.5rem;font-weight:200;
  color:var(--text);line-height:1;letter-spacing:-.04em}
.hero-label{font-size:.7rem;letter-spacing:.2em;text-transform:uppercase;
  color:var(--muted);margin-top:6px}

/* ── IO counters ── */
.io{display:flex;gap:2px;margin-bottom:20px;width:min(80vw,320px)}
.io-card{flex:1;background:var(--card);padding:14px 0;text-align:center;
  border:1px solid var(--border)}
.io-card:first-child{border-radius:8px 0 0 8px}
.io-card:last-child{border-radius:0 8px 8px 0}
.io-val{font-family:var(--mono);font-size:1.6rem;font-weight:400}
.io-lbl{font-size:.55rem;letter-spacing:.18em;text-transform:uppercase;color:var(--muted);margin-top:2px}
.io-in .io-val{color:var(--teal)}
.io-out .io-val{color:var(--exit)}

/* ── Tracking status ── */
.track{margin-bottom:16px;font-family:var(--mono);font-size:.72rem;text-align:center;
  min-height:1.8em;color:var(--muted)}
.track b{color:var(--text);font-weight:500}
.track .arr{font-weight:700;font-size:.9rem}
.arr-in{color:var(--teal)}
.arr-out{color:var(--exit)}

/* ── Sensor grid ── */
.grid-wrap{position:relative;margin-bottom:12px}
.grid{display:grid;grid-template-columns:repeat(8,1fr);gap:2px;
  width:min(78vw,300px);aspect-ratio:1}
.cell{border-radius:2px;transition:background .1s,box-shadow .1s;
  display:flex;align-items:center;justify-content:center;
  font-family:var(--mono);font-size:.5rem;color:rgba(0,0,0,.3)}
.cell.occ{box-shadow:inset 0 0 0 2px var(--teal),0 0 10px var(--teal-glow);z-index:1}
.cent-line{position:absolute;left:0;right:0;height:1.5px;pointer-events:none;z-index:3;
  transition:top .1s;opacity:.7}
.cent-enter{background:var(--teal)}
.cent-now{background:#e6a000}

/* ── Legend ── */
.legend{display:flex;align-items:center;gap:8px;margin-bottom:20px;
  font-size:.55rem;letter-spacing:.08em;color:var(--muted)}
.legend-bar{width:100px;height:6px;border-radius:3px;
  background:linear-gradient(90deg,var(--teal),#6db5ad,var(--grid-empty))}
.legend-ln{width:14px;height:2px;display:inline-block;vertical-align:middle}
.ln-e{background:var(--teal)}.ln-n{background:#e6a000}

/* ── Controls ── */
.ctrl{display:flex;flex-wrap:wrap;gap:8px;align-items:center;justify-content:center;margin-bottom:16px}
.btn{font-family:var(--sans);background:var(--card);color:var(--text);
  border:1px solid var(--border);border-radius:4px;
  padding:7px 16px;cursor:pointer;font-size:.7rem;letter-spacing:.08em;
  text-transform:uppercase;transition:all .2s;box-shadow:0 1px 3px rgba(0,0,0,.06)}
.btn:hover{color:var(--teal);border-color:var(--teal)}
.btn.danger{color:var(--exit);border-color:#e8a99a}
.btn.danger:hover{border-color:var(--exit);background:var(--exit-dim)}
.range-wrap{display:flex;align-items:center;gap:6px}
.range-wrap label{font-size:.6rem;letter-spacing:.1em;color:var(--muted);text-transform:uppercase}
input[type=range]{width:90px;accent-color:var(--teal);height:2px}
.range-val{font-family:var(--mono);font-size:.7rem;color:var(--text);min-width:50px}
input[type=range]::-webkit-slider-runnable-track{background:var(--border);border-radius:2px}

/* ── Status bar ── */
.sbar{display:flex;gap:14px;flex-wrap:wrap;justify-content:center;
  font-size:.6rem;color:var(--muted);letter-spacing:.06em}
.sbar span{display:flex;align-items:center;gap:4px}
.dot{width:5px;height:5px;border-radius:50%}
.dot.ok{background:var(--teal)}.dot.err{background:var(--exit)}.dot.warn{background:#c89b00}

/* ── Sidebar / Log ── */
.log-hdr{padding:16px 16px 12px;border-bottom:1px solid var(--border);
  font-size:.6rem;letter-spacing:.18em;text-transform:uppercase;color:var(--muted);
  display:flex;align-items:center;gap:8px}
.log-hdr::before{content:'';width:3px;height:14px;background:var(--teal);border-radius:1px}
.log-list{flex:1;overflow-y:auto;padding:8px 0;-webkit-overflow-scrolling:touch}
.log-item{display:flex;align-items:center;padding:7px 16px;gap:10px;
  font-family:var(--mono);font-size:.65rem;border-bottom:1px solid var(--border)}
.log-time{color:var(--muted);min-width:65px}
.log-tag{padding:2px 6px;border-radius:2px;font-size:.55rem;letter-spacing:.1em;
  font-weight:600;min-width:42px;text-align:center}
.log-tag.in{background:var(--teal-dim);color:var(--teal)}
.log-tag.out{background:var(--exit-dim);color:var(--exit)}
.log-n{color:var(--text);margin-left:auto}
.log-empty{padding:24px 16px;text-align:center;color:var(--faint);
  font-size:.65rem;letter-spacing:.1em}

/* ── Responsive ── */
@media(max-width:680px){
  .app{flex-direction:column}
  .sidebar{width:100%;border-left:none;border-top:1px solid var(--border);
    max-height:240px;flex-shrink:0}
  .main{padding:16px 12px}
  .hero{padding:20px 32px}
  .hero-num{font-size:4rem}
  .grid{width:min(82vw,300px)}
  .io{width:min(82vw,300px)}
}
</style>
</head>
<body>
<div class="app">
<div class="main">

  <div class="hdr"><div class="hdr-mark"></div><h1>Occupancy Counter</h1></div>

  <div class="hero">
    <div class="hero-num" id="occ">0</div>
    <div class="hero-label">current occupancy</div>
  </div>

  <div class="io">
    <div class="io-card io-in"><div class="io-val" id="vin">0</div><div class="io-lbl">Entries</div></div>
    <div class="io-card io-out"><div class="io-val" id="vout">0</div><div class="io-lbl">Exits</div></div>
  </div>

  <div class="track" id="trackInfo">Idle</div>

  <div class="grid-wrap">
    <div class="grid" id="grid"></div>
    <div class="cent-line cent-enter" id="enterLine" style="display:none"></div>
    <div class="cent-line cent-now" id="nowLine" style="display:none"></div>
  </div>

  <div class="legend">
    <span>Near</span><div class="legend-bar"></div><span>Far</span>
    <span class="legend-ln ln-e"></span><span>enter</span>
    <span class="legend-ln ln-n"></span><span>now</span>
  </div>

  <div class="ctrl">
    <button class="btn danger" onclick="doReset()">Reset</button>
    <button class="btn" onclick="doFlip()">Flip Direction</button>
    <div class="range-wrap">
      <label>Ceiling</label>
      <input type="range" id="ceilSlider" min="300" max="1500" step="25" value="1000"
             oninput="ceilChanged(this.value)">
      <span class="range-val" id="ceilVal">1000 mm</span>
    </div>
  </div>

  <div class="sbar">
    <span><span class="dot" id="dotSensor"></span> <span id="sSensor">--</span></span>
    <span><span class="dot" id="dotWifi"></span> <span id="sWifi">--</span></span>
    <span>FPS <b id="sFps">0</b></span>
    <span>Cells <b id="sOcc">0</b></span>
  </div>
</div>

<div class="sidebar">
  <div class="log-hdr">Event Log</div>
  <div class="log-list" id="logList">
    <div class="log-empty">No events recorded</div>
  </div>
</div>
</div>

<script>
const GRID_SIZE = 8;
const ZONE_COUNT = GRID_SIZE * GRID_SIZE;

const gridEl = document.getElementById('grid');
const cellEls = [];
for (let i = 0; i < ZONE_COUNT; i++) {
  const div = document.createElement('div');
  div.className = 'cell';
  gridEl.appendChild(div);
  cellEls.push(div);
}

const enterLine = document.getElementById('enterLine');
const nowLine = document.getElementById('nowLine');
const logList = document.getElementById('logList');

function rowToY(row) {
  return (row + 0.5) / GRID_SIZE * gridEl.offsetHeight;
}

function distanceToColor(dist) {
  if (dist < 0) return '#d0dada';
  const ratio = Math.max(0, Math.min(dist / 1200, 1));
  const hue = 170 - ratio * 20;
  const sat = 65 - ratio * 30;
  const lgt = 35 + ratio * 42;
  return `hsl(${hue},${sat}%,${lgt}%)`;
}

let lastFrameCount = 0, lastFpsTime = Date.now();
let prevEntries = 0, prevExits = 0;
const MAX_LOG_ITEMS = 80;

function formatTime() {
  const now = new Date();
  let hrs = now.getHours(), min = now.getMinutes(), sec = now.getSeconds();
  const ampm = hrs >= 12 ? 'PM' : 'AM';
  hrs = hrs % 12 || 12;
  return (hrs < 10 ? ' ' : '') + hrs + ':' +
         (min < 10 ? '0' : '') + min + ':' +
         (sec < 10 ? '0' : '') + sec + ' ' + ampm;
}

function addLogEntry(type, count) {
  const empty = logList.querySelector('.log-empty');
  if (empty) empty.remove();
  const el = document.createElement('div');
  el.className = 'log-item';
  el.innerHTML =
    '<span class="log-time">' + formatTime() + '</span>' +
    '<span class="log-tag ' + (type === 'in' ? 'in' : 'out') + '">' +
      (type === 'in' ? 'ENTRY' : 'EXIT') + '</span>' +
    '<span class="log-n">#' + count + '</span>';
  logList.prepend(el);
  while (logList.children.length > MAX_LOG_ITEMS) logList.lastChild.remove();
}

async function pollData() {
  try {
    const response = await fetch('/data');
    if (!response.ok) throw 0;
    const data = await response.json();

    document.getElementById('occ').textContent = data.occupancy;
    document.getElementById('vin').textContent = data.entries;
    document.getElementById('vout').textContent = data.exits;
    document.getElementById('sOcc').textContent = data.occ_n;

    if (data.entries > prevEntries) {
      for (let i = prevEntries + 1; i <= data.entries; i++) addLogEntry('in', i);
    }
    if (data.exits > prevExits) {
      for (let i = prevExits + 1; i <= data.exits; i++) addLogEntry('out', i);
    }
    prevEntries = data.entries;
    prevExits = data.exits;

    const now = Date.now();
    if (now - lastFpsTime >= 1000) {
      document.getElementById('sFps').textContent = data.frames - lastFrameCount;
      lastFrameCount = data.frames;
      lastFpsTime = now;
    }

    for (let i = 0; i < ZONE_COUNT; i++) {
      const dist = data.distances[i];
      cellEls[i].style.background = distanceToColor(dist);
      cellEls[i].className = data.occupied[i] ? 'cell occ' : 'cell';
      cellEls[i].textContent = dist >= 0 ? Math.round(dist / 10) : '';
    }

    const trackInfo = document.getElementById('trackInfo');
    if (!data.cal) {
      trackInfo.textContent = 'Calibrating \u2014 keep doorway clear\u2026';
      enterLine.style.display = 'none';
      nowLine.style.display = 'none';
    } else if (data.tracking) {
      const shift = data.cur_row - data.enter_row;
      const dir = shift > 0.3 ? 'down' : shift < -0.3 ? 'up' : '';
      const arrow = dir === 'down' ? '\u2193' : dir === 'up' ? '\u2191' : '\u00b7';
      const arrowClass = dir === 'down' ? 'arr-in' : dir === 'up' ? 'arr-out' : '';
      const trackLabel = data.num_tracks > 1 ? ' (' + data.num_tracks + ' tracks)' : '';
      trackInfo.innerHTML =
        '<span class="arr ' + arrowClass + '">' + arrow + '</span> Tracking' + trackLabel +
        ' &nbsp; enter <b>' + data.enter_row.toFixed(1) + '</b>' +
        ' &nbsp; now <b>' + data.cur_row.toFixed(1) + '</b>' +
        ' &nbsp; shift <b>' + (shift >= 0 ? '+' : '') + shift.toFixed(1) + '</b>';
      enterLine.style.display = '';
      enterLine.style.top = rowToY(data.enter_row) + 'px';
      nowLine.style.display = '';
      nowLine.style.top = rowToY(data.cur_row) + 'px';
    } else {
      trackInfo.textContent = 'Idle';
      enterLine.style.display = 'none';
      nowLine.style.display = 'none';
    }

    if (data.ceiling) {
      document.getElementById('ceilSlider').value = data.ceiling;
      document.getElementById('ceilVal').textContent = data.ceiling + ' mm';
    }

    document.getElementById('dotSensor').className = 'dot ' + (data.cal ? 'ok' : 'warn');
    document.getElementById('sSensor').textContent = data.cal ? 'Ready' : 'Cal\u2026';
    document.getElementById('dotWifi').className = 'dot ok';
    document.getElementById('sWifi').textContent = 'Connected';
  } catch (e) {
    document.getElementById('dotWifi').className = 'dot err';
    document.getElementById('sWifi').textContent = 'Error';
  }
}

function doReset() {
  fetch('/reset');
  prevEntries = 0;
  prevExits = 0;
  logList.innerHTML = '<div class="log-empty">No events recorded</div>';
}
function doFlip() { fetch('/flip'); }
function ceilChanged(value) {
  document.getElementById('ceilVal').textContent = value + ' mm';
  fetch('/config?ceiling=' + value);
}

setInterval(pollData, 200);
pollData();
</script>
</body>
</html>
)rawliteral";

#endif // WEB_PAGE_H
