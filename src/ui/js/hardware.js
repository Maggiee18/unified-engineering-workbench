// Hardware view: drives the C++ mock device adapters (clearly simulated).
import { api, fmt, escapeHtml, log as appLog } from './common.js';

let devices = [];
let pollTimer = null;
const latest = {};
const busy = new Set();

function hwLog(message, level = 'i') {
  const el = document.getElementById('hw-log');
  const t = new Date().toISOString().substring(11, 23);
  const line = document.createElement('div');
  line.innerHTML = `<span class="t">${t}</span> <span class="${level}"></span>`;
  line.lastChild.textContent = message;
  el.appendChild(line);
  el.scrollTop = el.scrollHeight;
  appLog(message, level);
}

const KIND_LABEL = { microcontroller: 'Microcontroller board', fpga: 'FPGA board', jtag_debugger: 'JTAG debug adapter' };

function formatField(f) {
  if (f.unit === 'hex') return '0x' + Math.round(f.value).toString(16).toUpperCase().padStart(8, '0');
  if (f.unit === 'bool') return f.value ? 'true' : 'false';
  if (f.unit === 'count') return String(Math.round(f.value));
  return `${fmt(f.value, 5)} ${f.unit}`;
}

function card(dev) {
  const d = dev.descriptor;
  const t = latest[d.id];
  const state = dev.state;
  const isBusy = busy.has(d.id);
  const tele = t ? `<table class="data"><tbody>${t.fields.map(f => `<tr><td class="txt">${escapeHtml(f.name)}</td><td>${escapeHtml(formatField(f))}</td></tr>`).join('')}
      <tr><td class="txt muted">sequence · latency</td><td>#${t.sequence} · ${fmt(t.latencyMs, 3)} ms</td></tr></tbody></table>`
    : '<p class="muted">No telemetry yet.</p>';
  return `<div class="card device" data-id="${d.id}">
    <div class="device-head"><div><div class="device-kind">${KIND_LABEL[d.kind] || d.kind}</div><div class="device-model">${escapeHtml(d.model)}</div></div>
      <div style="display:flex;gap:6px;flex-direction:column;align-items:flex-end"><span class="badge badge-${state}">${state.toUpperCase()}</span><span class="badge badge-sim">SIMULATED</span></div></div>
    <dl class="kv" style="margin:0"><dt>Host link</dt><dd>${escapeHtml(d.hostLink)}</dd><dt>Port</dt><dd>${escapeHtml(d.port)}</dd>
      <dt>USB ID</dt><dd>${escapeHtml(d.usbId)}</dd><dt>I/O voltage</dt><dd>${escapeHtml(d.ioVoltage)}</dd></dl>
    ${dev.lastError ? `<div class="alert alert-error" style="margin:0"><strong>Fault:</strong> ${escapeHtml(dev.lastError)} - press Recover.</div>` : ''}
    <div class="btn-row">
      <button class="btn btn-primary btn-small" data-act="connect" ${state === 'connected' || isBusy ? 'disabled' : ''}>Connect</button>
      <button class="btn btn-small" data-act="disconnect" ${state === 'disconnected' || isBusy ? 'disabled' : ''}>Disconnect</button>
      <button class="btn btn-small" data-act="telemetry" ${state !== 'connected' || isBusy ? 'disabled' : ''}>Read telemetry</button>
      <button class="btn btn-small" data-act="recover" ${state !== 'faulted' || isBusy ? 'disabled' : ''}>Recover</button>
    </div>
    <div class="btn-row"><select class="inline-select" data-fault><option value="telemetry_timeout">next telemetry times out</option><option value="connect_timeout">next connect times out</option></select>
      <button class="btn btn-danger btn-small" data-act="fault" ${isBusy ? 'disabled' : ''}>Inject fault</button></div>
    <div class="telemetry"><h3 style="margin-top:0">Telemetry</h3>${tele}</div>
    <details class="caps"><summary>Capabilities (${dev.capabilities.length})</summary><ul>${dev.capabilities.map(c => `<li><strong>${escapeHtml(c.name)}:</strong> ${escapeHtml(c.value)}</li>`).join('')}</ul></details>
  </div>`;
}

function render() { document.getElementById('hw-devices').innerHTML = devices.map(card).join(''); }

function updateDevice(dev) {
  const i = devices.findIndex(x => x.descriptor.id === dev.descriptor.id);
  if (i >= 0) devices[i] = dev; else devices.push(dev);
}

async function act(id, action) {
  const body = {};
  if (action === 'fault') body.mode = document.querySelector(`.device[data-id="${id}"] [data-fault]`).value;
  busy.add(id); render();
  try {
    const out = await api(`/api/hardware/${id}/${action}`, { method: 'POST', body });
    updateDevice(out.device);
    if (action === 'telemetry') {
      latest[id] = out.telemetry;
      hwLog(`${id}: telemetry #${out.telemetry.sequence} (${out.telemetry.fields.map(f => `${f.name}=${formatField(f)}`).join(', ')})`);
    } else if (action === 'fault') {
      hwLog(`${id}: armed simulated fault "${out.armed}"`, 'w');
    } else if (action === 'recover') {
      hwLog(`${id}: recovery ${out.recovery.recovered ? 'succeeded' : 'FAILED'} after ${out.recovery.attempts} attempt(s) - ${out.recovery.log.join(' | ')}`, out.recovery.recovered ? 'o' : 'e');
    } else {
      hwLog(`${id}: ${action} -> ${out.device.state}`, 'o');
    }
  } catch (e) {
    hwLog(`${id}: ${action} failed - ${e.message}${e.payload && e.payload.recoverable ? ' (recoverable: use Recover)' : ''}`, 'e');
    await refresh();
  } finally {
    busy.delete(id); render();
  }
}

