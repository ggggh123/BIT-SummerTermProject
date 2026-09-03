// Polling engine for the dashboard snapshot contract.
// Guarantees: a snapshot is accepted only after full contract validation;
// charts (onData) transition exactly once per new snapshotVersion;
// heartbeats (same version, newer generatedAt) only refresh status;
// HTTP errors / malformed JSON / invalid contracts keep the last good data;
// when no previous snapshot exists, the explicit fallback loads as `cached`.
// Statuses: live | stale | cached | error.

import { validateSnapshot, isDashboardStale } from './contracts.js';

export function createPoller({
  fetchImpl,
  liveUrl,
  fallbackUrl,
  intervalMs = 2000,
  staleThresholdMs = 10000,
  onData,
  onStatus,
}) {
  let lastSnapshot = null;
  let lastSnapshotVersion = null;
  let lastSuccessfulFetchAt = null;
  let lastEmittedState = null;
  let fallbackTried = false;
  let fetchInFlight = false;
  let timer = null;
  let running = false;

  function emit(state, info = {}) {
    lastEmittedState = state;
    onStatus(state, {
      source: info.source ?? null,
      heartbeat: info.heartbeat ?? false,
      lastSnapshotVersion,
      lastSuccessfulFetchAt,
      error: info.error ?? null,
    });
  }

  function withCacheBust(url) {
    return `${url}${url.includes('?') ? '&' : '?'}ts=${Date.now()}`;
  }

  async function fetchJson(url) {
    const res = await fetchImpl(withCacheBust(url), { cache: 'no-store' });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return await res.json(); // malformed JSON throws before any state change
  }

  function accept(snapshot, source) {
    validateSnapshot(snapshot); // throws before any state change
    const version = snapshot.snapshotVersion;
    const hadPrevious = lastSnapshot !== null;
    const isNew = lastSnapshotVersion === null || version !== lastSnapshotVersion;
    lastSnapshot = snapshot;
    lastSnapshotVersion = version;
    lastSuccessfulFetchAt = Date.now();
    if (source === 'live') fallbackTried = false;
    if (isNew) onData(snapshot, source);
    emit(source === 'cached' ? 'cached' : 'live', { source, heartbeat: hadPrevious && !isNew });
  }

  async function tryFetch(url, source) {
    const snapshot = await fetchJson(url);
    accept(snapshot, source);
  }

  async function tick() {
    // Connection staleness is judged only from the last successful fetch time;
    // a stale state keeps old data on screen but never stops the polling loop.
    if (
      lastSuccessfulFetchAt !== null &&
      isDashboardStale(lastSuccessfulFetchAt, Date.now(), staleThresholdMs) &&
      lastEmittedState !== 'stale'
    ) {
      emit('stale');
    }
    if (fetchInFlight) return;
    fetchInFlight = true;
    try {
      await tryFetch(liveUrl, 'live');
    } catch (liveError) {
      if (lastSnapshot === null && !fallbackTried && fallbackUrl) {
        // No previous snapshot: surface the explicit cached demo snapshot.
        fallbackTried = true;
        try {
          await tryFetch(fallbackUrl, 'cached');
        } catch (fallbackError) {
          emit('error', { error: fallbackError.message });
        }
      } else {
        emit('error', { error: liveError.message });
      }
    } finally {
      fetchInFlight = false;
    }
  }

  function start() {
    if (running) return;
    running = true;
    void tick();
    timer = setInterval(() => {
      void tick();
    }, intervalMs);
  }

  function stop() {
    running = false;
    if (timer !== null) {
      clearInterval(timer);
      timer = null;
    }
  }

  return {
    start,
    stop,
    getState: () => ({
      lastSnapshot,
      lastSnapshotVersion,
      lastSuccessfulFetchAt,
      state: lastEmittedState,
    }),
  };
}
