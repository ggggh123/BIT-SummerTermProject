import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createPoller } from '../assets/poller.js';

const here = dirname(fileURLToPath(import.meta.url));
const valid = JSON.parse(readFileSync(join(here, 'fixtures', 'valid_snapshot.json'), 'utf8'));
const invalid = JSON.parse(readFileSync(join(here, 'fixtures', 'invalid_snapshot.json'), 'utf8'));
const fallbackRaw = JSON.parse(
  readFileSync(join(here, '..', 'fallback', 'dashboard_snapshot.json'), 'utf8'),
);

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

async function waitFor(predicate, timeout = 1500) {
  const start = Date.now();
  while (Date.now() - start < timeout) {
    if (predicate()) return true;
    await sleep(5);
  }
  throw new Error('waitFor timeout');
}

const LIVE = 'runtime/dashboard_snapshot.json';
const FALLBACK = 'fallback/dashboard_snapshot.json';

function makeServer() {
  let mode = { type: 'body', body: valid };
  const fetchImpl = async (url) => {
    if (String(url).startsWith('fallback')) {
      if (mode.fallback === 'http') return jsonResponse({}, 500);
      return jsonResponse(fallbackRaw);
    }
    if (mode.type === 'http') return jsonResponse({}, 500);
    if (mode.type === 'malformed') return jsonResponse('{"snapshotVersion":1,"broken');
    if (mode.type === 'invalid') return jsonResponse(invalid);
    if (mode.type === 'pending') return new Promise(() => {});
    return jsonResponse(mode.body);
  };
  return { fetchImpl, set: (next) => { mode = next; } };
}

function jsonResponse(body, status = 200) {
  return {
    ok: status >= 200 && status < 300,
    status,
    json: async () => {
      if (typeof body === 'string') return JSON.parse(body); // throws on malformed
      return structuredClone(body);
    },
  };
}

function setup(options = {}) {
  const server = makeServer();
  const snapshots = [];
  const statuses = [];
  const poller = createPoller({
    fetchImpl: server.fetchImpl,
    liveUrl: LIVE,
    fallbackUrl: FALLBACK,
    intervalMs: 5,
    staleThresholdMs: 40,
    onData: (snapshot, source) => snapshots.push({ snapshot, source }),
    onStatus: (state, info) => statuses.push({ state, info }),
    ...options,
  });
  return { server, snapshots, statuses, poller };
}

test('initial load happens immediately with live source', async () => {
  const { snapshots, statuses, poller } = setup();
  poller.start();
  await waitFor(() => snapshots.length === 1);
  poller.stop();
  assert.equal(snapshots[0].source, 'live');
  assert.equal(snapshots[0].snapshot.snapshotVersion, 1);
  assert.ok(statuses.some((s) => s.state === 'live'));
});

test('equal version with newer generatedAt refreshes status without redraw', async () => {
  const { server, snapshots, statuses, poller } = setup();
  poller.start();
  await waitFor(() => snapshots.length === 1);
  const heartbeatBefore = statuses.filter((s) => s.state === 'live').length;
  server.set({ type: 'body', body: { ...valid, generatedAt: '2026-09-01T12:01:00+08:00' } });
  await sleep(40);
  poller.stop();
  assert.equal(snapshots.length, 1, 'same version must not redraw');
  const heartbeatAfter = statuses.filter((s) => s.state === 'live').length;
  assert.ok(heartbeatAfter > heartbeatBefore, 'status must refresh via heartbeat');
  const last = statuses[statuses.length - 1];
  assert.equal(last.state, 'live');
  assert.equal(last.info.lastSnapshotVersion, 1);
});

test('a new version redraws exactly once even in the same second', async () => {
  const { server, snapshots, poller } = setup();
  poller.start();
  await waitFor(() => snapshots.length === 1);
  // same generatedAt second, different version
  server.set({ type: 'body', body: { ...valid, snapshotVersion: 2 } });
  await waitFor(() => snapshots.length === 2);
  await sleep(30);
  poller.stop();
  assert.equal(snapshots.length, 2, 'new version must redraw exactly once');
  assert.equal(snapshots[1].snapshot.snapshotVersion, 2);
});

