// UI walkthrough / end-to-end check (documentation tooling, not backend logic).
// Drives the running workbench in headless Chromium, exercises every view and
// saves timestamped screenshots + the in-app log.
//   node tools/ui_walkthrough.mjs http://127.0.0.1:8765/ docs/walkthrough
import { createRequire } from 'module';
import fs from 'fs';
import path from 'path';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');

const url = process.argv[2] || 'http://127.0.0.1:8765/';
const out = process.argv[3] || 'docs/walkthrough';
fs.mkdirSync(out, { recursive: true });
const steps = [];
const browser = await chromium.launch({ args: ['--use-gl=swiftshader', '--enable-unsafe-swiftshader'] });
const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
const consoleErrors = [];
page.on('console', m => { if (m.type() === 'error') consoleErrors.push(m.text()); });
page.on('pageerror', e => consoleErrors.push(String(e)));
let n = 0;
async function shot(name, note) {
  n += 1;
  const file = `${String(n).padStart(2, '0')}_${name}.png`;
  await page.screenshot({ path: path.join(out, file) });
  steps.push({ step: n, time: new Date().toISOString(), file, note, status: await page.textContent('#status-text') });
  console.log(`${n}. ${note} -> ${file}`);
}
const nav = v => page.click(`.nav-item[data-view="${v}"]`);

await page.goto(url);
await page.waitForSelector('.dot-ok', { timeout: 15000 });
await shot('launch', 'Application opened; backend online indicator and launch-to-ready times');

await page.click('#stl-example');
await page.waitForFunction(() => document.getElementById('stl-info').textContent.includes('Triangles'));
await page.waitForTimeout(800);
await shot('stl_example', 'Example ASCII STL parsed in C++ and rendered (fit-to-view)');
await page.check('#view-wire');
const vp = await page.$('#viewport');
const box = await vp.boundingBox();
await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
await page.mouse.down(); await page.mouse.move(box.x + box.width / 2 + 160, box.y + box.height / 2 + 60, { steps: 10 }); await page.mouse.up();
await page.mouse.wheel(0, -300);
await page.waitForTimeout(500);
await shot('stl_rotate_zoom', 'Rotated (left-drag) and zoomed (wheel) with wireframe overlay');
await page.click('#view-fit');
await page.waitForTimeout(300);

await page.setInputFiles('#stl-file', { name: 'broken.stl', mimeType: 'text/plain', buffer: Buffer.from('solid x\nfacet normal 0 0 1\nouter loop\nvertex 0 0 nan\n') });
await page.waitForSelector('#stl-error:not([hidden])');
await shot('stl_malformed', 'Malformed STL (NaN vertex) rejected with line number; previous model kept');
await page.setInputFiles('#stl-file', { name: 'empty.stl', mimeType: 'text/plain', buffer: Buffer.from('') });
await page.waitForFunction(() => document.getElementById('stl-error').textContent.includes('empty'));
await shot('stl_empty', 'Empty STL file explained');

await page.click('[data-geo-mode="csv"]');
await page.click('#csv-example');
await page.waitForFunction(() => document.getElementById('csv-info').textContent.includes('Nodes'));
await page.waitForTimeout(600);
await page.click('#view-iso');
await page.waitForTimeout(400);
const vb = await vp.boundingBox();
// Click near the centre of the cloud for node inspection
for (const [dx, dy] of [[0, 0], [10, 5], [-15, 10], [20, -10], [0, 20]]) {
  await page.mouse.click(vb.x + vb.width / 2 + dx, vb.y + vb.height / 2 + dy);
  await page.waitForTimeout(150);
  if ((await page.textContent('#node-info')).includes('node_id')) break;
}
await shot('csv_point_cloud', 'Synthetic nodal-temperature CSV as coloured point cloud with °C legend and node inspection');
await page.setInputFiles('#csv-file', { name: 'missing_column.csv', mimeType: 'text/csv', buffer: Buffer.from('node_id,x_m,y_m,temperature_C\n1,0,0,20\n') });
await page.waitForSelector('#csv-error:not([hidden])');
await shot('csv_missing_column', 'CSV with missing z_m column rejected with actionable message');

