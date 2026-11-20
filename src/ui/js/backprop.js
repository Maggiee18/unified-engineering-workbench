// Backprop view: shows the C++ hand-derived gradients, finite-difference check and loss curve.
import { api, log, showError, fmt, download, readNumbers, setBusy, whiteBackground, SERIES } from './common.js';

let chart = null, lastCsv = '';

function row(cells) { return `<tr>${cells.map(c => `<td>${c}</td>`).join('')}</tr>`; }

async function run() {
  const err = document.getElementById('bp-error');
  showError(err, null);
  const form = document.getElementById('bp-form');
  const { values, ok } = readNumbers(form, ['w', 'b', 'v', 'c', 'x', 'y', 'learningRate', 'epsilon', 'steps'], {
    learningRate: { positive: true, message: 'learning rate must be > 0' }, epsilon: { positive: true, max: 0.999, message: '0 < ε < 1' },
    steps: { integer: true, min: 0, max: 100000, message: '0 … 100000 steps' },
  });
  if (!ok) { showError(err, new Error('Fix the highlighted fields.')); return; }
  const button = document.getElementById('bp-run');
  setBusy(button, true, 'Computing…');
  try {
    const r = await api('/api/ml/backprop', { method: 'POST', body: {
      params: { w: values.w, b: values.b, v: values.v, c: values.c }, x: values.x, y: values.y,
      learningRate: values.learningRate, epsilon: values.epsilon, steps: values.steps } });
    const f = r.forward, ch = r.chain;
    document.getElementById('bp-forward').innerHTML = '<thead><tr><th>Quantity</th><th>Expression</th><th>Value</th></tr></thead><tbody>' + [
      ['z', 'w·x + b', f.z], ['h', 'tanh(z)', f.h], ['ŷ', 'v·h + c', f.yHat], ['J', '½(ŷ − y)²', f.loss],
      ['∂J/∂ŷ', 'ŷ − y', ch.dJ_dyHat], ['∂J/∂h', '(ŷ − y)·v', ch.dJ_dh], ['∂h/∂z', '1 − tanh²(z)', ch.dh_dz], ['∂J/∂z', '∂J/∂h · ∂h/∂z', ch.dJ_dz],
    ].map(([q, e, v]) => row([`<span style="font-family:inherit">${q}</span>`, e, fmt(v, 10)])).join('') + '</tbody>';
    const exprs = { w: '∂J/∂z · x', b: '∂J/∂z', v: '(ŷ − y)·h', c: 'ŷ − y' };
    document.getElementById('bp-grads').innerHTML = '<thead><tr><th>θ</th><th>analytic</th><th>numerical (central)</th><th>|diff|</th><th>check</th></tr></thead><tbody>' +
      ['w', 'b', 'v', 'c'].map(p => row([`${p} <span class="muted">${exprs[p]}</span>`, fmt(r.analytic[p], 10), fmt(r.numerical[p], 10), fmt(r.absError[p], 3),
        r.absError[p] <= r.tolerance ? '<span class="pass">PASS</span>' : '<span class="fail">FAIL</span>'])).join('') + '</tbody>';
    document.getElementById('bp-update').innerHTML = '<thead><tr><th>θ</th><th>before</th><th>− η·∂J/∂θ</th><th>after</th><th>direction</th></tr></thead><tbody>' +
      ['w', 'b', 'v', 'c'].map(p => row([p, fmt(r.initial[p], 8), fmt(-values.learningRate * r.analytic[p], 8), fmt(r.updated[p], 8),
        r.analytic[p] < 0 ? 'increase (∂J/∂θ < 0)' : r.analytic[p] > 0 ? 'decrease (∂J/∂θ > 0)' : 'unchanged'])).join('') +
      `<tr><td colspan="5" class="txt">Loss J: ${fmt(f.loss, 8)} → ${fmt(r.lossAfterUpdate, 8)} after one step</td></tr></tbody>`;
    const C = window.Chart;
    if (!chart) {
      chart = new C(document.getElementById('chart-loss'), {
        type: 'line', data: { labels: [], datasets: [] }, plugins: [whiteBackground],
        options: { scales: { x: { title: { display: true, text: 'training step' } }, y: { type: 'logarithmic', title: { display: true, text: 'loss J (log scale)' } } },
                   plugins: { legend: { display: false } } },
      });
    }
    chart.data.labels = r.lossHistory.map((_, i) => i);
    chart.data.datasets = [{ data: r.lossHistory.map(v => Math.max(v, 1e-300)), borderColor: SERIES[0], pointRadius: 0, borderWidth: 2 }];
    chart.update();
    lastCsv = r.lossCsv;
    document.getElementById('bp-exp').disabled = false;
    log(`Backprop: J0=${fmt(f.loss, 6)}, gradient check ${r.gradientCheckPassed ? 'PASS' : 'FAIL'}, J after ${values.steps} steps = ${fmt(r.lossHistory[r.lossHistory.length - 1], 4)}`, r.gradientCheckPassed ? 'o' : 'e');
  } catch (e) {
    showError(err, e); log(e.message, 'e');
  } finally { setBusy(button, false); }
}

export function initBackprop() {
  document.getElementById('bp-run').addEventListener('click', run);
  document.getElementById('bp-exp').addEventListener('click', () => download('backprop_loss_history.csv', lastCsv));
  run();
}
