// Pure v1 dashboard snapshot validation and formatting.
// No DOM access: shared between the browser page and the Node unit tests.
// Every validator throws Error('snapshot.<field path>: <reason>') and never
// coerces strings into numbers.

const CHARGER_STATES = ['idle', 'reserved', 'charging', 'fault', 'restarting'];
const CONGESTION_LEVELS = ['low', 'medium', 'high'];
const REQUIRED_TOP_LEVEL_KEYS = [
  'schemaVersion',
  'snapshotVersion',
  'generatedAt',
  'kpis',
  'revenue7d',
  'actualLoad24h',
  'chargerStatus',
  'stationRanking',
  'stations',
  'events',
  'forecastRun',
  'forecast24h',
];
const ISO_8601_CN = /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\+08:00$/;
const CALENDAR_DATE = /^\d{4}-\d{2}-\d{2}$/;
const LOWERCASE_SHA256 = /^[0-9a-f]{64}$/;

function fail(path, reason) {
  throw new Error(`snapshot.${path}: ${reason}`);
}

function requireObject(value, path) {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) {
    fail(path, 'must be an object');
  }
  return value;
}

function requireArray(value, path) {
  if (!Array.isArray(value)) fail(path, 'must be an array');
  return value;
}

function requireString(value, path) {
  if (typeof value !== 'string' || value.length === 0) fail(path, 'must be a non-empty string');
  return value;
}

function requireInteger(value, path) {
  if (typeof value !== 'number' || !Number.isInteger(value)) fail(path, 'must be an integer');
  return value;
}

function requireNonNegativeInteger(value, path) {
  requireInteger(value, path);
  if (value < 0) fail(path, 'must be >= 0');
  return value;
}

function requireFiniteNumber(value, path) {
  if (typeof value !== 'number' || !Number.isFinite(value)) fail(path, 'must be a finite number');
  return value;
}

function requireNonNegativeNumber(value, path) {
  requireFiniteNumber(value, path);
  if (value < 0) fail(path, 'must be >= 0');
  return value;
}

function requirePercent(value, path) {
  requireFiniteNumber(value, path);
  if (value < 0 || value > 100) fail(path, 'must be within [0, 100]');
  return value;
}

function requireBoolean(value, path) {
  if (typeof value !== 'boolean') fail(path, 'must be a boolean');
  return value;
}

function requireIsoTimestamp(value, path) {
  requireString(value, path);
  if (!ISO_8601_CN.test(value) || Number.isNaN(Date.parse(value))) {
    fail(path, 'must be an ISO 8601 timestamp with +08:00');
  }
}

function requireCalendarDate(value, path) {
  requireString(value, path);
  if (!CALENDAR_DATE.test(value) || Number.isNaN(Date.parse(`${value}T00:00:00+08:00`))) {
    fail(path, 'must be a YYYY-MM-DD calendar date');
  }
}

