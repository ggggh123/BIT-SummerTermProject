import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  validateSnapshot,
  formatFen,
  formatPercent,
  formatKwh,
  isDashboardStale,
  isForecastStale,
} from '../assets/contracts.js';

const here = dirname(fileURLToPath(import.meta.url));
const loadFixture = (name) => JSON.parse(readFileSync(join(here, 'fixtures', name), 'utf8'));
const valid = loadFixture('valid_snapshot.json');
const invalid = loadFixture('invalid_snapshot.json');

const rejects = (value, pattern) => assert.throws(() => validateSnapshot(value), pattern);
const mutateRow = (list, index, patch) =>
  list.map((row, i) => (i === index ? { ...row, ...patch } : row));

test('accepts the frozen valid snapshot', () => {
  assert.doesNotThrow(() => validateSnapshot(valid));
});

test('reports the missing field path', () => {
  rejects(invalid, /kpis\.todayRevenueFen/);
});

test('formats values consistently', () => {
  assert.equal(formatFen(123456), '¥1,234.56');
  assert.equal(formatPercent(87.5), '87.5%');
  assert.equal(formatKwh(12.345), '12.35 kWh');
});

test('rejects schemaVersion other than 1', () => {
  rejects({ ...valid, schemaVersion: 2 }, /schemaVersion/);
});

test('rejects non-positive or fractional snapshotVersion', () => {
  rejects({ ...valid, snapshotVersion: 0 }, /snapshotVersion/);
  rejects({ ...valid, snapshotVersion: 1.5 }, /snapshotVersion/);
});

test('rejects non-ISO generatedAt', () => {
  rejects({ ...valid, generatedAt: '2026-09-01 12:00:00' }, /generatedAt/);
  rejects({ ...valid, generatedAt: '2026-09-01T12:00:00' }, /generatedAt/);
});

test('rejects revenue7d length other than 7', () => {
  rejects({ ...valid, revenue7d: valid.revenue7d.slice(0, 6) }, /revenue7d/);
});

test('rejects actualLoad24h length other than 144', () => {
  rejects({ ...valid, actualLoad24h: valid.actualLoad24h.slice(0, 143) }, /actualLoad24h/);
});

test('rejects forecast24h length other than 0 or 144', () => {
  rejects({ ...valid, forecast24h: valid.forecast24h.slice(0, 100) }, /forecast24h/);
});

test('accepts a degraded snapshot with null run and empty forecast', () => {
  assert.doesNotThrow(() => validateSnapshot({ ...valid, forecastRun: null, forecast24h: [] }));
});

test('rejects status sum unequal to chargerStatus.total', () => {
  const broken = {
    ...valid,
    chargerStatus: { ...valid.chargerStatus, idle: valid.chargerStatus.idle + 1 },
  };
  rejects(broken, /chargerStatus/);
});

test('rejects unknown charger status keys', () => {
  const broken = {
    ...valid,
    chargerStatus: {
      ...valid.chargerStatus,
      offline: 2,
      total: valid.chargerStatus.total + 2,
    },
  };
  rejects(broken, /chargerStatus/);
});

test('rejects unknown congestion level', () => {
  const broken = { ...valid, forecast24h: mutateRow(valid.forecast24h, 0, { congestionLevel: 'busy' }) };
  rejects(broken, /congestionLevel/);
});

test('rejects nonfinite numbers and string values without coercion', () => {
  const infinite = { ...valid, actualLoad24h: mutateRow(valid.actualLoad24h, 0, { loadKw: Infinity }) };
  rejects(infinite, /loadKw/);
  const stringly = { ...valid, kpis: { ...valid.kpis, todayOrderCount: '42' } };
  rejects(stringly, /kpis\.todayOrderCount/);
});

test('rejects busy+idle not equaling the forecast-enabled station capacity', () => {
  const broken = { ...valid, forecast24h: mutateRow(valid.forecast24h, 0, { predictedIdleCount: 9 }) };
  rejects(broken, /capacity/);
});

test('keeps forecastRun and forecast24h paired', () => {
  rejects({ ...valid, forecastRun: null }, /forecastRun/);
  rejects({ ...valid, forecast24h: [] }, /forecast24h/);
});

test('requires canonical lowercase payloadHash and full run fields', () => {
  rejects(
    { ...valid, forecastRun: { ...valid.forecastRun, payloadHash: valid.forecastRun.payloadHash.toUpperCase() } },
    /payloadHash/,
  );
  const { activatedAt, ...runWithoutActivation } = valid.forecastRun;
  rejects({ ...valid, forecastRun: runWithoutActivation }, /activatedAt/);
});

test('dashboard staleness uses the last successful fetch time', () => {
  const now = Date.parse('2026-09-01T12:00:00+08:00');
  assert.equal(isDashboardStale(now - 9_999, now), false);
  assert.equal(isDashboardStale(now - 10_001, now), true);
});

test('forecast staleness uses activatedAt with a two hour threshold', () => {
  const now = Date.parse('2026-09-01T11:04:00+08:00');
  const activatedAt = '2026-09-01T09:05:00+08:00';
  assert.equal(isForecastStale(activatedAt, now), false);
  assert.equal(isForecastStale(activatedAt, now + 120_000), true);
});
