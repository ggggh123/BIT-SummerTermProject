import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import test from 'node:test';
import vm from 'node:vm';

const htmlPath = new URL('../resources/map/navigation.html', import.meta.url);
const qrcPath = new URL('../resources/resources.qrc', import.meta.url);
const executableSuffix = process.platform === 'win32' ? '.exe' : '';

function canRun(command) {
  const result = spawnSync(command, ['--version'], { encoding: 'utf8', windowsHide: true });
  return !result.error && result.status === 0;
}

function* queryQtHostDirectories() {
  for (const qtpaths of [`qtpaths6${executableSuffix}`, `qtpaths${executableSuffix}`]) {
    if (!canRun(qtpaths)) continue;
    for (const query of ['QT_HOST_LIBEXECS', 'QT_INSTALL_LIBEXECS', 'QT_HOST_BINS', 'QT_INSTALL_BINS']) {
      const result = spawnSync(qtpaths, ['--query', query], { encoding: 'utf8', windowsHide: true });
      const directory = result.status === 0 ? result.stdout.trim() : '';
      if (directory) yield directory;
    }
  }
}

function resolveRcc() {
  if (process.env.QT_RCC) {
    if (canRun(process.env.QT_RCC)) return process.env.QT_RCC;
    throw new Error(`QT_RCC is not an executable Qt resource compiler: ${process.env.QT_RCC}. Set QT_RCC to the full path of rcc.`);
  }

  const candidates = [];
  for (const directory of queryQtHostDirectories()) candidates.push(join(directory, `rcc${executableSuffix}`));
  candidates.push(`rcc6${executableSuffix}`, `rcc${executableSuffix}`, `rcc-qt6${executableSuffix}`);
  const rcc = candidates.find(canRun);
  if (rcc) return rcc;
  throw new Error('Unable to locate Qt rcc. Set QT_RCC to the full path of the Qt resource compiler.');
}

const rccExecutable = resolveRcc();

function makeElement(tagName = 'div') {
  return {
    tagName: tagName.toUpperCase(),
    children: [],
    style: { display: '' },
    textContent: '',
    disabled: false,
    onclick: null,
    appendChild(child) {
      this.children.push(child);
      return child;
    },
    addEventListener(type, listener) {
      if (type === 'click') this.onclick = listener;
    },
    click() {
      return this.onclick?.();
    },
  };
}

function fakeTMap({ routeResult } = {}) {
  const calls = { maps: [], driving: [], walking: [], searches: [], polylineStyles: [], polylines: [], detachedLayers: [] };
  let nextRouteResult = routeResult ?? { result: { routes: [{ polyline: [{ lat: 39.9, lng: 116.4 }, { lat: 39.91, lng: 116.41 }] }] } };
  class LatLng {
    constructor(lat, lng) { this.lat = lat; this.lng = lng; }
  }
  class Map {
    constructor(container, options) { calls.maps.push({ container, options }); }
  }
  class Driving {
    constructor(options) { calls.driving.push(options); }
    search(request) { calls.searches.push({ kind: 'driving', request }); return Promise.resolve(nextRouteResult); }
  }
  class Walking {
    constructor(options) { calls.walking.push(options); }
    search(request) { calls.searches.push({ kind: 'walking', request }); return Promise.resolve(nextRouteResult); }
  }
  class PolylineStyle {
    constructor(options) { this.options = options; calls.polylineStyles.push(this); }
  }
  class MultiPolyline {
    constructor(options) { this.options = options; calls.polylines.push(this); }
    setMap(map) { calls.detachedLayers.push({ layer: this, map }); }
  }
  return {
    api: { LatLng, Map, PolylineStyle, MultiPolyline, service: { Driving, Walking } },
    calls,
    setRouteResult(value) { nextRouteResult = value; },
  };
}

function loadPage() {
  const html = readFileSync(htmlPath, 'utf8');
  const elements = {
    map: makeElement(),
    'route-status': makeElement(),
    'route-empty': makeElement(),
    'route-retry': makeElement('button'),
  };
  const scripts = [];
  const logs = [];
  const document = {
    head: { appendChild(script) { scripts.push(script); return script; } },
    createElement(tag) { return makeElement(tag); },
    getElementById(id) { return elements[id] ?? null; },
  };
  const context = {
    document,
    console: { log: (...args) => logs.push(args.join(' ')), error: (...args) => logs.push(args.join(' ')), warn: (...args) => logs.push(args.join(' ')) },
    encodeURIComponent,
    setTimeout,
    clearTimeout,
  };
  context.window = context;
  vm.createContext(context);
  const inlineScript = html.match(/<script>([\s\S]*?)<\/script>/)?.[1];
  assert.ok(inlineScript, 'navigation resource must contain an executable inline script');
  vm.runInContext(inlineScript, context, { filename: 'navigation.html' });
  return { html, context, elements, scripts, logs };
}

