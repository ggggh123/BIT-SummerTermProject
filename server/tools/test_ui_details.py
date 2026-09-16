"""城市能源指挥舱：质量明细及 Q9 缺坐标的 API 回归。"""
import json
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from flask import Flask
from api import quality, user


class UiDetailsTests(unittest.TestCase):
    def setUp(self):
        self.app = Flask(__name__)
        self.app.register_blueprint(quality.bp, url_prefix='/api')
        self.app.register_blueprint(user.bp, url_prefix='/api')
        self.client = self.app.test_client()
        self.time = patch('services.envelope.generated_at', return_value='2026-09-15T10:55:00+08:00')
        self.time.start()
        self.addCleanup(self.time.stop)

    def get_quality(self, meta):
        def query(sql):
            if 'ads_quality_meta' in sql: return [{'key': k, 'value': v} for k, v in meta.items()]
            if 'ads_quality_table' in sql: return [{'name': 'ods_orders', 'rows_before': 100, 'rows_after': 70}]
            return [{'rule': 'R01', 'type': '测试规则', 'injected': 10, 'detected': 12, 'handled': 12, 'recall': .8}]
        with patch.object(quality.ads, 'query', side_effect=query), patch.object(quality.ads, 'meta_map', return_value={'runId': 'ads-test'}):
            return self.client.get('/api/quality/summary').get_json()

    def test_exact_metrics_and_cascade_are_read_not_inferred(self):
        result = self.get_quality({
            'source': 'prl-quality-report', 'qualityRunId': 'q-1', 'policyVersion': '0.2.0-draft',
            'readyForTeamDelivery': 'false', 'pendingPolicyNotes': '["待确认"]',
            'exactMetrics': json.dumps({'R01': {'truePositive': 8, 'falsePositive': 4, 'falseNegative': 2, 'precision': 2/3, 'recall': .8}}),
            'tableDetails': json.dumps({'ods_orders': {'cascadeAffectedRows': 7}}),
        })
        self.assertEqual(result['code'], 0)
        data = result['data']
        self.assertEqual(data['issues'][0]['falsePositive'], 4)
        self.assertEqual(data['issues'][0]['falseNegative'], 2)
        self.assertEqual(data['tables'][0]['cascadeAffectedRows'], 7)  # 不是减少的 30 行
        self.assertFalse(data['readyForTeamDelivery'])
        self.assertEqual(data['pendingPolicyNotes'], ['待确认'])
        self.assertIn('PRL', data['note'])

    def test_old_batch_missing_details_are_null_not_zero(self):
        data = self.get_quality({})['data']
        self.assertIsNone(data['readyForTeamDelivery'])
        self.assertIsNone(data['issues'][0]['falsePositive'])
        self.assertIsNone(data['issues'][0]['precision'])
        self.assertIsNone(data['tables'][0]['cascadeAffectedRows'])
        self.assertEqual(data['issues'][0]['recall'], .8)

    def test_zero_denominator_exact_recall_remains_null(self):
        data = self.get_quality({'exactMetrics': '{"R01":{"recall":null,"precision":null}}'})['data']
        self.assertIsNone(data['issues'][0]['recall'])

    def test_bad_optional_metadata_does_not_break_base_summary(self):
        data = self.get_quality({'readyForTeamDelivery': '1', 'exactMetrics': '{bad', 'tableDetails': '[]', 'pendingPolicyNotes': 'null'})['data']
        self.assertIsNone(data['readyForTeamDelivery'])
        self.assertIsNone(data['issues'][0]['truePositive'])
        self.assertEqual(data['detectedTotal'], 12)

    def test_null_coordinates_skipped_only_by_distance_endpoint(self):
        common = {'name': '站点', 'idle_cnt': 3, 'price_fen_per_kwh': 150, 'longitude': 116.4}
        rows = [{**common, 'station_id': 2, 'latitude': None}, {**common, 'station_id': 1, 'latitude': 39.9}]
        with patch.object(user.ads, 'stations', return_value=rows):
            body = self.client.get('/api/user/price-distance').get_json()
            prices = self.client.get('/api/user/price-compare').get_json()
        self.assertEqual(body['code'], 0)
        self.assertEqual([r['stationId'] for r in body['data']], [1])
        self.assertEqual(len(prices['data']['stations']), 2)
        self.assertGreater(body['data'][0]['distanceKm'], 0)


if __name__ == '__main__':
    unittest.main()
