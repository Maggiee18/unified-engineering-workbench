// Shared UI helpers: backend calls, logging, formatting, downloads.
// Presentation only - all computation happens in the C++ backend.

const logEl = () => document.getElementById('app-log');

export function log(message, level = 'i') {
  const t = new Date().toISOString().substring(11, 23);
  const line = document.createElement('div');
  line.innerHTML = `<span class="t">${t}</span> `;
  const body = document.createElement('span');
  body.className = level;
  body.textContent = message;
  line.appendChild(body);
  const el = logEl();
  if (el) { el.appendChild(line); el.scrollTop = el.scrollHeight; }
  setStatus(message, level);
}

export function setStatus(message, level = 'i') {
  const el = document.getElementById('status-text');
  if (!el) return;
  el.textContent = message;
  el.style.color = level === 'e' ? 'var(--err)' : level === 'o' ? 'var(--ok)' : '';
}

/** Error carrying the backend's structured error payload. */
export class ApiError extends Error {
  constructor(message, payload, status) { super(message); this.payload = payload; this.status = status; }
}

/**
 * Calls the C++ backend. `body` may be an object (sent as JSON) or a string
 * (sent raw, e.g. file contents). Returns parsed JSON or text.
 */
export async function api(path, { method = 'GET', body, raw = false } = {}) {
  const init = { method, headers: {} };
  if (body !== undefined) {
    if (typeof body === 'string') { init.body = body; init.headers['Content-Type'] = 'text/plain'; }
    else { init.body = JSON.stringify(body); init.headers['Content-Type'] = 'application/json'; }
  }
  let res;
  try { res = await fetch(path, init); }
  catch (e) { throw new ApiError('Backend not reachable - is Launch.exe still running?', null, 0); }
  const type = res.headers.get('Content-Type') || '';
  const payload = type.includes('application/json') ? await res.json() : await res.text();
  if (!res.ok) {
    const msg = (payload && payload.error) ? payload.error : `HTTP ${res.status}`;
    throw new ApiError(msg, payload, res.status);
  }
  return raw ? payload : payload;
}

export function showError(el, err) {
  if (!el) return;
  if (!err) { el.hidden = true; el.textContent = ''; return; }
  el.hidden = false;
  const kind = err.payload && err.payload.kind ? err.payload.kind.replace('_', ' ') : 'error';
  el.innerHTML = '';
  const strong = document.createElement('strong');
  strong.textContent = kind.charAt(0).toUpperCase() + kind.slice(1) + ': ';
  el.appendChild(strong);
  el.appendChild(document.createTextNode(err.message));
}

/** Engineering-friendly number formatting. */
export function fmt(x, digits = 4) {
  if (x === null || x === undefined || Number.isNaN(x)) return '—';
  const a = Math.abs(x);
  if (a !== 0 && (a < 1e-3 || a >= 1e6)) return x.toExponential(digits - 1);
  return Number(x.toPrecision(digits)).toString();
}

export function download(filename, text, type = 'text/csv') {
  const blob = new Blob([text], { type });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url; a.download = filename;
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
  log(`Exported ${filename}`, 'o');
}

export function downloadDataUrl(filename, dataUrl) {
  const a = document.createElement('a');
  a.href = dataUrl; a.download = filename;
  document.body.appendChild(a); a.click(); a.remove();
  log(`Exported ${filename}`, 'o');
}

/** Combines several canvases vertically into one PNG (white background). */
export function canvasesToPng(canvases) {
  const w = Math.max(...canvases.map(c => c.width));
  const h = canvases.reduce((s, c) => s + c.height, 0);
  const out = document.createElement('canvas');
  out.width = w; out.height = h;
  const ctx = out.getContext('2d');
  ctx.fillStyle = '#fff'; ctx.fillRect(0, 0, w, h);
  let y = 0;
  for (const c of canvases) { ctx.drawImage(c, 0, y); y += c.height; }
  return out.toDataURL('image/png');
}

/** Reads number inputs of a form; marks invalid ones. Returns {values, ok}. */
export function readNumbers(form, names, rules = {}) {
  const values = {};
  let ok = true;
  for (const name of names) {
    const input = form.elements[name];
    const v = Number(input.value);
    const rule = rules[name] || {};
    let valid = input.value.trim() !== '' && Number.isFinite(v);
    if (valid && rule.positive && !(v > 0)) valid = false;
    if (valid && rule.integer && !Number.isInteger(v)) valid = false;
    if (valid && rule.min !== undefined && v < rule.min) valid = false;
    if (valid && rule.max !== undefined && v > rule.max) valid = false;
    input.classList.toggle('invalid', !valid);
    input.title = valid ? '' : (rule.message || 'Enter a valid number');
    if (!valid) ok = false;
    values[name] = v;
  }
  return { values, ok };
}

export function setBusy(button, busy, label) {
  if (!button.dataset.label) button.dataset.label = button.textContent;
  button.disabled = busy;
  button.textContent = busy ? (label || 'Working…') : button.dataset.label;
}

export function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

export function kpi(label, value, sub = '', cls = '') {
  return `<div class="kpi ${cls}"><div class="kpi-label">${escapeHtml(label)}</div><div class="kpi-value">${escapeHtml(value)}</div>${sub ? `<div class="kpi-sub">${escapeHtml(sub)}</div>` : ''}</div>`;
}

/** Chart.js defaults shared by all plots. */
export function chartDefaults() {
  const C = window.Chart;
  C.defaults.font.family = '"Segoe UI", system-ui, sans-serif';
  C.defaults.font.size = 12;
  C.defaults.color = '#364457';
  C.defaults.animation = false;
  C.defaults.maintainAspectRatio = false;
  C.defaults.plugins.legend.labels.boxWidth = 14;
}

/** Plugin: paints a white background so exported PNGs are not transparent. */
export const whiteBackground = {
  id: 'whiteBackground',
  beforeDraw(chart) {
    const { ctx } = chart;
    ctx.save(); ctx.globalCompositeOperation = 'destination-over';
    ctx.fillStyle = '#fff'; ctx.fillRect(0, 0, chart.width, chart.height); ctx.restore();
  },
};

export const SERIES = ['#1f5fbf', '#d9480f', '#2f9e44', '#7048e8', '#c2255c', '#0c8599'];