async function refresh() {
  try { devices = (await api('/api/hardware/devices')).devices; render(); } catch (e) { hwLog(e.message, 'e'); }
}

async function discover() {
  try {
    const out = await api('/api/hardware/discover', { method: 'POST' });
    devices = out.devices;
    Object.keys(latest).forEach(k => delete latest[k]);
    render();
    hwLog(`Discovery (simulated) found ${devices.length} devices: ${devices.map(d => `${d.descriptor.id} [${d.descriptor.usbId}]`).join(', ')}`, 'o');
  } catch (e) { hwLog(e.message, 'e'); }
}

function setPolling(on) {
  clearInterval(pollTimer);
  if (!on) return;
  pollTimer = setInterval(() => {
    for (const d of devices) {
      if (d.state === 'connected' && !busy.has(d.descriptor.id)) act(d.descriptor.id, 'telemetry');
    }
  }, 1000);
}

function diagram() {
  // Block diagram of the assumed host-connected workbench (see docs/hardware).
  return `<svg viewBox="0 0 700 260" width="100%" role="img" aria-label="Workbench block diagram" style="font:11px Segoe UI,sans-serif">
  <defs><marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0L10,5L0,10z" fill="#475569"/></marker></defs>
  <rect x="5" y="95" width="140" height="70" rx="6" fill="#e7effb" stroke="#1f5fbf"/><text x="75" y="122" text-anchor="middle" font-weight="600">Host PC</text>
  <text x="75" y="140" text-anchor="middle">Windows 11 · Launch.exe</text><text x="75" y="155" text-anchor="middle" fill="#5d6b7c">C++ adapters</text>
  <rect x="185" y="95" width="95" height="70" rx="6" fill="#f3f5f8" stroke="#64748b"/><text x="232" y="125" text-anchor="middle" font-weight="600">Powered</text><text x="232" y="141" text-anchor="middle" font-weight="600">USB hub</text><text x="232" y="157" text-anchor="middle" fill="#5d6b7c">5 V, common GND</text>
  <line x1="140" y1="130" x2="185" y2="130" stroke="#475569" marker-end="url(#ar)"/>
  <rect x="345" y="15" width="260" height="60" rx="6" fill="#fff" stroke="#1a7f37"/><text x="475" y="38" text-anchor="middle" font-weight="600">MCU board (NUCLEO-F446RE)</text><text x="475" y="56" text-anchor="middle" fill="#5d6b7c">3.3 V I/O · ST-LINK VCP UART · SWD/JTAG hdr</text>
  <rect x="345" y="100" width="260" height="60" rx="6" fill="#fff" stroke="#9a5b00"/><text x="475" y="123" text-anchor="middle" font-weight="600">FPGA board (Basys 3, Artix-7)</text><text x="475" y="141" text-anchor="middle" fill="#5d6b7c">3.3 V LVCMOS33 · FT2232: JTAG + UART</text>
  <rect x="345" y="185" width="260" height="60" rx="6" fill="#fff" stroke="#5b3fa8"/><text x="475" y="208" text-anchor="middle" font-weight="600">JTAG/SWD adapter (J-Link class)</text><text x="475" y="226" text-anchor="middle" fill="#5d6b7c">level-shifted to target VTref</text>
  <line x1="280" y1="115" x2="345" y2="45" stroke="#475569" marker-end="url(#ar)"/><text x="300" y="70" fill="#475569">USB</text>
  <line x1="280" y1="130" x2="345" y2="130" stroke="#475569" marker-end="url(#ar)"/><text x="296" y="124" fill="#475569">USB</text>
  <line x1="280" y1="145" x2="345" y2="215" stroke="#475569" marker-end="url(#ar)"/><text x="300" y="200" fill="#475569">USB</text>
  <path d="M605,215 C640,215 640,45 605,45" fill="none" stroke="#5b3fa8" stroke-dasharray="5 3" marker-end="url(#ar)"/>
  <text x="648" y="125" fill="#5b3fa8">SWD/</text><text x="648" y="139" fill="#5b3fa8">JTAG</text><text x="648" y="153" fill="#5b3fa8">10-pin</text>
  </svg>`;
}

export function initHardware() {
  document.getElementById('hw-diagram').innerHTML = diagram();
  document.getElementById('hw-discover').addEventListener('click', discover);
  document.getElementById('hw-autopoll').addEventListener('change', e => setPolling(e.target.checked));
  document.getElementById('hw-devices').addEventListener('click', e => {
    const btn = e.target.closest('[data-act]');
    if (!btn) return;
    act(btn.closest('.device').dataset.id, btn.dataset.act);
  });
  refresh();
}
