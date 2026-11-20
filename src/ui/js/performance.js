// Performance view: starts a C++ benchmark job, polls progress, plots results.
import { api, log, showError, fmt, kpi, download, downloadDataUrl, canvasesToPng, readNumbers, setBusy, whiteBackground, SERIES } from './common.js';

let jobId = null, pollTimer = null, lastResult = null, charts = {}, logicalCpus = 1;

function renderSystem(sys) {
  if (!sys) return;
  const rows = [
    ['CPU', sys.cpuModel], ['Physical cores', sys.physicalCores || 'unknown'], ['Logical CPUs', sys.logicalCpus],
    ['RAM', `${(sys.totalRamBytes / 1073741824).toFixed(1)} GiB`], ['OS', sys.operatingSystem], ['Compiler', sys.compiler],
    ['Build / flags', `${sys.buildType} · ${sys.optimisationFlags}`],
    ['Launch → backend ready', `${Math.round(sys.backendReadyMs)} ms`], ['Launch → UI ready', sys.uiReadyMs ? `${Math.round(sys.uiReadyMs)} ms` : 'pending'],
  ];
  document.getElementById('sys-info').innerHTML = rows.map(([k, v]) => `<dt>${k}</dt><dd>${String(v).replace(/</g, '&lt;')}</dd>`).join('');
  logicalCpus = sys.logicalCpus;
  document.querySelectorAll('#perf-workers input').forEach(i => {
    const tooMany = Number(i.value) > logicalCpus;
    i.disabled = tooMany;
    if (tooMany) i.checked = false;
  });
  document.getElementById('perf-cpu-hint').textContent = logicalCpus < 4
    ? `Only ${logicalCpus} logical CPU(s) available: larger worker counts are disabled.` + (logicalCpus === 1 ? ' Speedup cannot be assessed on this machine.' : '')
    : `${logicalCpus} logical CPUs available; worker counts are capped at 4.`;
}

function makeCharts() {
  const C = window.Chart;
  const axis = (t, extra = {}) => ({ title: { display: true, text: t }, grid: { color: '#edf0f4' }, ...extra });
  charts.time = new C(document.getElementById('chart-time'), {
    type: 'bar', data: { labels: [], datasets: [] }, plugins: [whiteBackground],
    options: { scales: { x: axis('configuration'), y: axis('median wall-clock time [s]', { beginAtZero: true }) }, plugins: { legend: { display: false } } },
  });
  charts.speed = new C(document.getElementById('chart-speedup'), {
    type: 'line', data: { labels: [], datasets: [] }, plugins: [whiteBackground],
    options: { scales: { x: axis('workers p'), y: axis('speedup T₁/Tₚ', { beginAtZero: true, position: 'left' }),
                         y2: axis('efficiency', { beginAtZero: true, max: 1.2, position: 'right', grid: { drawOnChartArea: false } }) } },
  });
}

