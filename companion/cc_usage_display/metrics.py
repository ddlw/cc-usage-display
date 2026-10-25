from __future__ import annotations

import sqlite3
from dataclasses import asdict, dataclass
from datetime import datetime, timedelta
from typing import Optional

from .config import DEFAULT_USD_TO_CNY


APP_TYPES_WITH_CACHE_INCLUSIVE_INPUT = ("codex", "gemini", "grokbuild")
SESSION_DATA_SOURCES = (
    "session_log",
    "codex_session",
    "gemini_session",
    "opencode_session",
)
SESSION_PROXY_DEDUP_WINDOW_SECONDS = 10 * 60


def _fresh_input_sql() -> str:
    apps = ", ".join(f"'{app}'" for app in APP_TYPES_WITH_CACHE_INCLUSIVE_INPUT)
    return (
        "CASE "
        "WHEN input_token_semantics = 2 THEN input_tokens "
        f"WHEN app_type IN ({apps}) AND input_token_semantics = 1 "
        "AND input_tokens >= (cache_read_tokens + cache_creation_tokens) "
        "THEN input_tokens - cache_read_tokens - cache_creation_tokens "
        f"WHEN app_type IN ({apps}) AND input_token_semantics = 0 "
        "AND input_tokens >= cache_read_tokens "
        "THEN input_tokens - cache_read_tokens "
        "ELSE input_tokens END"
    )


FRESH_INPUT = _fresh_input_sql()
REAL_TOTAL = (
    f"(({FRESH_INPUT}) + output_tokens + cache_read_tokens + "
    "cache_creation_tokens)"
)


def _effective_usage_filter(alias: str = "l") -> str:
    """Match CC Switch's dashboard filter for imported session rows.

    CC Switch imports some usage from local session logs. A successful proxy
    request can represent the same API call, so its dashboard hides a session
    row when a matching proxy row exists within +/-10 minutes. Keep this SQL in
    sync with ``effective_usage_log_filter`` in CC Switch.
    """
    data_source = f"COALESCE({alias}.data_source, 'proxy')"
    session_sources = ", ".join(f"'{source}'" for source in SESSION_DATA_SOURCES)
    session_only_sources = ", ".join(
        f"'{source}'"
        for source in ("codex_session", "gemini_session", "opencode_session")
    )
    app_type = f"{alias}.app_type"
    same_app_type = (
        f"p.app_type IN ({app_type}, CASE WHEN {app_type} = 'claude' "
        f"THEN 'claude-desktop' ELSE {app_type} END)"
    )
    return f"""
        NOT (
            {data_source} IN ({session_sources})
            AND EXISTS (
                SELECT 1
                FROM proxy_request_logs p
                WHERE COALESCE(p.data_source, 'proxy') = 'proxy'
                  AND p.status_code >= 200 AND p.status_code < 300
                  AND {same_app_type}
                  AND p.input_tokens = {alias}.input_tokens
                  AND p.output_tokens = {alias}.output_tokens
                  AND p.cache_read_tokens = {alias}.cache_read_tokens
                  AND (
                      p.cache_creation_tokens = {alias}.cache_creation_tokens
                      OR (
                          {alias}.cache_creation_tokens = 0
                          AND {data_source} IN ({session_only_sources})
                      )
                      OR (
                          p.cache_creation_tokens = 0
                          AND p.input_token_semantics = 0
                          AND {data_source} = 'codex_session'
                      )
                  )
                  AND p.created_at BETWEEN
                      {alias}.created_at - {SESSION_PROXY_DEDUP_WINDOW_SECONDS}
                      AND {alias}.created_at + {SESSION_PROXY_DEDUP_WINDOW_SECONDS}
                  AND (
                      LOWER(p.model) = LOWER({alias}.model)
                      OR LOWER(p.model) = 'unknown'
                      OR LOWER({alias}.model) = 'unknown'
                  )
            )
        )
    """


@dataclass(frozen=True)
class Snapshot:
    generated_at: int
    utc_offset_minutes: int
    tokens_today: int
    tokens_7d: int
    cost_today_cny_micros: int
    calls_today: int
    errors_today: int
    success_permille: int
    avg_ttft_ms: int
    latest_model: str
    latest_status: int
    latest_latency_ms: int
    latest_failed: bool
    session_count_30d: int
    total_calls_30d: int
    input_30d: int
    output_30d: int
    cache_read_30d: int
    cache_write_30d: int
    cost_30d_cny_micros: int
    usd_to_cny: float
    cache_hit_permille: int
    top_model: str
    top_model_calls: int

    def payload(self) -> dict:
        return asdict(self)


def _aggregate(conn: sqlite3.Connection, start: int, end: int) -> dict:
    effective_filter = _effective_usage_filter("l")
    sql = f"""
        SELECT
            COUNT(*),
            COALESCE(SUM(CASE WHEN l.status_code >= 200
                                   AND l.status_code < 300
                              THEN 1 ELSE 0 END), 0),
            COALESCE(SUM(CASE WHEN l.status_code >= 400
                              THEN 1 ELSE 0 END), 0),
            COALESCE(SUM({REAL_TOTAL}), 0),
            COALESCE(SUM({FRESH_INPUT}), 0),
            COALESCE(SUM(l.output_tokens), 0),
            COALESCE(SUM(l.cache_read_tokens), 0),
            COALESCE(SUM(l.cache_creation_tokens), 0),
            COALESCE(SUM(CAST(l.total_cost_usd AS REAL)), 0),
            COALESCE(AVG(CASE WHEN l.first_token_ms IS NOT NULL
                                   AND l.first_token_ms > 0
                              THEN l.first_token_ms END), 0)
        FROM proxy_request_logs l
        WHERE l.created_at >= ? AND l.created_at <= ?
          AND {effective_filter}
    """
    row = conn.execute(sql, (start, end)).fetchone()
    if row is None:
        return {}
    keys = (
        "calls",
        "successes",
        "errors",
        "tokens",
        "fresh_input",
        "output",
        "cache_read",
        "cache_write",
        "cost_usd",
        "avg_ttft_ms",
    )
    return dict(zip(keys, row))


