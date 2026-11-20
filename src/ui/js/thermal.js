// Thermal Analysis view: drives the C++ 1D FEA solver and plots its output.
import { api, log, showError, fmt, kpi, download, downloadDataUrl, canvasesToPng, readNumbers, setBusy, whiteBackground, SERIES } from './common.js';

const PARAMS = ['L', 'A', 'k', 'q', 'T0', 'TL'];
const DEFAULTS = { L: 1, A: 0.0001, k: 10, q: 10000, T0: 100, TL: 20, N: 10 };
const RULES = {
  L: { positive: true, message: 'L must be > 0 m' }, A: { positive: true, message: 'A must be > 0 m²' },
  k: { positive: true, message: 'k must be > 0 W/(m·K)' }, q: {}, T0: { min: -273.15, message: 'T(0) ≥ −273.15 °C' },
  TL: { min: -273.15, message: 'T(L) ≥ −273.15 °C' }, N: { integer: true, min: 1, max: 2000000, message: 'N must be a whole number 1 … 2,000,000' },
};
let charts = {};
let last = { problem: null, elements: 10, meshes: [10, 20, 40, 80] };

function form() { return document.getElementById('fea-form'); }

function readInputs(withMesh) {
  const names = withMesh ? [...PARAMS, 'N'] : PARAMS;
  const { values, ok } = readNumbers(form(), names, RULES);
  const meshes = [...document.querySelectorAll('#fea-meshes input:checked')].map(i => Number(i.value));
  if (!ok) throw new Error('Some parameters are invalid - fix the highlighted fields (hover for details).');
  if (!withMesh && meshes.length < 2) throw new Error('Select at least two meshes for a convergence study.');
  const problem = Object.fromEntries(PARAMS.map(p => [p, values[p]]));
  return { problem, elements: values.N, meshes };
}

function makeCharts() {
  const C = window.Chart;
  const axis = (title, extra = {}) => ({ title: { display: true, text: title }, grid: { color: '#edf0f4' }, ...extra });
  charts.temp = new C(document.getElementById('chart-temp'), {
    type: 'scatter', data: { datasets: [] }, plugins: [whiteBackground],
    options: { showLine: true, scales: { x: axis('x [m]'), y: axis('Temperature T [°C]') },
               plugins: { tooltip: { callbacks: { label: c => `${c.dataset.label}: x=${fmt(c.parsed.x)} m, T=${fmt(c.parsed.y, 6)} °C` } } } },
  });
  charts.mid = new C(document.getElementById('chart-miderr'), {
    type: 'scatter', data: { datasets: [] }, plugins: [whiteBackground],
    options: { showLine: true, scales: { x: axis('x at element midpoint [m]'), y: axis('T_exact − T_h [°C]') } },
  });
  charts.conv = new C(document.getElementById('chart-conv'), {
    type: 'scatter', data: { datasets: [] }, plugins: [whiteBackground],
    options: { showLine: true, scales: { x: axis('element size h [m]', { type: 'logarithmic' }), y: axis('max |error| [°C]', { type: 'logarithmic' }) },
               plugins: { tooltip: { callbacks: { label: c => `${c.dataset.label}: h=${fmt(c.parsed.x)} m, e=${fmt(c.parsed.y, 6)} °C` } } } },
  });
}

function exactDataset(sol) {
  return { label: 'Exact T(x)', data: sol.exactX.map((x, i) => ({ x, y: sol.exactT[i] })), borderColor: '#18212d',
           borderWidth: 2, borderDash: [6, 4], pointRadius: 0, order: 0 };
}

function showKpis(sol, extraLabel = '') {
  const balance = sol.reactionLeftW + sol.reactionRightW + sol.heatGeneratedW;
  document.getElementById('fea-kpis').innerHTML = [
    kpi(`Max |e| at midpoints${extraLabel}`, `${fmt(sol.maxMidpointError, 6)} °C`, `N = ${sol.elements}, FE interpolation`),
    kpi('Predicted q h²/(8k)', `${fmt(sol.predictedMidpointError, 6)} °C`, 'closed-form midpoint error'),
    kpi('Max |e| at nodes', `${fmt(sol.maxNodalError, 3)} °C`, 'round-off level: nodes are exact'),
    kpi('Energy balance', `${fmt(Math.abs(balance), 3)} W`, `|R₀ + R_L + qAL|, qAL = ${fmt(sol.heatGeneratedW)} W`, Math.abs(balance) < 1e-9 ? 'ok' : 'bad'),
    kpi('Peak nodal temperature', sol.T.length ? `${fmt(sol.T.reduce((a, b) => Math.max(a, b), -Infinity), 6)} °C` : '—', `solve ${fmt(sol.solveMs ?? 0, 3)} ms (C++)`),
  ].join('');
}