async function configureWithFakeMap(page, key = 'runtime key +&=') {
  const pending = page.context.configureMap({ key });
  assert.equal(page.scripts.length, 1, 'configureMap must append exactly one API script');
  const fake = fakeTMap();
  page.context.TMap = fake.api;
  page.scripts[0].onload();
  await pending;
  return fake;
}

test('qrc manifest exposes only the checked-in navigation page and default avatar', () => {
  const qrc = readFileSync(qrcPath, 'utf8');
  const files = [...qrc.matchAll(/<file(?:\s[^>]*)?>([^<]+)<\/file>/g)].map((match) => match[1].trim());
  assert.deepEqual(files, ['map/navigation.html', 'images/default-avatar.svg']);
  const mapping = execFileSync(rccExecutable, ['--list-mapping', qrcPath.pathname], { encoding: 'utf8' });
  assert.deepEqual(
    mapping.trim().split('\n').map((line) => line.split('\t')[0]),
    [':/images/default-avatar.svg', ':/map/navigation.html'],
  );
});

test('page has no committed key and does not load remote code before configuration', () => {
  const page = loadPage();
  assert.doesNotMatch(page.html, /AIza|SK[A-Za-z0-9_-]{12,}|腾讯地图\s*Key\s*[:=]\s*["'][^"']+/i);
  assert.doesNotMatch(page.html, /<script[^>]+\bsrc=/i);
  assert.equal(page.scripts.length, 0);
  assert.equal(typeof page.context.configureMap, 'function');
  assert.equal(typeof page.context.renderRoute, 'function');
  assert.equal(page.context.lastRouteStatus.state, 'idle');
});

test('missing key, invalid coordinates, and unsupported mode fail visibly in Chinese', async () => {
  const page = loadPage();
  await assert.rejects(page.context.configureMap({}), /腾讯地图/);
  assert.match(page.elements['route-status'].textContent, /腾讯地图/);

  await configureWithFakeMap(page);
  await assert.rejects(
    page.context.renderRoute({ from: { lat: 'bad', lng: 116.4 }, to: { lat: 39.9, lng: 116.4 }, mode: 'driving', stationName: '测试站' }),
    /坐标/,
  );
  assert.match(page.elements['route-status'].textContent, /坐标/);
  await assert.rejects(
    page.context.renderRoute({ from: { lat: 39.9, lng: 116.4 }, to: { lat: 39.91, lng: 116.41 }, mode: 'bus', stationName: '测试站' }),
    /驾车或步行/,
  );
  assert.match(page.elements['route-status'].textContent, /驾车或步行/);
  assert.equal(page.elements['route-retry'].style.display, '');
});

test('configureMap requests the official GL service library with only URL-encoded runtime key', async () => {
  const page = loadPage();
  const runtimeKey = 'key with space +&=';
  const pending = page.context.configureMap({ key: runtimeKey });
  const script = page.scripts[0];
  assert.match(script.src, /^https:\/\/map\.qq\.com\/api\/gljs\?v=1\.exp&/);
  assert.match(script.src, /libraries=service/);
  assert.match(script.src, /callback=/);
  assert.match(script.src, new RegExp(`key=${encodeURIComponent(runtimeKey).replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}`));
  assert.ok(typeof page.context[script.src.match(/[?&]callback=([^&]+)/)[1]] === 'function');
  const fake = fakeTMap();
  page.context.TMap = fake.api;
  script.onload();
  await pending;
  assert.equal(fake.calls.maps.length, 1);
});

test('an unusable successful script load rejects visibly instead of leaving configuration pending', async () => {
  const page = loadPage();
  const configuration = page.context.configureMap({ key: 'private-test-key' });
  page.scripts[0].onload();
  await assert.rejects(configuration, /地图加载失败/);
  assert.match(page.elements['route-status'].textContent, /地图加载失败/);
  assert.equal(page.elements['route-retry'].style.display, '');
});

test('Retry recovers API loading with a private key before a pending route', async () => {
  const page = loadPage();
  const runtimeKey = 'private retry +&=';
  const configuration = page.context.configureMap({ key: runtimeKey });
  page.scripts[0].onerror();
  await assert.rejects(configuration, /地图加载失败/);
  const retry = page.elements['route-retry'].click();
  assert.equal(typeof retry?.then, 'function', 'Retry must return the retry promise for controlled callers');
  assert.equal(page.scripts.length, 2, 'Retry must request a fresh API script after API failure');
  const fake = fakeTMap();
  page.context.TMap = fake.api;
  page.scripts[1].onload();
  await retry;
  assert.equal(fake.calls.maps.length, 1);
  const exposed = `${JSON.stringify(page.context.lastRouteStatus)} ${page.elements['route-status'].textContent} ${page.elements['route-empty'].textContent} ${page.logs.join(' ')}`;
  assert.doesNotMatch(exposed, new RegExp(runtimeKey.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')));
});

test('route runtime keeps the key secret while constructing Tencent driving and walking searches', async () => {
  const page = loadPage();
  const runtimeKey = 'not-for-status-+-&';
  const fake = await configureWithFakeMap(page, runtimeKey);
  const from = { lat: 39.9042, lng: 116.4074 };
  const to = { lat: 39.9142, lng: 116.4174 };
  await page.context.renderRoute({ from, to, mode: 'driving', stationName: '朝阳充电站' });
  assert.equal(fake.calls.driving.length, 1);
  assert.deepEqual({ ...fake.calls.searches[0].request.from }, from);
  assert.deepEqual({ ...fake.calls.searches[0].request.to }, to);
  assert.equal(fake.calls.searches[0].kind, 'driving');
  assert.equal(fake.calls.polylines.length, 1);
  assert.equal(fake.calls.polylineStyles.length, 1);
  assert.ok(fake.calls.polylines[0].options.styles.route instanceof fake.api.PolylineStyle);
  assert.equal(page.context.lastRouteStatus.state, 'success');
  assert.match(page.context.lastRouteStatus.label, /朝阳充电站/);

  await page.context.renderRoute({ from, to, mode: 'walking', stationName: '朝阳充电站' });
  assert.equal(fake.calls.walking.length, 1);
  assert.equal(fake.calls.searches[1].kind, 'walking');
  assert.equal(fake.calls.polylines.length, 2);
  assert.deepEqual(fake.calls.detachedLayers, [{ layer: fake.calls.polylines[0], map: null }]);
  const exposed = `${JSON.stringify(page.context.lastRouteStatus)} ${page.elements['route-status'].textContent} ${page.elements['route-empty'].textContent} ${page.logs.join(' ')}`;
  assert.doesNotMatch(exposed, new RegExp(runtimeKey.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')));
});

test('later route failure remains visible and retryable without replacing the last successful route', async () => {
  const page = loadPage();
  const fake = await configureWithFakeMap(page);
  const request = { from: { lat: 39.9, lng: 116.4 }, to: { lat: 39.91, lng: 116.41 }, mode: 'driving', stationName: '保留站' };
  await page.context.renderRoute(request);
  const polylinesBefore = fake.calls.polylines.length;
  const successfulRequest = JSON.stringify(page.context.lastRouteStatus.lastSuccessfulRequest);
  fake.setRouteResult({ result: { routes: [] } });
  await assert.rejects(page.context.renderRoute({ ...request, mode: 'walking' }), /路线/);
  assert.equal(fake.calls.polylines.length, polylinesBefore, 'failed replacement must keep the rendered successful route');
  assert.equal(JSON.stringify(page.context.lastRouteStatus.lastSuccessfulRequest), successfulRequest);
  assert.equal(page.context.lastRouteStatus.state, 'error');
  assert.match(page.elements['route-status'].textContent, /路线/);
  assert.match(page.elements['route-empty'].textContent, /上次成功路线/);
  assert.equal(page.elements['route-retry'].style.display, '');
  fake.setRouteResult({ result: { routes: [{ polyline: [{ lat: 39.9, lng: 116.4 }, { lat: 39.91, lng: 116.41 }] }] } });
  const retry = page.elements['route-retry'].click();
  assert.equal(typeof retry?.then, 'function', 'Retry must return the route retry promise');
  await retry;
  assert.equal(fake.calls.searches.length, 3, 'Retry must start another route search');
  assert.equal(page.context.lastRouteStatus.state, 'success');
});

test('empty or malformed Tencent route results fail safely', async () => {
  const page = loadPage();
  const fake = await configureWithFakeMap(page);
  const request = { from: { lat: 39.9, lng: 116.4 }, to: { lat: 39.91, lng: 116.41 }, mode: 'driving', stationName: '测试站' };
  for (const result of [null, {}, { result: {} }, { result: { routes: [{ polyline: [] }] } }]) {
    fake.setRouteResult(result);
    await assert.rejects(page.context.renderRoute(request), /路线/);
  }
  assert.equal(page.context.lastRouteStatus.state, 'error');
  assert.match(page.elements['route-status'].textContent, /路线/);
});