function render(r) {
  const runs = r.timings.filter(t => !t.skipped);
  charts.time.data.labels = ['serial', ...runs.map(t => `p=${t.workers}`)];
  charts.time.data.datasets = [{ data: [r.serialMedianSeconds, ...runs.map(t => t.medianSeconds)], backgroundColor: ['#8a96a6', ...runs.map(() => SERIES[0])] }];
  charts.time.update();
  charts.speed.data.labels = runs.map(t => String(t.workers));
  charts.speed.data.datasets = [
    { label: 'measured speedup', data: runs.map(t => t.speedup), borderColor: SERIES[0], backgroundColor: SERIES[0], yAxisID: 'y' },
    { label: 'ideal (= p)', data: runs.map(t => t.workers), borderColor: '#8a96a6', borderDash: [5, 4], pointRadius: 0, yAxisID: 'y' },
    { label: 'efficiency', data: runs.map(t => t.efficiency), borderColor: SERIES[1], backgroundColor: SERIES[1], yAxisID: 'y2' },
  ];
  charts.speed.update();

  const rowsHtml = [`<tr><td class="txt">serial loop</td><td>1</td><td>${fmt(r.serialMedianSeconds, 5)}</td><td>${r.serialRunSeconds.map(s => fmt(s, 4)).join(', ')}</td><td>—</td><td>—</td><td>reference</td><td>—</td></tr>`];
  for (const t of r.timings) {
    if (t.skipped) { rowsHtml.push(`<tr><td class="txt">pool</td><td>${t.workers}</td><td colspan="6" class="txt muted">skipped: ${t.skipReason}</td></tr>`); continue; }
    rowsHtml.push(`<tr><td class="txt">thread pool</td><td>${t.workers}</td><td>${fmt(t.medianSeconds, 5)}</td><td>${t.runSeconds.map(s => fmt(s, 4)).join(', ')}</td>
      <td>${fmt(t.speedup, 4)}</td><td>${fmt(t.efficiency, 3)}</td><td>${t.maxAbsDiffVsSerial === 0 ? '0 (bitwise equal)' : fmt(t.maxAbsDiffVsSerial, 3)}</td>
      <td class="${t.agreementPassed ? 'pass' : 'fail'}">${t.agreementPassed ? 'PASS' : 'FAIL'}</td></tr>`);
  }
  document.querySelector('#perf-table tbody').innerHTML = rowsHtml.join('');

  const best = runs.reduce((a, b) => (b.speedup > (a ? a.speedup : 0) ? b : a), null);
  const p1 = runs.find(t => t.workers === 1);
  document.getElementById('perf-kpis').innerHTML = [
    kpi('Workload', `${r.config.cases} × N=${r.config.elements}`, `seed ${r.config.seed}, ${r.config.warmups} warm-up + ${r.config.repetitions} runs`),
    kpi('Serial median', `${fmt(r.serialMedianSeconds, 4)} s`, 'plain loop, no pool'),
    best ? kpi('Best speedup', `${fmt(best.speedup, 3)}×`, `p = ${best.workers}, efficiency ${fmt(best.efficiency, 3)}`) : '',
    kpi('Serial/parallel agreement', r.allAgreementPassed ? 'PASS' : 'FAIL', 'max |ΔT| ≤ 1e-8 °C', r.allAgreementPassed ? 'ok' : 'bad'),
    p1 ? kpi('Pool overhead (p=1)', `${fmt((p1.medianSeconds / r.serialMedianSeconds - 1) * 100, 3)} %`, 'vs. serial loop') : '',
  ].join('');

  const notes = [];
  if (r.note) notes.push(r.note);
  if (best && best.workers > 1) {
    notes.push(`Measured on ${r.system.logicalCpus} logical / ${r.system.physicalCores || '?'} physical cores. Efficiency below 1 comes from task dispatch and joining, memory bandwidth (each case allocates and writes ${r.config.elements + 1} doubles) and any background load; worker counts above the physical core count share execution units (SMT) and cannot scale linearly.`);
  }
  document.getElementById('perf-analysis').textContent = notes.join(' ');
}

async function poll() {
  try {
    const s = await api(`/api/jobs/${jobId}`);
    document.getElementById('perf-bar').style.width = `${Math.round(s.progress * 100)}%`;
    document.getElementById('perf-progress-text').textContent = `${Math.round(s.progress * 100)} % · ${s.message || 'starting'}`;
    if (s.status === 'running') { pollTimer = setTimeout(poll, 300); return; }
    finish();
    if (s.status === 'completed') {
      lastResult = s.result;
      render(s.result);
      ['perf-exp-summary', 'perf-exp-runs', 'perf-exp-json', 'perf-exp-png'].forEach(id => { document.getElementById(id).disabled = false; });
      log(`Benchmark finished in ${fmt(s.elapsedSeconds, 3)} s; agreement ${s.result.allAgreementPassed ? 'PASS' : 'FAIL'}`, 'o');
    } else if (s.status === 'cancelled') {
      document.getElementById('perf-progress-text').textContent = 'Cancelled - no partial results were published; previous results are unchanged.';
      log('Benchmark cancelled safely', 'w');
    } else {
      showError(document.getElementById('perf-error'), new Error(s.error || 'benchmark failed'));
      log(`Benchmark failed: ${s.error}`, 'e');
    }
  } catch (e) { finish(); showError(document.getElementById('perf-error'), e); log(e.message, 'e'); }
}