export function validateSnapshot(value) {
  const root = requireObject(value, '');
  for (const key of REQUIRED_TOP_LEVEL_KEYS) {
    if (!(key in root)) fail(key, 'is required');
  }

  if (root.schemaVersion !== 1) fail('schemaVersion', 'must be 1');
  if (!Number.isInteger(root.snapshotVersion) || root.snapshotVersion < 1) {
    fail('snapshotVersion', 'must be a positive integer');
  }
  requireIsoTimestamp(root.generatedAt, 'generatedAt');

  const kpis = requireObject(root.kpis, 'kpis');
  requireNonNegativeInteger(kpis.todayRevenueFen, 'kpis.todayRevenueFen');
  requireNonNegativeNumber(kpis.todayEnergyKwh, 'kpis.todayEnergyKwh');
  requireNonNegativeInteger(kpis.todayOrderCount, 'kpis.todayOrderCount');
  requirePercent(kpis.onlineRate, 'kpis.onlineRate');
  requireNonNegativeInteger(kpis.idleChargerCount, 'kpis.idleChargerCount');

  const revenue7d = requireArray(root.revenue7d, 'revenue7d');
  if (revenue7d.length !== 7) fail('revenue7d', 'must contain exactly 7 daily points');
  revenue7d.forEach((row, i) => {
    requireObject(row, `revenue7d[${i}]`);
    requireCalendarDate(row.date, `revenue7d[${i}].date`);
    requireNonNegativeInteger(row.revenueFen, `revenue7d[${i}].revenueFen`);
  });

  const actualLoad24h = requireArray(root.actualLoad24h, 'actualLoad24h');
  if (actualLoad24h.length !== 144) fail('actualLoad24h', 'must contain exactly 144 hourly points (6 stations x 24h)');
  actualLoad24h.forEach((row, i) => {
    requireObject(row, `actualLoad24h[${i}]`);
    requireNonNegativeInteger(row.stationId, `actualLoad24h[${i}].stationId`);
    requireIsoTimestamp(row.observedAt, `actualLoad24h[${i}].observedAt`);
    requireNonNegativeNumber(row.loadKw, `actualLoad24h[${i}].loadKw`);
  });

  const chargerStatus = requireObject(root.chargerStatus, 'chargerStatus');
  const allowedStatusKeys = new Set([...CHARGER_STATES, 'total']);
  for (const key of Object.keys(chargerStatus)) {
    if (!allowedStatusKeys.has(key)) fail(`chargerStatus.${key}`, 'unknown charger status');
  }
  CHARGER_STATES.forEach((state) => {
    requireNonNegativeInteger(chargerStatus[state], `chargerStatus.${state}`);
  });
  requireNonNegativeInteger(chargerStatus.total, 'chargerStatus.total');
  const stateSum = CHARGER_STATES.reduce((acc, state) => acc + chargerStatus[state], 0);
  if (stateSum !== chargerStatus.total) {
    fail('chargerStatus.total', 'status counts must sum to total');
  }

  const stations = requireArray(root.stations, 'stations');
  const capacityByForecastStation = new Map();
  stations.forEach((row, i) => {
    requireObject(row, `stations[${i}]`);
    requireNonNegativeInteger(row.stationId, `stations[${i}].stationId`);
    requireString(row.name, `stations[${i}].name`);
    requireFiniteNumber(row.latitude, `stations[${i}].latitude`);
    requireFiniteNumber(row.longitude, `stations[${i}].longitude`);
    requireNonNegativeInteger(row.idleCount, `stations[${i}].idleCount`);
    requireNonNegativeInteger(row.chargerCount, `stations[${i}].chargerCount`);
    requirePercent(row.onlineRate, `stations[${i}].onlineRate`);
    requireNonNegativeInteger(row.revenueFen, `stations[${i}].revenueFen`);
    requireBoolean(row.forecastEnabled, `stations[${i}].forecastEnabled`);
    if (row.forecastEnabled) capacityByForecastStation.set(row.stationId, row.chargerCount);
  });

  const stationRanking = requireArray(root.stationRanking, 'stationRanking');
  stationRanking.forEach((row, i) => {
    requireObject(row, `stationRanking[${i}]`);
    requireNonNegativeInteger(row.stationId, `stationRanking[${i}].stationId`);
    requireString(row.name, `stationRanking[${i}].name`);
    requirePercent(row.utilizationRate, `stationRanking[${i}].utilizationRate`);
    requireNonNegativeInteger(row.idleCount, `stationRanking[${i}].idleCount`);
    requireNonNegativeInteger(row.chargerCount, `stationRanking[${i}].chargerCount`);
    requireNonNegativeInteger(row.revenueFen, `stationRanking[${i}].revenueFen`);
  });

  const events = requireArray(root.events, 'events');
  events.forEach((row, i) => {
    requireObject(row, `events[${i}]`);
    requireNonNegativeInteger(row.eventId, `events[${i}].eventId`);
    requireString(row.eventType, `events[${i}].eventType`);
    requireString(row.entityType, `events[${i}].entityType`);
    requireInteger(row.entityId, `events[${i}].entityId`);
    requireString(row.message, `events[${i}].message`);
    requireIsoTimestamp(row.createdAt, `events[${i}].createdAt`);
  });

  const forecast24h = requireArray(root.forecast24h, 'forecast24h');
  if (forecast24h.length !== 0 && forecast24h.length !== 144) {
    fail('forecast24h', 'must contain 0 (degraded) or exactly 144 records');
  }

  const run = root.forecastRun;
  if (run === null) {
    if (forecast24h.length !== 0) fail('forecastRun', 'must be null exactly when forecast24h is empty');
  } else {
    if (forecast24h.length === 0) fail('forecast24h', 'must be empty exactly when forecastRun is null');
    const runObject = requireObject(run, 'forecastRun');
    const runId = runObject.runId;
    const runIdValid =
      (typeof runId === 'string' && runId.length > 0) ||
      (typeof runId === 'number' && Number.isInteger(runId) && runId >= 0);
    if (!runIdValid) fail('forecastRun.runId', 'must be a non-empty string or a non-negative integer');
    requireIsoTimestamp(runObject.generatedAt, 'forecastRun.generatedAt');
    requireIsoTimestamp(runObject.dataCutoff, 'forecastRun.dataCutoff');
    requireIsoTimestamp(runObject.activatedAt, 'forecastRun.activatedAt');
    requireString(runObject.modelVersion, 'forecastRun.modelVersion');
    requireString(runObject.payloadHash, 'forecastRun.payloadHash');
    if (!LOWERCASE_SHA256.test(runObject.payloadHash)) {
      fail('forecastRun.payloadHash', 'must be a canonical lowercase sha-256 hex digest');
    }
    requireBoolean(runObject.stale, 'forecastRun.stale');
  }

  forecast24h.forEach((row, i) => {
    requireObject(row, `forecast24h[${i}]`);
    requireNonNegativeInteger(row.stationId, `forecast24h[${i}].stationId`);
    requireIsoTimestamp(row.forecastAt, `forecast24h[${i}].forecastAt`);
    requireInteger(row.horizonH, `forecast24h[${i}].horizonH`);
    if (row.horizonH < 1 || row.horizonH > 24) fail(`forecast24h[${i}].horizonH`, 'must be within [1, 24]');
    requireNonNegativeNumber(row.predictedLoadKw, `forecast24h[${i}].predictedLoadKw`);
    requireNonNegativeInteger(row.predictedBusyCount, `forecast24h[${i}].predictedBusyCount`);
    requireNonNegativeInteger(row.predictedIdleCount, `forecast24h[${i}].predictedIdleCount`);
    if (!CONGESTION_LEVELS.includes(row.congestionLevel)) {
      fail(`forecast24h[${i}].congestionLevel`, 'must be low, medium or high');
    }
    requireBoolean(row.isPeak, `forecast24h[${i}].isPeak`);
    const capacity = capacityByForecastStation.get(row.stationId);
    if (capacity === undefined) {
      fail(`forecast24h[${i}].stationId`, 'must reference a forecast-enabled station');
    }
    if (row.predictedBusyCount + row.predictedIdleCount !== capacity) {
      fail(`forecast24h[${i}]`, 'predictedBusyCount + predictedIdleCount must equal matching station capacity');
    }
  });

  return root;
}

