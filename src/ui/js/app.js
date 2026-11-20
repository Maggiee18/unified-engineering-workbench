// Application shell: navigation, backend status, log panel, view modules.
import { api, log, chartDefaults } from './common.js';
import { initGeometry, onGeometryShown } from './geometry.js';
import { initThermal } from './thermal.js';
import { initPerformance } from './performance.js';
import { initHardware } from './hardware.js';
import { initBackprop } from './backprop.js';

function initNavigation() {
  const items = document.querySelectorAll('.nav-item');
  items.forEach(item => item.addEventListener('click', () => {
    items.forEach(i => i.classList.toggle('active', i === item));
    document.querySelectorAll('.view').forEach(v => v.classList.toggle('active', v.id === `view-${item.dataset.view}`));
    if (item.dataset.view === 'geometry') onGeometryShown();
  }));
}

function initLogPanel() {
  const panel = document.getElementById('logpanel');
  const toggle = document.getElementById('log-toggle');
  toggle.addEventListener('click', () => {
    panel.hidden = !panel.hidden;
    toggle.textContent = panel.hidden ? 'Show log' : 'Hide log';
  });
}

async function connectBackend() {
  const dot = document.getElementById('backend-dot');
  const text = document.getElementById('backend-text');
  try {
    const ready = await api('/api/ui-ready', { method: 'POST' });
    const sys = await api('/api/system');
    dot.className = 'dot dot-ok';
    text.textContent = `Backend online · port ${sys.port}`;
    document.getElementById('launch-meta').textContent =
      `backend ${Math.round(ready.backendReadyMs)} ms · UI ${Math.round(ready.uiReadyMs)} ms`;
    log(`Connected to C++ backend (${sys.compiler}); launch-to-ready: backend ${Math.round(ready.backendReadyMs)} ms, UI ${Math.round(ready.uiReadyMs)} ms`, 'o');
    return sys;
  } catch (e) {
    dot.className = 'dot dot-err';
    text.textContent = 'Backend offline';
    log(e.message, 'e');
    return null;
  }
}

function initQuit() {
  document.getElementById('btn-quit').addEventListener('click', async () => {
    if (!window.confirm('Stop the workbench backend? You can restart it with Launch.exe.')) return;
    try { await api('/api/shutdown', { method: 'POST' }); } catch (_) { /* already stopping */ }
    document.body.innerHTML = '<div style="display:grid;place-items:center;height:100vh;font:16px Segoe UI,sans-serif;color:#364457">The workbench backend has stopped. You can close this window.</div>';
  });
}

// Detect the backend going away (console window closed) and say so.
function startHeartbeat() {
  setInterval(async () => {
    const dot = document.getElementById('backend-dot');
    if (!dot) return;
    try { await api('/api/system'); dot.className = 'dot dot-ok'; }
    catch (_) {
      dot.className = 'dot dot-err';
      document.getElementById('backend-text').textContent = 'Backend offline';
    }
  }, 5000);
}

async function main() {
  chartDefaults();
  initNavigation();
  initLogPanel();
  initQuit();
  const sys = await connectBackend();
  initGeometry();
  initThermal();
  initPerformance(sys);
  initHardware();
  initBackprop();
  startHeartbeat();
}

main();