function finish() {
  setBusy(document.getElementById('perf-start'), false);
  document.getElementById('perf-cancel').disabled = true;
  jobId = null;
}

async function start() {
  const err = document.getElementById('perf-error');
  showError(err, null);
  const form = document.getElementById('perf-form');
  const { values, ok } = readNumbers(form, ['cases', 'elementsPerCase', 'seed', 'warmups', 'repetitions'], {
    cases: { integer: true, min: 1, max: 1000000, message: '1 … 1,000,000 cases' },
    elementsPerCase: { integer: true, min: 1, max: 200000, message: '1 … 200,000 elements' },
    seed: { integer: true, min: 0, message: 'non-negative integer' },
    warmups: { integer: true, min: 0, max: 10, message: '0 … 10' }, repetitions: { integer: true, min: 1, max: 50, message: '1 … 50' },
  });
  const workers = [...document.querySelectorAll('#perf-workers input:checked')].map(i => Number(i.value));
  if (!ok) { showError(err, new Error('Fix the highlighted fields (hover for the allowed range).')); return; }
  if (!workers.length) { showError(err, new Error('Select at least one worker count.')); return; }
  try {
    const res = await api('/api/performance/benchmark', { method: 'POST', body: { cases: values.cases, elements: values.elementsPerCase, seed: values.seed,
                                                                       warmups: values.warmups, repetitions: values.repetitions, workers } });
    jobId = res.jobId;
    setBusy(document.getElementById('perf-start'), true, 'Running…');
    document.getElementById('perf-cancel').disabled = false;
    document.getElementById('perf-progress').hidden = false;
    document.getElementById('perf-bar').style.width = '0%';
    log(`Benchmark started (${jobId}): ${values.cases} cases × ${values.elementsPerCase} elements, workers ${workers.join(',')}`);
    poll();
  } catch (e) { showError(err, e); log(e.message, 'e'); }
}

export function initPerformance(sys) {
  renderSystem(sys);
  makeCharts();
  document.getElementById('perf-start').addEventListener('click', start);
  document.getElementById('perf-cancel').addEventListener('click', async () => {
    if (!jobId) return;
    document.getElementById('perf-cancel').disabled = true;
    try { await api(`/api/jobs/${jobId}/cancel`, { method: 'POST' }); log('Cancellation requested; workers stop at the next case boundary', 'w'); }
    catch (e) { log(e.message, 'e'); }
  });
  const tag = () => `cases${lastResult.config.cases}_N${lastResult.config.elements}`;
  document.getElementById('perf-exp-summary').addEventListener('click', () => download(`benchmark_summary_${tag()}.csv`, lastResult.summaryCsv));
  document.getElementById('perf-exp-runs').addEventListener('click', () => download(`benchmark_runs_${tag()}.csv`, lastResult.runsCsv));
  document.getElementById('perf-exp-json').addEventListener('click', () => {
    const { summaryCsv, runsCsv, ...rest } = lastResult;
    download(`benchmark_${tag()}.json`, JSON.stringify(rest, null, 2), 'application/json');
  });
  document.getElementById('perf-exp-png').addEventListener('click', () =>
    downloadDataUrl('benchmark_plots.png', canvasesToPng(['chart-time', 'chart-speedup'].map(id => document.getElementById(id)))));
  // Refresh UI-ready time once it has been recorded.
  setTimeout(async () => { try { renderSystem(await api('/api/system')); } catch (_) { /* offline */ } }, 1500);
}