// Decimal half-up rounding on the shortest round-trip string representation,
// avoiding binary floating point traps such as (12.345).toFixed(2) === '12.34'.
function roundHalfUp(value, digits) {
  const negative = value < 0;
  const [intPart = '0', fracPart = ''] = String(Math.abs(value)).split('.');
  const kept = BigInt((intPart || '0') + fracPart.slice(0, digits).padEnd(digits, '0'));
  const roundDigit = fracPart[digits] ?? '0';
  const incremented = kept + (roundDigit >= '5' ? 1n : 0n);
  const scaled = String(incremented).padStart(digits + 1, '0');
  const outInt = scaled.slice(0, scaled.length - digits);
  const outFrac = scaled.slice(scaled.length - digits);
  const result = digits > 0 ? `${outInt}.${outFrac}` : outInt;
  return negative ? `-${result}` : result;
}

export function formatFen(value) {
  if (typeof value !== 'number' || !Number.isInteger(value)) return '—';
  const sign = value < 0 ? '-' : '';
  const abs = Math.abs(value);
  const yuan = Math.trunc(abs / 100);
  const cents = String(abs % 100).padStart(2, '0');
  const grouped = String(yuan).replace(/\B(?=(\d{3})+(?!\d))/g, ',');
  return `¥${sign}${grouped}.${cents}`;
}

export function formatPercent(value) {
  if (typeof value !== 'number' || !Number.isFinite(value)) return '—';
  return `${roundHalfUp(value, 1)}%`;
}

export function formatKwh(value) {
  if (typeof value !== 'number' || !Number.isFinite(value)) return '—';
  return `${roundHalfUp(value, 2)} kWh`;
}

export function isDashboardStale(lastSuccessfulFetchAt, now, thresholdMs = 10000) {
  return now - lastSuccessfulFetchAt > thresholdMs;
}

export function isForecastStale(activatedAt, now, thresholdMs = 7200000) {
  const activated = Date.parse(activatedAt);
  if (Number.isNaN(activated)) return false;
  return now - activated > thresholdMs;
}
