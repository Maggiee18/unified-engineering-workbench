// Geometry view: STL triangles and nodal-temperature point cloud (three.js).
// Files are sent verbatim to the C++ backend, which parses and validates them;
// this module only renders the returned arrays.
import * as THREE from 'three';
import { OrbitControls } from '../vendor/OrbitControls.js';
import { api, log, showError, fmt, escapeHtml } from './common.js';

const MAX_UPLOAD = 64 * 1024 * 1024;
let renderer, scene, camera, controls, raycaster;
let modelGroup, helperGroup;
let stlObject = null, cloudObject = null, wireObject = null;
let mode = 'stl';
let cloudData = null;
let initialised = false;

// Perceptually ordered "thermal" ramp (blue -> cyan -> yellow -> red).
const RAMP = [[0.0, [0.19, 0.21, 0.58]], [0.25, [0.15, 0.55, 0.85]], [0.5, [0.35, 0.80, 0.55]],
              [0.75, [0.98, 0.80, 0.20]], [1.0, [0.84, 0.15, 0.13]]];
function rampColor(t) {
  t = Math.min(1, Math.max(0, t));
  for (let i = 1; i < RAMP.length; i++) {
    if (t <= RAMP[i][0]) {
      const [t0, c0] = RAMP[i - 1], [t1, c1] = RAMP[i];
      const f = (t - t0) / (t1 - t0);
      return c0.map((v, k) => v + f * (c1[k] - v));
    }
  }
  return RAMP[RAMP.length - 1][1];
}

function initScene() {
  const host = document.getElementById('viewport');
  renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
  renderer.setPixelRatio(window.devicePixelRatio);
  renderer.setClearColor(0xf7f9fb);
  host.appendChild(renderer.domElement);
  scene = new THREE.Scene();
  camera = new THREE.PerspectiveCamera(40, 1, 0.001, 1e7);
  camera.up.set(0, 0, 1);
  scene.add(new THREE.HemisphereLight(0xffffff, 0x8899aa, 1.6));
  const sun = new THREE.DirectionalLight(0xffffff, 1.4);
  sun.position.set(1, 0.6, 1.4);
  camera.add(sun);
  scene.add(camera);
  modelGroup = new THREE.Group();
  helperGroup = new THREE.Group();
  scene.add(modelGroup, helperGroup);
  controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  controls.screenSpacePanning = true;
  raycaster = new THREE.Raycaster();
  renderer.domElement.addEventListener('click', onPick);
  new ResizeObserver(resize).observe(host);
  resize();
  (function loop() { requestAnimationFrame(loop); controls.update(); renderer.render(scene, camera); })();
}

function resize() {
  const host = document.getElementById('viewport');
  const w = host.clientWidth, h = host.clientHeight;
  if (!w || !h) return;
  renderer.setSize(w, h, false);
  renderer.domElement.style.width = w + 'px';
  renderer.domElement.style.height = h + 'px';
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
}

function activeObject() { return mode === 'stl' ? stlObject : cloudObject; }

function fitToView(direction = new THREE.Vector3(1, -1.2, 0.9)) {
  const obj = activeObject();
  if (!obj) return;
  const box = new THREE.Box3().setFromObject(obj);
  const centre = box.getCenter(new THREE.Vector3());
  const radius = Math.max(box.getSize(new THREE.Vector3()).length() / 2, 1e-9);
  const dist = radius / Math.sin(THREE.MathUtils.degToRad(camera.fov / 2)) * 1.1;
  camera.position.copy(centre).add(direction.clone().normalize().multiplyScalar(dist));
  camera.near = dist / 1000; camera.far = dist * 100;
  camera.updateProjectionMatrix();
  controls.target.copy(centre);
  controls.update();
  rebuildHelpers(box, radius);
}

function rebuildHelpers(box, radius) {
  helperGroup.clear();
  if (!document.getElementById('view-axes').checked) return;
  helperGroup.add(new THREE.Box3Helper(box, 0x9aa7b6));
  const axes = new THREE.AxesHelper(radius * 0.6);
  axes.position.copy(box.min);
  helperGroup.add(axes);
}

function showObject() {
  modelGroup.clear();
  const obj = activeObject();
  document.getElementById('viewport-empty').hidden = !!obj;
  document.getElementById('legend').hidden = !(mode === 'csv' && cloudObject);
  document.getElementById('node-inspect').hidden = !(mode === 'csv' && cloudObject);
  if (!obj) { helperGroup.clear(); document.getElementById('viewport-badge').textContent = ''; return; }
  modelGroup.add(obj);
  if (mode === 'stl' && wireObject && document.getElementById('view-wire').checked) modelGroup.add(wireObject);
  fitToView();
}