await nav('thermal');
await page.waitForFunction(() => document.querySelector('#fea-table tbody').textContent.includes('0.3125'));
await page.waitForTimeout(400);
await shot('thermal_convergence', 'FEA for N = 10, 20, 40, 80 vs exact solution; midpoint errors and log-log convergence');
await page.fill('#fea-form input[name="k"]', '-5');
await page.click('#fea-run-study');
await page.waitForSelector('#fea-error:not([hidden])');
await shot('thermal_validation', 'Invalid conductivity flagged before any solve');
await page.click('#fea-reset');

await nav('performance');
await page.fill('#perf-form input[name="cases"]', process.env.WB_CASES || '400');
await page.fill('#perf-form input[name="elementsPerCase"]', process.env.WB_ELEMENTS || '2000');
await page.click('#perf-start');
await page.waitForTimeout(700);
await shot('performance_running', 'Benchmark running in a C++ background job; UI stays responsive with progress');
await page.waitForFunction(() => !document.getElementById('perf-start').disabled, null, { timeout: 600000 });
await page.waitForTimeout(400);
await shot('performance_results', 'Serial vs thread-pool medians, speedup, efficiency and 1e-8 °C agreement check');
await page.fill('#perf-form input[name="cases"]', '20000');
await page.click('#perf-start');
await page.waitForTimeout(600);
await page.click('#perf-cancel');
await page.waitForFunction(() => document.getElementById('perf-progress-text').textContent.includes('Cancelled'), null, { timeout: 60000 });
await shot('performance_cancelled', 'Cancellation: job stopped safely, earlier results untouched');

await nav('hardware');
await page.click('#hw-discover');
await page.waitForSelector('.device');
for (const id of ['mcu0', 'fpga0', 'jtag0']) {
  await page.click(`.device[data-id="${id}"] [data-act="connect"]`);
  await page.waitForSelector(`.device[data-id="${id}"] .badge-connected`);
  await page.click(`.device[data-id="${id}"] [data-act="telemetry"]`);
  await page.waitForFunction(i => document.querySelector(`.device[data-id="${i}"] .telemetry table`), id);
}
await shot('hardware_connected', 'All three simulated devices discovered, connected, telemetry received');
await page.click('.device[data-id="fpga0"] [data-act="fault"]');
await page.waitForTimeout(200);
await page.click('.device[data-id="fpga0"] [data-act="telemetry"]');
await page.waitForSelector('.device[data-id="fpga0"] .badge-faulted');
await shot('hardware_timeout', 'Injected telemetry timeout: FPGA faulted with recoverable error');
await page.click('.device[data-id="fpga0"] [data-act="recover"]');
await page.waitForSelector('.device[data-id="fpga0"] .badge-connected');
await page.click('.device[data-id="fpga0"] [data-act="telemetry"]');
await page.waitForTimeout(400);
await shot('hardware_recovered', 'Recovery reconnects the device and telemetry resumes');

await nav('backprop');
await page.waitForFunction(() => document.getElementById('bp-grads').textContent.includes('PASS'));
await page.waitForTimeout(300);
await shot('backprop', 'Hand-coded gradients vs central differences (ε = 1e-6), one update, loss over 100 steps');

await page.click('#log-toggle');
await page.waitForTimeout(200);
await shot('log', 'Application log panel');
const logText = await page.evaluate(() => [...document.querySelectorAll('#app-log div')].map(d => d.textContent).join('\n'));
fs.writeFileSync(path.join(out, 'ui_log.txt'), logText + '\n');
fs.writeFileSync(path.join(out, 'steps.json'), JSON.stringify({ url, consoleErrors, steps }, null, 2));
console.log('console errors:', consoleErrors.length ? consoleErrors : 'none');
await browser.close();
