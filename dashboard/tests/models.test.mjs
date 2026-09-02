import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  buildRevenueOption,
  buildLoadForecastOption,
  buildStatusOption,
  buildRankingOption,
  buildStationOption,
} from '../assets/models.js';

const here = dirname(fileURLToPath(import.meta.url));
const valid = JSON.parse(readFileSync(join(here, 'fixtures', 'valid_snapshot.json'), 'utf8'));

const assertHasNoUndefined = (value, path = 'root') => {
  if (value === undefined) throw new Error(`undefined at ${path}`);
  if (typeof value === 'number') {
    assert.ok(Number.isFinite(value), `non-finite number at ${path}: ${value}`);
  } else if (Array.isArray(value)) {
    value.forEach((v, i) => assertHasNoUndefined(v, `${path}[${i}]`));
  } else if (value !== null && typeof value === 'object') {
    Object.entries(value).forEach(([k, v]) => assertHasNoUndefined(v, `${path}.${k}`));
  }
};

const withNewStation = (stationId) => {
  const snapshot = structuredClone(valid);
  snapshot.stations.push({
    stationId,
    name: '测试新站',
    latitude: 39.8,
    longitude: 116.2,
    idleCount: 0,
    chargerCount: 8,
    onlineRate: 100,
    revenueFen: 0,
    forecastEnabled: false,
  });
  return snapshot;
};

test('revenue option has 7 labels and 7 amounts', () => {
  const option = buildRevenueOption(valid);
  assert.equal(option.xAxis.data.length, 7);
  assert.equal(option.series[0].data.length, 7);
  option.xAxis.data.forEach((label) => assert.match(label, /^\d{2}-\d{2}$/));
});

test('load+forecast option splits 24 historical and 24 future points', () => {
  const option = buildLoadForecastOption(valid, 1);
  assert.equal(option.xAxis.data.length, 48);
  const actual = option.series.find((s) => s.name === '实际负荷');
  const forecast = option.series.find((s) => s.name === '预测负荷');
  assert.equal(actual.data.filter((v) => v !== null).length, 24);
  assert.equal(forecast.data.filter((v) => v !== null).length, 24);
  assert.ok(actual.data.slice(0, 24).every((v) => v !== null));
  assert.ok(forecast.data.slice(0, 24).every((v) => v === null));
});

test('a non-forecast station returns an explicit no-forecast model', () => {
  const snapshot = withNewStation(99);
  const option = buildLoadForecastOption(snapshot, 99);
  assert.equal(option.noForecast, true);
  assert.equal(option.stationName, '测试新站');
});

test('status option keeps idle,reserved,charging,fault,restarting order', () => {
  const option = buildStatusOption(valid);
  const data = option.series[0].data;
  assert.deepEqual(
    data.map((d) => d.name),
    ['空闲', '已预约', '充电中', '故障', '重启中'],
  );
  assert.deepEqual(
    data.map((d) => d.value),
    [valid.chargerStatus.idle, valid.chargerStatus.reserved, valid.chargerStatus.charging, valid.chargerStatus.fault, valid.chargerStatus.restarting],
  );
});

test('ranking is descending by utilization', () => {
  const option = buildRankingOption(valid);
  const values = option.series[0].data.map((d) => d.value);
  const util = valid.stationRanking.map((r) => r.utilizationRate).sort((a, b) => b - a);
  assert.deepEqual(values, util);
  for (let i = 1; i < values.length; i += 1) {
    assert.ok(values[i - 1] >= values[i], 'ranking must be non-increasing');
  }
});

test('station scatter uses longitude/latitude and rich tooltip data', () => {
  const option = buildStationOption(valid);
  const items = option.series[0].data;
  assert.equal(items.length, valid.stations.length);
  for (const item of items) {
    assert.equal(item.value[0], item.longitude);
    assert.equal(item.value[1], item.latitude);
    assert.ok(item.name);
    assert.ok(Number.isInteger(item.idleCount));
    assert.ok(Number.isInteger(item.total));
    assert.ok(Number.isFinite(item.revenueFen));
    assert.equal(typeof item.forecastEnabled, 'boolean');
  }
});

test('peak forecast points are marked', () => {
  const option = buildLoadForecastOption(valid, 1);
  const forecastSeries = option.series.find((s) => s.name === '预测负荷');
  const marks = forecastSeries.markPoint ? forecastSeries.markPoint.data : [];
  assert.ok(marks.length >= 2, 'expected at least two peak marks');
  const fc = valid.forecast24h.filter((f) => f.stationId === 1);
  const peakLoads = fc.filter((f) => f.isPeak).map((f) => f.predictedLoadKw);
  const markedLoads = marks.map((m) => m.coord[1]);
  assert.deepEqual(markedLoads, peakLoads);
});

test('no model contains undefined or non-finite numbers', () => {
  const load1 = buildLoadForecastOption(valid, 1);
  const load99 = buildLoadForecastOption(withNewStation(99), 99);
  for (const option of [
    buildRevenueOption(valid),
    load1,
    load99,
    buildStatusOption(valid),
    buildRankingOption(valid),
    buildStationOption(valid),
  ]) {
    assertHasNoUndefined(option);
  }
});