test('http error preserves previous snapshot then recovers', async () => {
  const { server, snapshots, statuses, poller } = setup();
  poller.start();
  await waitFor(() => snapshots.length === 1);
  server.set({ type: 'http' });
  await waitFor(() => statuses.some((s) => s.state === 'error'));
  await sleep(30);
  assert.equal(snapshots.length, 1, 'failed fetch must not redraw');
  server.set({ type: 'body', body: { ...valid, snapshotVersion: 3 } });
  await waitFor(() => snapshots.length === 2);
  poller.stop();
  assert.equal(snapshots[1].snapshot.snapshotVersion, 3);
  assert.equal(statuses[statuses.length - 1].state, 'live');
});

test('malformed json preserves previous snapshot', async () => {
  const { server, snapshots, statuses, poller } = setup();
  poller.start();
  await waitFor(() => snapshots.length === 1);
  server.set({ type: 'malformed' });
  await waitFor(() => statuses.some((s) => s.state === 'error'));
  await sleep(30);
  poller.stop();
  assert.equal(snapshots.length, 1);
  assert.equal(snapshots[0].snapshot.snapshotVersion, 1);
});

test('invalid contract preserves previous snapshot', async () => {
  const { server, snapshots, statuses, poller } = setup();
  poller.start();
  await waitFor(() => snapshots.length === 1);
  server.set({ type: 'invalid' });
  await waitFor(() => statuses.some((s) => s.state === 'error'));
  await sleep(30);
  poller.stop();
  assert.equal(snapshots.length, 1);
  assert.equal(snapshots[0].snapshot.snapshotVersion, 1);
});

test('no previous snapshot plus live failure loads fallback as cached', async () => {
  const server = makeServer();
  server.set({ type: 'http' });
  const snapshots = [];
  const statuses = [];
  const poller = createPoller({
    fetchImpl: server.fetchImpl,
    liveUrl: LIVE,
    fallbackUrl: FALLBACK,
    intervalMs: 5,
    onData: (snapshot, source) => snapshots.push({ snapshot, source }),
    onStatus: (state) => statuses.push(state),
  });
  poller.start();
  await waitFor(() => snapshots.length === 1);
  poller.stop();
  assert.equal(snapshots[0].source, 'cached');
  assert.equal(snapshots[0].snapshot.snapshotVersion, fallbackRaw.snapshotVersion);
  assert.ok(statuses.includes('cached'));
});

test('cached fallback then live recovery emits new version from live', async () => {
  const server = makeServer();
  server.set({ type: 'http' });
  const snapshots = [];
  const poller = createPoller({
    fetchImpl: server.fetchImpl,
    liveUrl: LIVE,
    fallbackUrl: FALLBACK,
    intervalMs: 5,
    onData: (snapshot, source) => snapshots.push({ snapshot, source }),
    onStatus: () => {},
  });
  poller.start();
  await waitFor(() => snapshots.length === 1);
  assert.equal(snapshots[0].source, 'cached');
  server.set({ type: 'body', body: { ...valid, snapshotVersion: 9 } });
  await waitFor(() => snapshots.length === 2);
  poller.stop();
  assert.equal(snapshots[1].source, 'live');
  assert.equal(snapshots[1].snapshot.snapshotVersion, 9);
});

test('connection staleness is driven by last successful fetch time', async () => {
  const { server, snapshots, statuses, poller } = setup();
  poller.start();
  await waitFor(() => snapshots.length === 1);
  const liveInfo = statuses.find((s) => s.state === 'live').info;
  const lastSuccess = liveInfo.lastSuccessfulFetchAt;
  assert.equal(typeof lastSuccess, 'number');
  server.set({ type: 'pending' }); // fetch never resolves
  await waitFor(() => statuses.some((s) => s.state === 'stale'), 800);
  poller.stop();
  assert.ok(Date.now() - lastSuccess >= 40, 'stale must wait past the threshold');
  assert.equal(snapshots.length, 1, 'stale period must not redraw');
});
