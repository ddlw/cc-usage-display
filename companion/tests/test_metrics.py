import sqlite3
import unittest
from datetime import datetime, timezone

from cc_usage_display.metrics import build_snapshot


SCHEMA = """
CREATE TABLE proxy_request_logs (
    request_id TEXT PRIMARY KEY,
    provider_id TEXT NOT NULL,
    app_type TEXT NOT NULL,
    model TEXT NOT NULL,
    request_model TEXT,
    input_tokens INTEGER NOT NULL DEFAULT 0,
    output_tokens INTEGER NOT NULL DEFAULT 0,
    cache_read_tokens INTEGER NOT NULL DEFAULT 0,
    cache_creation_tokens INTEGER NOT NULL DEFAULT 0,
    total_cost_usd TEXT NOT NULL DEFAULT '0',
    latency_ms INTEGER NOT NULL DEFAULT 0,
    first_token_ms INTEGER,
    status_code INTEGER NOT NULL DEFAULT 200,
    error_message TEXT,
    session_id TEXT,
    created_at INTEGER NOT NULL,
    data_source TEXT NOT NULL DEFAULT 'proxy',
    pricing_model TEXT,
    input_token_semantics INTEGER NOT NULL DEFAULT 0
);
"""


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.conn = sqlite3.connect(":memory:")
        self.conn.executescript(SCHEMA)
        self.now = datetime(2026, 10, 24, 12, 0, tzinfo=timezone.utc)

    def tearDown(self):
        self.conn.close()

    def insert(self, **values):
        defaults = {
            "request_id": "r",
            "provider_id": "p",
            "app_type": "codex",
            "model": "gpt-test",
            "request_model": "gpt-test",
            "input_tokens": 1000,
            "output_tokens": 100,
            "cache_read_tokens": 600,
            "cache_creation_tokens": 100,
            "total_cost_usd": "0.001",
            "latency_ms": 1200,
            "first_token_ms": 300,
            "status_code": 200,
            "session_id": "s1",
            "created_at": int(self.now.timestamp()) - 60,
            "data_source": "proxy",
            "input_token_semantics": 1,
        }
        defaults.update(values)
        keys = ", ".join(defaults)
        marks = ", ".join("?" for _ in defaults)
        self.conn.execute(
            f"INSERT INTO proxy_request_logs ({keys}) VALUES ({marks})",
            tuple(defaults.values()),
        )

    def test_normalizes_total_input_and_costs(self):
        self.insert()
        self.insert(
            request_id="r2",
            model="claude-test",
            app_type="claude",
            input_tokens=200,
            cache_read_tokens=50,
            cache_creation_tokens=0,
            input_token_semantics=2,
            status_code=502,
            session_id="s2",
            created_at=int(self.now.timestamp()) - 120,
        )

        snapshot = build_snapshot(self.conn, self.now, usd_to_cny=7.0)

        # codex fresh input = 1000 - 600 - 100 = 300.
        # claude is already fresh input = 200.
        self.assertEqual(snapshot.input_30d, 500)
        self.assertEqual(snapshot.output_30d, 200)
        self.assertEqual(snapshot.cache_read_30d, 650)
        self.assertEqual(snapshot.cache_write_30d, 100)
        self.assertEqual(snapshot.total_calls_30d, 2)
        self.assertEqual(snapshot.session_count_30d, 2)
        self.assertEqual(snapshot.success_permille, 500)
        self.assertEqual(snapshot.errors_today, 1)
        self.assertEqual(snapshot.cost_today_cny_micros, 14000)
        self.assertEqual(snapshot.cost_30d_cny_micros, 14000)
        self.assertEqual(snapshot.usd_to_cny, 7.0)
        self.assertEqual(snapshot.latest_model, "gpt-test")
        self.assertEqual(snapshot.latest_status, 200)
        self.assertFalse(snapshot.latest_failed)

    def test_cache_hit_uses_fresh_input(self):
        self.insert()
        snapshot = build_snapshot(self.conn, self.now)
        # 600 / (300 + 100 + 600) = 600 / 1000
        self.assertEqual(snapshot.cache_hit_permille, 600)
        self.assertEqual(snapshot.tokens_today, 1100)

    def test_empty_database_is_safe(self):
        snapshot = build_snapshot(self.conn, self.now)
        self.assertEqual(snapshot.tokens_today, 0)
        self.assertEqual(snapshot.calls_today, 0)
        self.assertEqual(snapshot.success_permille, 1000)
        self.assertEqual(snapshot.latest_model, "-")
        self.assertEqual(snapshot.top_model, "-")
        self.assertEqual(snapshot.cost_today_cny_micros, 0)
        self.assertEqual(snapshot.cost_30d_cny_micros, 0)

    def test_matching_session_row_is_not_double_counted(self):
        created_at = int(self.now.timestamp()) - 60
        self.insert(request_id="proxy-row", created_at=created_at)
        self.insert(
            request_id="session-row",
            session_id="session-1",
            created_at=created_at + 120,
            data_source="codex_session",
        )

        snapshot = build_snapshot(self.conn, self.now)

        self.assertEqual(snapshot.calls_today, 1)
        self.assertEqual(snapshot.tokens_today, 1100)
        self.assertEqual(snapshot.total_calls_30d, 1)
        self.assertEqual(snapshot.top_model_calls, 1)

    def test_session_only_usage_is_included(self):
        self.insert(
            request_id="session-only",
            session_id="session-2",
            created_at=int(self.now.timestamp()) - 60,
            data_source="codex_session",
        )

        snapshot = build_snapshot(self.conn, self.now)

        self.assertEqual(snapshot.calls_today, 1)
        self.assertEqual(snapshot.tokens_today, 1100)
        self.assertEqual(snapshot.total_calls_30d, 1)

    def test_session_outside_dedup_window_is_included(self):
        now = int(self.now.timestamp())
        self.insert(request_id="proxy-row", created_at=now - 1000)
        self.insert(
            request_id="session-row",
            session_id="session-3",
            created_at=now - 100,
            data_source="codex_session",
        )

        snapshot = build_snapshot(self.conn, self.now)

        self.assertEqual(snapshot.calls_today, 2)
        self.assertEqual(snapshot.tokens_today, 2200)

    def test_failed_proxy_does_not_hide_session_row(self):
        now = int(self.now.timestamp())
        self.insert(
            request_id="failed-proxy",
            status_code=500,
            created_at=now - 120,
        )
        self.insert(
            request_id="session-row",
            session_id="session-4",
            created_at=now - 60,
            data_source="codex_session",
        )

        snapshot = build_snapshot(self.conn, self.now)

        self.assertEqual(snapshot.calls_today, 2)
        self.assertEqual(snapshot.errors_today, 1)


if __name__ == "__main__":
    unittest.main()