// ------------------------------------------------------------------ STL
function buildStl(data) {
  const geom = new THREE.BufferGeometry();
  geom.setAttribute('position', new THREE.Float32BufferAttribute(data.positions, 3));
  geom.setAttribute('normal', new THREE.Float32BufferAttribute(data.normals, 3));
  const mat = new THREE.MeshStandardMaterial({ color: 0x8fa9c9, metalness: 0.15, roughness: 0.55, side: THREE.DoubleSide,
                                               polygonOffset: true, polygonOffsetFactor: 1, polygonOffsetUnits: 1 });
  stlObject = new THREE.Mesh(geom, mat);
  wireObject = new THREE.LineSegments(new THREE.WireframeGeometry(geom), new THREE.LineBasicMaterial({ color: 0x2b3a4d, transparent: true, opacity: 0.35 }));
}

function stlInfo(data, fileName) {
  const b = data.bounds, size = b.max.map((v, i) => v - b.min[i]);
  const rows = [
    ['File', fileName], ['Solid name', data.solidName || '(unnamed)'],
    ['Triangles', data.triangleCount.toLocaleString()], ['Vertices', data.vertexCount.toLocaleString()],
    ['Extent x·y·z', size.map(v => fmt(v)).join(' × ') + ' (file units)'],
    ['Bounds min', b.min.map(v => fmt(v)).join(', ')], ['Bounds max', b.max.map(v => fmt(v)).join(', ')],
    ['Surface area', fmt(data.surfaceArea) + ' units²'], ['Parse time (C++)', fmt(data.parseMs, 3) + ' ms'],
    ['Warnings', data.warnings.length ? data.warnings.join('; ') : 'none'],
  ];
  document.getElementById('stl-info').innerHTML = rows.map(([k, v]) => `<dt>${k}</dt><dd>${escapeHtml(v)}</dd>`).join('');
}

async function loadStl(promiseFactory, fileName) {
  const err = document.getElementById('stl-error');
  showError(err, null);
  document.getElementById('stl-info').innerHTML = '<dt>Status</dt><dd>Parsing in C++…</dd>';
  try {
    const data = await promiseFactory();
    buildStl(data);
    stlInfo(data, fileName || data.fileName);
    log(`STL loaded: ${fileName || data.fileName} - ${data.triangleCount} triangles in ${fmt(data.parseMs, 3)} ms`, 'o');
    document.getElementById('viewport-badge').textContent = `${fileName || data.fileName} · ${data.triangleCount} triangles`;
    showObject();
  } catch (e) {
    showError(err, e);
    document.getElementById('stl-info').innerHTML = '<dt>Status</dt><dd>Rejected - previous model kept</dd>';
    log(`STL rejected: ${e.message}`, 'e');
  }
}

// ---------------------------------------------------------------- CSV
function buildCloud(data) {
  const n = data.nodeCount;
  const pos = new Float32Array(n * 3), col = new Float32Array(n * 3);
  const span = data.maxTemperatureC - data.minTemperatureC || 1;
  for (let i = 0; i < n; i++) {
    pos[3 * i] = data.positions[3 * i]; pos[3 * i + 1] = data.positions[3 * i + 1]; pos[3 * i + 2] = data.positions[3 * i + 2];
    const c = rampColor((data.temperatures[i] - data.minTemperatureC) / span);
    col[3 * i] = c[0]; col[3 * i + 1] = c[1]; col[3 * i + 2] = c[2];
  }
  const geom = new THREE.BufferGeometry();
  geom.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  geom.setAttribute('color', new THREE.BufferAttribute(col, 3));
  const mat = new THREE.PointsMaterial({ size: Number(document.getElementById('csv-point-size').value), sizeAttenuation: false, vertexColors: true });
  cloudObject = new THREE.Points(geom, mat);
  cloudData = data;
  buildLegend(data.minTemperatureC, data.maxTemperatureC);
}

function buildLegend(tMin, tMax) {
  // Top of the bar = maximum temperature (t = 1).
  const stops = RAMP.slice().reverse().map(([t, c]) => `rgb(${c.map(v => Math.round(v * 255)).join(',')}) ${(1 - t) * 100}%`);
  document.getElementById('legend-bar').style.background = `linear-gradient(to bottom, ${stops.join(',')})`;
  const ticks = [];
  for (let i = 0; i <= 5; i++) ticks.push(`<span>${fmt(tMax - (tMax - tMin) * i / 5, 4)} °C</span>`);
  document.getElementById('legend-ticks').innerHTML = ticks.join('');
}

function csvInfo(data, fileName) {
  const b = data.bounds;
  const rows = [
    ['File', fileName], ['Nodes', data.nodeCount.toLocaleString()],
    ['Temperature range', `${fmt(data.minTemperatureC)} … ${fmt(data.maxTemperatureC)} °C`],
    ['x range', `${fmt(b.min[0])} … ${fmt(b.max[0])} m`], ['y range', `${fmt(b.min[1])} … ${fmt(b.max[1])} m`],
    ['z range', `${fmt(b.min[2])} … ${fmt(b.max[2])} m`], ['Parse time (C++)', fmt(data.parseMs, 3) + ' ms'],
    ['Warnings', data.warnings.length ? data.warnings.join('; ') : 'none'],
  ];
  document.getElementById('csv-info').innerHTML = rows.map(([k, v]) => `<dt>${k}</dt><dd>${escapeHtml(v)}</dd>`).join('');
  const syn = document.getElementById('csv-synthetic');
  const label = data.comments.find(c => /synthetic/i.test(c));
  syn.hidden = !label;
  if (label) syn.textContent = 'Dataset is labelled: ' + label;
}

