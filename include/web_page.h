#pragma once

// Self-contained control page: no external CSS/JS, no filesystem upload
// needed -- it's just compiled into the firmware as a string constant.
// (ESP32 keeps const data like this in flash automatically, so there's no
// need for the PROGMEM macro you may have seen in 8-bit Arduino examples.)

static const char PAGE_HTML[] = R"WEBPAGE(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ALEX filter control</title>
<style>
  body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;background:#111;color:#eee;margin:0;padding:16px;}
  h1{font-size:1.3rem;margin:0 0 4px;}
  .sub{color:#999;font-size:0.85rem;margin-bottom:16px;}
  .panels{display:flex;gap:16px;flex-wrap:wrap;}
  .panel{background:#1c1c1c;border-radius:8px;padding:12px 16px;flex:1;min-width:260px;}
  .panel h2{font-size:1rem;margin:0 0 8px;display:flex;justify-content:space-between;align-items:center;}
  .word{font-family:monospace;color:#7ad0ee;font-size:0.85rem;}
  .row{display:flex;align-items:center;justify-content:space-between;padding:6px 0;border-bottom:1px solid #292929;}
  .row:last-child{border-bottom:none;}
  .name{font-size:0.95rem;}
  .bitno{color:#777;font-size:0.75rem;margin-left:6px;}
  .sw{position:relative;width:44px;height:24px;flex:none;}
  .sw input{opacity:0;width:0;height:0;}
  .slider{position:absolute;inset:0;background:#444;border-radius:24px;cursor:pointer;transition:.15s;}
  .slider:before{content:"";position:absolute;height:18px;width:18px;left:3px;top:3px;background:#eee;border-radius:50%;transition:.15s;}
  input:checked + .slider{background:#2a9d6f;}
  input:checked + .slider:before{transform:translateX(20px);}
  .clearbtn{background:#333;color:#eee;border:none;border-radius:4px;padding:5px 10px;font-size:0.75rem;cursor:pointer;margin-top:8px;}
  .power{margin-top:16px;background:#1c1c1c;border-radius:8px;padding:12px 16px;display:flex;gap:28px;flex-wrap:wrap;}
  .stat .val{font-size:1.3rem;font-weight:600;}
  .stat .lbl{color:#999;font-size:0.75rem;}
</style>
</head>
<body>
<h1>ALEX filter board control for Mike, G0KAD</h1>
<div class="sub">Bit names are still being verified against the physical boards -- if a name looks wrong, fix it in alex_bits.h.</div>
<div class="panels">
  <div class="panel">
    <h2>RX board <span class="word" id="rx-word">--</span></h2>
    <div id="rx-rows"></div>
    <button class="clearbtn" onclick="clearBoard('rx')">Clear all</button>
  </div>
  <div class="panel">
    <h2>TX board <span class="word" id="tx-word">--</span></h2>
    <div id="tx-rows"></div>
    <button class="clearbtn" onclick="clearBoard('tx')">Clear all</button>
  </div>
</div>
<div class="power">
  <div class="stat"><div class="val" id="fwd-w">-- W</div><div class="lbl">Forward (<span id="fwd-mv">--</span> mV)</div></div>
  <div class="stat"><div class="val" id="rev-w">-- W</div><div class="lbl">Reverse (<span id="rev-mv">--</span> mV)</div></div>
  <div class="stat"><div class="val" id="swr">--</div><div class="lbl">SWR (approx)</div></div>
</div>
<script>
function renderBoard(board, data) {
  document.getElementById(board + '-word').textContent = '0x' + data.word.toString(16).toUpperCase().padStart(4, '0');
  const rows = document.getElementById(board + '-rows');
  rows.innerHTML = '';
  data.bits.forEach(b => {
    const row = document.createElement('div');
    row.className = 'row';
    row.innerHTML = '<span class="name">' + b.name + '<span class="bitno">#' + b.bit + '</span></span>' +
      '<label class="sw"><input type="checkbox" ' + (b.on ? 'checked' : '') +
      ' onchange="setBit(\'' + board + '\',' + b.bit + ',this.checked)"><span class="slider"></span></label>';
    rows.appendChild(row);
  });
}
function renderState(data) {
  renderBoard('rx', data.rx);
  renderBoard('tx', data.tx);
}
function setBit(board, bit, on) {
  fetch('/api/set?board=' + board + '&bit=' + bit + '&val=' + (on ? 1 : 0))
    .then(r => r.json()).then(renderState);
}
function clearBoard(board) {
  fetch('/api/clear?board=' + board).then(r => r.json()).then(renderState);
}
function renderPower(p) {
  if (!p.tx_active) {
    document.getElementById('fwd-mv').textContent = '--';
    document.getElementById('rev-mv').textContent = '--';
    document.getElementById('fwd-w').textContent = 'RX';
    document.getElementById('rev-w').textContent = '(not transmitting)';
    document.getElementById('swr').textContent = '--';
    return;
  }
  document.getElementById('fwd-mv').textContent = p.fwd_mv;
  document.getElementById('rev-mv').textContent = p.rev_mv;
  document.getElementById('fwd-w').textContent = p.fwd_w.toFixed(2) + ' W';
  document.getElementById('rev-w').textContent = p.rev_w.toFixed(3) + ' W';
  document.getElementById('swr').textContent = (p.swr === null) ? '--' : p.swr.toFixed(2) + ':1';
}
function pollPower() {
  fetch('/api/power').then(r => r.json()).then(renderPower).catch(() => {});
}
fetch('/api/state').then(r => r.json()).then(renderState);
pollPower();
setInterval(pollPower, 1000);
</script>
</body>
</html>
)WEBPAGE";
