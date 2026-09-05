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

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((resolvePromise, rejectPromise) => {
    resolve = resolvePromise;
    reject = rejectPromise;
  });
  return { promise, resolve, reject };
}

function fakeTMap({ routeResult } = {}) {
  const calls = {
    maps: [], driving: [], walking: [], searches: [], polylineStyles: [], polylines: [],
    markerStyles: [], markers: [], detachedLayers: [], attachedLayers: [], fittedBounds: [],
  };
  let nextRouteResult = routeResult ?? { result: { routes: [{ polyline: [{ lat: 39.9, lng: 116.4 }, { lat: 39.91, lng: 116.41 }] }] } };
  const queuedRouteResults = [];
  let failNextMarkerConstruction = false;
  let failNextMarkerAttachment = false;
  const routeResultForSearch = () => queuedRouteResults.length > 0
    ? queuedRouteResults.shift() : nextRouteResult;
  class LatLng {
    constructor(lat, lng) { this.lat = lat; this.lng = lng; }
  }
  class Map {
    constructor(container, options) { calls.maps.push({ container, options }); }
    fitBounds(bounds, options) { calls.fittedBounds.push({ bounds, options }); }
  }
  class LatLngBounds {
    constructor(southwest, northeast) { this.southwest = southwest; this.northeast = northeast; }
  }
  class Driving {
    constructor(options) { calls.driving.push(options); }
    search(request) { calls.searches.push({ kind: 'driving', request }); return Promise.resolve(routeResultForSearch()); }
  }
  class Walking {
    constructor(options) { calls.walking.push(options); }
    search(request) { calls.searches.push({ kind: 'walking', request }); return Promise.resolve(routeResultForSearch()); }
  }
  class PolylineStyle {
    constructor(options) { this.options = options; calls.polylineStyles.push(this); }
  }
  class MultiPolyline {
    constructor(options) { this.options = options; calls.polylines.push(this); }
    setMap(map) {
      (map === null ? calls.detachedLayers : calls.attachedLayers).push({ kind: 'route', layer: this, map });
    }
  }
  class MarkerStyle {
    constructor(options) { this.options = options; calls.markerStyles.push(this); }
  }
  class MultiMarker {
    constructor(options) {
      if (failNextMarkerConstruction) {
        failNextMarkerConstruction = false;
        throw new Error('planned marker construction failure');
      }
      this.options = options;
      calls.markers.push(this);
    }
    setMap(map) {
      if (map !== null && failNextMarkerAttachment) {
        failNextMarkerAttachment = false;
        throw new Error('planned marker attachment failure');
      }
      (map === null ? calls.detachedLayers : calls.attachedLayers).push({ kind: 'marker', layer: this, map });
    }
  }
  return {
    api: { LatLng, LatLngBounds, Map, PolylineStyle, MultiPolyline, MarkerStyle, MultiMarker, service: { Driving, Walking } },
    calls,
    setRouteResult(value) { nextRouteResult = value; },
    queueRouteResult(value) { queuedRouteResults.push(value); },
    failNextMarkerConstruction() { failNextMarkerConstruction = true; },
    failNextMarkerAttachment() { failNextMarkerAttachment = true; },
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
  assert.equal(typeof page.context.resetRouteSession, 'function');
  assert.doesNotMatch(page.html, /id=["']route-retry["']/);
  assert.doesNotMatch(page.html, /retryCurrentOperation/);
  assert.equal(page.context.lastRouteStatus.state, 'idle');
});

test('missing key, invalid coordinates, and unsupported mode fail visibly in Chinese', async () => {
  const page = loadPage();
  await assert.rejects(page.context.configureMap({}), /腾讯地图/);
  assert.match(page.elements['route-status'].textContent, /腾讯地图/);

  await configureWithFakeMap(page);
  await assert.rejects(
    page.context.renderRoute({ from: { lat: 'bad', lng: 116.4 }, to: { lat: 39.9, lng: 116.4 }, mode: 'driving', stationName: '测试站' }, 'invalid-coordinate'),
    /坐标/,
  );
  assert.match(page.elements['route-status'].textContent, /坐标/);
  await assert.rejects(
    page.context.renderRoute({ from: { lat: 39.9, lng: 116.4 }, to: { lat: 39.91, lng: 116.41 }, mode: 'bus', stationName: '测试站' }, 'invalid-mode'),
    /驾车或步行/,
  );
  assert.match(page.elements['route-status'].textContent, /驾车或步行/);
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
  assert.doesNotMatch(page.html, /id=["']route-retry["']/);
});

test('HTML exposes no autonomous retry that can bypass the native operation owner', async () => {
  const page = loadPage();
  const runtimeKey = 'private retry +&=';
  const configuration = page.context.configureMap({ key: runtimeKey });
  page.scripts[0].onerror();
  await assert.rejects(configuration, /地图加载失败/);
  assert.doesNotMatch(page.html, /id=["']route-retry["']/);
  assert.doesNotMatch(page.html, /retryElement\.addEventListener/);
  assert.equal(page.elements['route-retry'].onclick, null);
  assert.equal(page.scripts.length, 1);
  const exposed = `${JSON.stringify(page.context.lastRouteStatus)} ${page.elements['route-status'].textContent} ${page.elements['route-empty'].textContent} ${page.logs.join(' ')}`;
  assert.doesNotMatch(exposed, new RegExp(runtimeKey.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')));
});

test('route runtime keeps the key secret while constructing Tencent driving and walking searches', async () => {
  const page = loadPage();
  const runtimeKey = 'not-for-status-+-&';
  const fake = await configureWithFakeMap(page, runtimeKey);
  const from = { lat: 39.9042, lng: 116.4074 };
  const to = { lat: 39.9142, lng: 116.4174 };
  await page.context.renderRoute({ from, to, mode: 'driving', stationName: '朝阳充电站' }, 'native-driving');
  assert.equal(fake.calls.driving.length, 1);
  assert.deepEqual({ ...fake.calls.searches[0].request.from }, from);
  assert.deepEqual({ ...fake.calls.searches[0].request.to }, to);
  assert.equal(fake.calls.searches[0].kind, 'driving');
  assert.equal(fake.calls.polylines.length, 1);
  assert.equal(fake.calls.markers.length, 1);
  assert.equal(fake.calls.markerStyles.length, 2);
  assert.equal(fake.calls.polylineStyles.length, 1);
  assert.ok(fake.calls.polylines[0].options.styles.route instanceof fake.api.PolylineStyle);
  assert.ok(fake.calls.markers[0].options.styles.start instanceof fake.api.MarkerStyle);
  assert.ok(fake.calls.markers[0].options.styles.end instanceof fake.api.MarkerStyle);
  assert.match(fake.calls.markers[0].options.styles.start.options.src, /^data:image\/svg\+xml/);
  assert.match(fake.calls.markers[0].options.styles.end.options.src, /^data:image\/svg\+xml/);
  assert.doesNotMatch(fake.calls.markers[0].options.styles.start.options.src, /^https?:/);
  assert.deepEqual(
    JSON.parse(JSON.stringify(fake.calls.markers[0].options.geometries
      .map(({ id, styleId, position }) => ({ id, styleId, lat: position.lat, lng: position.lng })))),
    [{ id: 'start', styleId: 'start', ...from }, { id: 'end', styleId: 'end', ...to }],
  );
  assert.equal(page.context.lastRouteStatus.state, 'success');
  assert.match(page.context.lastRouteStatus.label, /朝阳充电站/);

  await page.context.renderRoute({ from, to, mode: 'walking', stationName: '朝阳充电站' }, 'native-walking');
  assert.equal(fake.calls.walking.length, 1);
  assert.equal(fake.calls.searches[1].kind, 'walking');
  assert.equal(fake.calls.polylines.length, 2);
  assert.equal(fake.calls.markers.length, 2);
  assert.ok(fake.calls.detachedLayers.some(({ kind, layer, map }) => kind === 'route' && layer === fake.calls.polylines[0] && map === null));
  assert.ok(fake.calls.detachedLayers.some(({ kind, layer, map }) => kind === 'marker' && layer === fake.calls.markers[0] && map === null));
  const exposed = `${JSON.stringify(page.context.lastRouteStatus)} ${page.elements['route-status'].textContent} ${page.elements['route-empty'].textContent} ${page.logs.join(' ')}`;
  assert.doesNotMatch(exposed, new RegExp(runtimeKey.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')));
});

test('successful route fits every route point and endpoints without reframing on failure', async () => {
  const page = loadPage();
  const fake = await configureWithFakeMap(page);
  fake.setRouteResult({ result: { routes: [{ polyline: [
    { lat: 39.95, lng: 116.30 }, { lat: 40.02, lng: 116.28 }, { lat: 39.96, lng: 116.33 },
  ] }] } });
  const request = {
    from: { lat: 39.94, lng: 116.31 }, to: { lat: 39.97, lng: 116.34 },
    mode: 'walking', stationName: '完整路线',
  };
  await page.context.renderRoute(request, 'fit-route');
  assert.equal(fake.calls.fittedBounds.length, 1);
  const { bounds } = fake.calls.fittedBounds[0];
  assert.deepEqual({ ...bounds.southwest }, { lat: 39.94, lng: 116.28 });
  assert.deepEqual({ ...bounds.northeast }, { lat: 40.02, lng: 116.34 });
  fake.setRouteResult({ result: { routes: [] } });
  await assert.rejects(page.context.renderRoute(request, 'failed-route'), /路线规划失败/);
  assert.equal(fake.calls.fittedBounds.length, 1, 'failed request must retain the previous camera');
});

test('newer route attempt owns overlays status and cache when deferred completions invert', async () => {
  const page = loadPage();
  const fake = await configureWithFakeMap(page);
  const firstResult = deferred();
  const secondResult = deferred();
  fake.queueRouteResult(firstResult.promise);
  fake.queueRouteResult(secondResult.promise);
  const requestA = {
    from: { lat: 39.9, lng: 116.4 }, to: { lat: 39.91, lng: 116.41 },
    mode: 'driving', stationName: '旧目的地',
  };
  const requestB = {
    from: { lat: 39.8, lng: 116.3 }, to: { lat: 39.82, lng: 116.32 },
    mode: 'walking', stationName: '新目的地',
  };
  const pendingA = page.context.renderRoute(requestA, 'attempt-1');
  const pendingB = page.context.renderRoute(requestB, 'attempt-2');
  secondResult.resolve({ result: { routes: [{ polyline: [requestB.from, requestB.to] }] } });
  await pendingB;
  const successfulLayer = fake.calls.polylines.at(-1);
  const successfulMarkers = fake.calls.markers.at(-1);
  assert.match(page.elements['route-status'].textContent, /新目的地/);
  assert.equal(page.context.lastRouteStatus.lastSuccessfulRequest.stationName, '新目的地');

  firstResult.resolve({ result: { routes: [{ polyline: [requestA.from, requestA.to] }] } });
  await assert.rejects(pendingA, /已失效|superseded/);
  assert.equal(fake.calls.polylines.at(-1), successfulLayer);
  assert.equal(fake.calls.markers.at(-1), successfulMarkers);
  assert.match(page.elements['route-status'].textContent, /新目的地/);
  assert.equal(page.context.lastRouteStatus.lastSuccessfulRequest.stationName, '新目的地');
});

test('timeout invalidation makes a late route completion harmless', async () => {
  const page = loadPage();
  const fake = await configureWithFakeMap(page);
  const baseline = {
    from: { lat: 39.9, lng: 116.4 }, to: { lat: 39.91, lng: 116.41 },
    mode: 'driving', stationName: '保留路线',
  };
  await page.context.renderRoute(baseline, 'baseline');
  const baselineLayer = fake.calls.polylines.at(-1);
  const baselineMarkers = fake.calls.markers.at(-1);
  const lateResult = deferred();
  fake.queueRouteResult(lateResult.promise);
  const lateRequest = { ...baseline, to: { lat: 39.93, lng: 116.43 }, stationName: '超时路线' };
  const pending = page.context.renderRoute(lateRequest, 'timeout-attempt');
  page.context.invalidateRouteAttempt('timeout-attempt');
  lateResult.resolve({ result: { routes: [{ polyline: [lateRequest.from, lateRequest.to] }] } });
  await assert.rejects(pending, /已失效|superseded/);
  assert.equal(fake.calls.polylines.at(-1), baselineLayer);
  assert.equal(fake.calls.markers.at(-1), baselineMarkers);
  assert.equal(page.context.lastRouteStatus.lastSuccessfulRequest.stationName, '保留路线');
});

test('route and endpoint markers replace transactionally and survive marker failure', async () => {
  const page = loadPage();
  const fake = await configureWithFakeMap(page);
  const original = {
    from: { lat: 39.9, lng: 116.4 }, to: { lat: 39.91, lng: 116.41 },
    mode: 'driving', stationName: '原路线',
  };
  await page.context.renderRoute(original, 'original-attempt');
  const originalLayer = fake.calls.polylines[0];
  const originalMarkers = fake.calls.markers[0];
  fake.failNextMarkerAttachment();
  const replacement = {
    ...original, to: { lat: 39.95, lng: 116.45 }, mode: 'walking', stationName: '失败替换',
  };
  await assert.rejects(
    page.context.renderRoute(replacement, 'failed-marker-attempt'), /路线/,
  );
  assert.equal(page.context.lastRouteStatus.lastSuccessfulRequest.stationName, '原路线');
  assert.ok(!fake.calls.detachedLayers.some(({ layer }) => layer === originalLayer));
  assert.ok(!fake.calls.detachedLayers.some(({ layer }) => layer === originalMarkers));
  assert.match(page.elements['route-empty'].textContent, /原路线/);
});

test('later route failure remains visible for native retry without replacing the last successful route', async () => {
  const page = loadPage();
  const fake = await configureWithFakeMap(page);
  const request = { from: { lat: 39.9, lng: 116.4 }, to: { lat: 39.91, lng: 116.41 }, mode: 'driving', stationName: '保留站' };
  await page.context.renderRoute(request, 'native-success');
  const polylinesBefore = fake.calls.polylines.length;
  const successfulRequest = JSON.stringify(page.context.lastRouteStatus.lastSuccessfulRequest);
  fake.setRouteResult({ result: { routes: [] } });
  await assert.rejects(page.context.renderRoute({ ...request, mode: 'walking' }, 'native-failure'), /路线/);
  assert.equal(fake.calls.polylines.length, polylinesBefore, 'failed replacement must keep the rendered successful route');
  assert.equal(JSON.stringify(page.context.lastRouteStatus.lastSuccessfulRequest), successfulRequest);
  assert.equal(page.context.lastRouteStatus.state, 'error');
  assert.match(page.elements['route-status'].textContent, /路线/);
  assert.match(page.elements['route-empty'].textContent, /上次成功路线/);
  assert.doesNotMatch(page.html, /id=["']route-retry["']/);
  assert.equal(fake.calls.searches.length, 2, 'HTML must not dispatch an independent retry');
});

test('renderRoute requires a native operation id and cannot supersede a native attempt without one', async () => {
  const page = loadPage();
  const fake = await configureWithFakeMap(page);
  const request = { from: { lat: 39.9, lng: 116.4 }, to: { lat: 39.91, lng: 116.41 }, mode: 'driving', stationName: '受控路线' };
  await page.context.renderRoute(request, 'native-owned');
  const statusBefore = JSON.stringify(page.context.lastRouteStatus);
  const searchesBefore = fake.calls.searches.length;
  await assert.rejects(page.context.renderRoute({ ...request, stationName: '绕过路线' }), /操作标识/);
  assert.equal(fake.calls.searches.length, searchesBefore);
  assert.equal(JSON.stringify(page.context.lastRouteStatus), statusBefore);
  assert.match(page.elements['route-status'].textContent, /受控路线/);
});

test('session reset atomically removes route and endpoint markers and clears all route cache', async () => {
  const page = loadPage();
  const fake = await configureWithFakeMap(page);
  const request = { from: { lat: 39.9, lng: 116.4 }, to: { lat: 39.91, lng: 116.41 }, mode: 'driving', stationName: '旧账户路线' };
  await page.context.renderRoute(request, 'old-session-route');
  const oldRoute = fake.calls.polylines.at(-1);
  const oldMarkers = fake.calls.markers.at(-1);

  assert.equal(page.context.resetRouteSession(), true);
  assert.ok(fake.calls.detachedLayers.some(({ kind, layer }) => kind === 'route' && layer === oldRoute));
  assert.ok(fake.calls.detachedLayers.some(({ kind, layer }) => kind === 'marker' && layer === oldMarkers));
  assert.equal(page.context.lastRouteStatus.state, 'idle');
  assert.equal(page.context.lastRouteStatus.lastSuccessfulRequest, undefined);
  assert.doesNotMatch(page.elements['route-status'].textContent, /旧账户路线/);
  assert.doesNotMatch(page.elements['route-empty'].textContent, /旧账户路线/);
});

test('empty or malformed Tencent route results fail safely', async () => {
  const page = loadPage();
  const fake = await configureWithFakeMap(page);
  const request = { from: { lat: 39.9, lng: 116.4 }, to: { lat: 39.91, lng: 116.41 }, mode: 'driving', stationName: '测试站' };
  let index = 0;
  for (const result of [null, {}, { result: {} }, { result: { routes: [{ polyline: [] }] } }]) {
    fake.setRouteResult(result);
    await assert.rejects(page.context.renderRoute(request, `malformed-${++index}`), /路线/);
  }
  assert.equal(page.context.lastRouteStatus.state, 'error');
  assert.match(page.elements['route-status'].textContent, /路线/);
});