async function loadCsv(promiseFactory, fileName) {
  const err = document.getElementById('csv-error');
  showError(err, null);
  document.getElementById('csv-info').innerHTML = '<dt>Status</dt><dd>Parsing in C++…</dd>';
  try {
    const data = await promiseFactory();
    buildCloud(data);
    csvInfo(data, fileName || data.fileName);
    document.getElementById('node-info').innerHTML = '<dt>Hint</dt><dd>Click a point to inspect it</dd>';
    document.getElementById('viewport-badge').textContent = `${fileName || data.fileName} · ${data.nodeCount} nodes`;
    log(`Nodal CSV loaded: ${fileName || data.fileName} - ${data.nodeCount} nodes, ${fmt(data.minTemperatureC)}…${fmt(data.maxTemperatureC)} °C`, 'o');
    showObject();
  } catch (e) {
    showError(err, e);
    document.getElementById('csv-info').innerHTML = '<dt>Status</dt><dd>Rejected - previous data kept</dd>';
    log(`CSV rejected: ${e.message}`, 'e');
  }
}

function onPick(event) {
  if (mode !== 'csv' || !cloudObject) return;
  const rect = renderer.domElement.getBoundingClientRect();
  const ndc = new THREE.Vector2(((event.clientX - rect.left) / rect.width) * 2 - 1, -((event.clientY - rect.top) / rect.height) * 2 + 1);
  raycaster.setFromCamera(ndc, camera);
  // Pick tolerance in world units scaled to the view distance.
  raycaster.params.Points.threshold = camera.position.distanceTo(controls.target) * 0.01;
  const hits = raycaster.intersectObject(cloudObject);
  if (!hits.length) return;
  hits.sort((a, b) => a.distanceToRay - b.distanceToRay);
  const i = hits[0].index;
  const d = cloudData;
  document.getElementById('node-info').innerHTML = [
    ['node_id', d.nodeIds[i]], ['x', fmt(d.positions[3 * i], 6) + ' m'], ['y', fmt(d.positions[3 * i + 1], 6) + ' m'],
    ['z', fmt(d.positions[3 * i + 2], 6) + ' m'], ['temperature', fmt(d.temperatures[i], 6) + ' °C'],
  ].map(([k, v]) => `<dt>${k}</dt><dd>${escapeHtml(v)}</dd>`).join('');
  log(`Node ${d.nodeIds[i]}: ${fmt(d.temperatures[i], 6)} °C`);
}

function readFile(input, onText) {
  input.addEventListener('change', () => {
    const file = input.files[0];
    input.value = '';
    if (!file) return;
    if (file.size > MAX_UPLOAD) {
      const msg = `${file.name} is ${(file.size / 1048576).toFixed(1)} MiB; the limit is 64 MiB`;
      showError(document.getElementById(mode === 'stl' ? 'stl-error' : 'csv-error'), new Error(msg));
      log(msg, 'e');
      return;
    }
    file.text().then(text => onText(text, file.name));
  });
}

function setMode(next) {
  mode = next;
  document.querySelectorAll('[data-geo-mode]').forEach(b => b.classList.toggle('active', b.dataset.geoMode === mode));
  document.getElementById('geo-stl-panel').hidden = mode !== 'stl';
  document.getElementById('geo-csv-panel').hidden = mode !== 'csv';
  document.getElementById('view-wire').disabled = mode !== 'stl';
  const obj = activeObject();
  document.getElementById('viewport-badge').textContent = obj ? (mode === 'stl' ? 'STL geometry' : 'Nodal temperature point cloud') : '';
  showObject();
}

export function onGeometryShown() { if (initialised) resize(); }

export function initGeometry() {
  initScene();
  initialised = true;
  document.querySelectorAll('[data-geo-mode]').forEach(b => b.addEventListener('click', () => setMode(b.dataset.geoMode)));
  readFile(document.getElementById('stl-file'), (text, name) => loadStl(() => api('/api/geometry/stl', { method: 'POST', body: text }), name));
  readFile(document.getElementById('csv-file'), (text, name) => loadCsv(() => api('/api/results/nodal-csv', { method: 'POST', body: text }), name));
  document.getElementById('stl-example').addEventListener('click', () => loadStl(() => api('/api/geometry/example')));
  document.getElementById('csv-example').addEventListener('click', () => loadCsv(() => api('/api/results/example')));
  document.getElementById('view-fit').addEventListener('click', () => fitToView());
  document.getElementById('view-iso').addEventListener('click', () => fitToView(new THREE.Vector3(1, -1, 1)));
  document.getElementById('view-top').addEventListener('click', () => fitToView(new THREE.Vector3(0, -0.001, 1)));
  document.getElementById('view-wire').addEventListener('change', showObject);
  document.getElementById('view-axes').addEventListener('change', () => fitToView());
  document.getElementById('csv-point-size').addEventListener('input', e => { if (cloudObject) cloudObject.material.size = Number(e.target.value); });
}