function plotSolutions(solutions) {
  const temp = [exactDataset(solutions[0])];
  const mid = [];
  solutions.forEach((s, k) => {
    const color = SERIES[k % SERIES.length];
    temp.push({ label: `FE nodes, N=${s.elements}`, data: s.x.map((x, i) => ({ x, y: s.T[i] })), borderColor: color, backgroundColor: color,
                borderWidth: 1.4, pointRadius: s.elements <= 40 ? 2.5 : 1.2 });
    mid.push({ label: `N=${s.elements}`, data: s.midpointX.map((x, i) => ({ x, y: -s.midpointSignedError[i] })), borderColor: color,
               backgroundColor: color, pointRadius: 2, borderWidth: 1 });
  });
  charts.temp.data.datasets = temp; charts.temp.update();
  charts.mid.data.datasets = mid; charts.mid.update();
}

function plotConvergence(rows) {
  const pts = rows.map(r => ({ x: r.h, y: r.maxMidpointError }));
  const ref = rows.map(r => ({ x: r.h, y: rows[0].maxMidpointError * (r.h / rows[0].h) ** 2 }));
  charts.conv.data.datasets = [
    { label: 'FE: max midpoint error', data: pts, borderColor: SERIES[0], backgroundColor: SERIES[0], pointRadius: 4 },
    { label: 'slope 2 reference (∝ h²)', data: ref, borderColor: '#8a96a6', borderDash: [5, 4], pointRadius: 0 },
  ];
  charts.conv.update();
  document.querySelector('#fea-table tbody').innerHTML = rows.map(r => `<tr><td>${r.elements}</td><td>${fmt(r.h)}</td>
    <td>${fmt(r.maxMidpointError, 8)}</td><td>${fmt(r.predictedMidpointError, 8)}</td><td>${fmt(r.maxNodalError, 3)}</td>
    <td>${r.observedOrder ? fmt(r.observedOrder, 6) : '—'}</td></tr>`).join('');
}

async function run(kind) {
  const err = document.getElementById('fea-error');
  const progress = document.getElementById('fea-progress');
  const button = document.getElementById(kind === 'study' ? 'fea-run-study' : 'fea-run-single');
  showError(err, null);
  let input;
  try { input = readInputs(kind === 'single'); } catch (e) { showError(err, e); log(e.message, 'e'); return; }
  setBusy(button, true, 'Solving…'); progress.hidden = false;
  try {
    if (kind === 'study') {
      const out = await api('/api/thermal/convergence', { method: 'POST', body: { problem: input.problem, meshes: input.meshes } });
      plotSolutions(out.solutions);
      plotConvergence(out.rows);
      const finest = out.solutions[out.solutions.length - 1];
      showKpis(finest, ' (finest)');
      last = { problem: input.problem, elements: finest.elements, meshes: input.meshes };
      log(`Convergence study N=${input.meshes.join(',')}: midpoint errors ${out.rows.map(r => fmt(r.maxMidpointError, 5)).join(', ')} °C; order ≈ ${fmt(out.rows[out.rows.length - 1].observedOrder, 5)}`, 'o');
    } else {
      const sol = await api('/api/thermal/solve', { method: 'POST', body: { problem: input.problem, elements: input.elements } });
      if (sol.nodesIncluded) plotSolutions([sol]);
      showKpis(sol);
      last = { ...last, problem: input.problem, elements: input.elements };
      log(`Solved N=${sol.elements} in ${fmt(sol.solveMs, 3)} ms: max midpoint error ${fmt(sol.maxMidpointError, 6)} °C`, 'o');
      if (!sol.nodesIncluded) log('Mesh too fine to plot every node; summary values shown, CSV export contains all nodes.', 'w');
    }
    ['fea-exp-nodal', 'fea-exp-mid', 'fea-exp-conv', 'fea-exp-png'].forEach(id => { document.getElementById(id).disabled = false; });
  } catch (e) {
    showError(err, e); log(`Thermal solve failed: ${e.message}`, 'e');
  } finally {
    setBusy(button, false); progress.hidden = true;
  }
}

async function exportCsv(kind) {
  try {
    const csv = await api('/api/thermal/export', { method: 'POST', body: { problem: last.problem, elements: last.elements, meshes: last.meshes, kind } });
    const name = kind === 'convergence' ? 'fea_convergence.csv' : `fea_${kind}_N${last.elements}.csv`;
    download(name, csv);
  } catch (e) { log(`Export failed: ${e.message}`, 'e'); }
}

export function initThermal() {
  makeCharts();
  document.getElementById('fea-run-study').addEventListener('click', () => run('study'));
  document.getElementById('fea-run-single').addEventListener('click', () => run('single'));
  document.getElementById('fea-reset').addEventListener('click', () => {
    for (const [k, v] of Object.entries(DEFAULTS)) { form().elements[k].value = v; form().elements[k].classList.remove('invalid'); }
    log('Thermal parameters reset to the assignment values');
  });
  form().addEventListener('input', () => showError(document.getElementById('fea-error'), null));
  document.getElementById('fea-exp-nodal').addEventListener('click', () => exportCsv('nodal'));
  document.getElementById('fea-exp-mid').addEventListener('click', () => exportCsv('midpoint'));
  document.getElementById('fea-exp-conv').addEventListener('click', () => exportCsv('convergence'));
  document.getElementById('fea-exp-png').addEventListener('click', () =>
    downloadDataUrl('fea_plots.png', canvasesToPng(['chart-temp', 'chart-miderr', 'chart-conv'].map(id => document.getElementById(id)))));
  run('study');  // show the assignment case immediately
}