def _latest_request(conn: sqlite3.Connection) -> dict:
    effective_filter = _effective_usage_filter("l")
    row = conn.execute(
        f"""
        SELECT l.model, l.status_code, l.latency_ms, l.error_message
        FROM proxy_request_logs l
        WHERE {effective_filter}
        ORDER BY l.created_at DESC, l.rowid DESC
        LIMIT 1
        """
    ).fetchone()
    if row is None:
        return {
            "model": "-",
            "status": 0,
            "latency_ms": 0,
            "failed": False,
        }
    model, status, latency_ms, error_message = row
    status = int(status or 0)
    return {
        "model": str(model or "-")[:31],
        "status": status,
        "latency_ms": max(0, int(latency_ms or 0)),
        "failed": bool(error_message) or status < 200 or status >= 400,
    }


def _top_model(conn: sqlite3.Connection, start: int, end: int) -> tuple:
    effective_filter = _effective_usage_filter("l")
    row = conn.execute(
        f"""
        SELECT l.model, COUNT(*) AS calls
        FROM proxy_request_logs l
        WHERE l.created_at >= ? AND l.created_at <= ?
          AND l.model IS NOT NULL AND l.model <> ''
          AND {effective_filter}
        GROUP BY l.model
        ORDER BY calls DESC, l.model ASC
        LIMIT 1
        """,
        (start, end),
    ).fetchone()
    if row is None:
        return "-", 0
    return str(row[0])[:23], int(row[1])


def _session_count(conn: sqlite3.Connection, start: int, end: int) -> int:
    effective_filter = _effective_usage_filter("l")
    row = conn.execute(
        f"""
        SELECT COUNT(DISTINCT CASE
            WHEN l.session_id IS NOT NULL AND l.session_id <> '' 
            THEN l.session_id
        END)
        FROM proxy_request_logs l
        WHERE l.created_at >= ? AND l.created_at <= ?
          AND {effective_filter}
        """,
        (start, end),
    ).fetchone()
    return int(row[0] or 0) if row else 0


def _cny_micros(cost_usd: float, usd_to_cny: float) -> int:
    return max(
        0, int(round(float(cost_usd or 0) * float(usd_to_cny) * 1_000_000))
    )


def _permille(numerator: int, denominator: int) -> int:
    if denominator <= 0:
        return 0
    return max(0, min(1000, int(round(numerator * 1000 / denominator))))


def build_snapshot(
    conn: sqlite3.Connection,
    now: Optional[datetime] = None,
    usd_to_cny: float = DEFAULT_USD_TO_CNY,
) -> Snapshot:
    rate = float(usd_to_cny)
    if rate <= 0:
        raise ValueError("usd_to_cny must be positive")

    current = now or datetime.now().astimezone()
    if current.tzinfo is None:
        current = current.astimezone()

    today_start_dt = current.replace(hour=0, minute=0, second=0, microsecond=0)
    week_start_dt = today_start_dt - timedelta(days=6)
    month_start_dt = today_start_dt - timedelta(days=29)

    end = int(current.timestamp())
    today_start = int(today_start_dt.timestamp())
    week_start = int(week_start_dt.timestamp())
    month_start = int(month_start_dt.timestamp())

    today = _aggregate(conn, today_start, end)
    week = _aggregate(conn, week_start, end)
    month = _aggregate(conn, month_start, end)
    latest = _latest_request(conn)
    top_model, top_model_calls = _top_model(conn, month_start, end)

    calls_today = int(today.get("calls", 0))
    successes_today = int(today.get("successes", 0))
    cacheable_input = (
        int(month.get("fresh_input", 0))
        + int(month.get("cache_write", 0))
        + int(month.get("cache_read", 0))
    )
    offset = current.utcoffset()
    utc_offset_minutes = int(offset.total_seconds() // 60) if offset else 0

    return Snapshot(
        generated_at=end,
        utc_offset_minutes=utc_offset_minutes,
        tokens_today=int(today.get("tokens", 0)),
        tokens_7d=int(week.get("tokens", 0)),
        cost_today_cny_micros=_cny_micros(today.get("cost_usd", 0), rate),
        calls_today=calls_today,
        errors_today=int(today.get("errors", 0)),
        success_permille=(
            _permille(successes_today, calls_today) if calls_today else 1000
        ),
        avg_ttft_ms=int(round(float(today.get("avg_ttft_ms", 0) or 0))),
        latest_model=latest["model"],
        latest_status=latest["status"],
        latest_latency_ms=latest["latency_ms"],
        latest_failed=latest["failed"],
        session_count_30d=_session_count(conn, month_start, end),
        total_calls_30d=int(month.get("calls", 0)),
        input_30d=int(month.get("fresh_input", 0)),
        output_30d=int(month.get("output", 0)),
        cache_read_30d=int(month.get("cache_read", 0)),
        cache_write_30d=int(month.get("cache_write", 0)),
        cost_30d_cny_micros=_cny_micros(month.get("cost_usd", 0), rate),
        usd_to_cny=rate,
        cache_hit_permille=_permille(
            int(month.get("cache_read", 0)), cacheable_input
        ),
        top_model=top_model,
        top_model_calls=top_model_calls,
    )


def connect_read_only(db_path: str) -> sqlite3.Connection:
    conn = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True, timeout=2)
    conn.execute("PRAGMA query_only = ON")
    return conn
