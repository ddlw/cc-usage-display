from __future__ import annotations

import argparse
import json
import logging
import sys
from logging.handlers import RotatingFileHandler
from pathlib import Path
from typing import Optional

from .config import load_config
from .metrics import build_snapshot, connect_read_only
from .server import serve


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Serve CC Switch usage data to the ESP32 dashboard"
    )
    parser.add_argument("--config", help="config JSON path")
    parser.add_argument("--db", help="override the cc-switch.db path")
    parser.add_argument("--host", help="listen address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, help="listen port (default: 8787)")
    parser.add_argument("--token", help="override the device bearer token")
    parser.add_argument(
        "--log-file",
        default=str(Path.home() / ".cc-usage-display" / "companion.log"),
        help="companion log path (default: ~/.cc-usage-display/companion.log)",
    )
    parser.add_argument(
        "--once",
        action="store_true",
        help="print one snapshot JSON and exit",
    )
    parser.add_argument("--verbose", action="store_true")
    return parser


def configure_logging(verbose: bool, log_file: str) -> None:
    level = logging.DEBUG if verbose else logging.INFO
    formatter = logging.Formatter(
        "%(asctime)s %(levelname)s %(name)s %(message)s"
    )
    handlers: list[logging.Handler] = []

    if sys.stderr is not None:
        console = logging.StreamHandler()
        console.setFormatter(formatter)
        handlers.append(console)

    try:
        path = Path(log_file).expanduser()
        path.parent.mkdir(parents=True, exist_ok=True)
        file_handler = RotatingFileHandler(
            path, maxBytes=1_000_000, backupCount=2, encoding="utf-8"
        )
        file_handler.setFormatter(formatter)
        handlers.append(file_handler)
    except OSError as error:
        if sys.stderr is not None:
            print(f"Warning: could not open log file {log_file}: {error}")

    if not handlers:
        handlers.append(logging.NullHandler())

    logging.basicConfig(level=level, handlers=handlers, force=True)


def main(argv: Optional[list[str]] = None) -> None:
    args = build_parser().parse_args(argv)
    configure_logging(args.verbose, args.log_file)
    logger = logging.getLogger("cc_usage_display")

    config = load_config(
        config_path=args.config,
        db_path=args.db,
        host=args.host,
        port=args.port,
        token=args.token,
    )
    if not Path(config.db_path).exists():
        logger.error("CC Switch database not found: %s", config.db_path)
        raise SystemExit(f"CC Switch database not found: {config.db_path}")

    if args.once:
        conn = connect_read_only(config.db_path)
        try:
            print(
                json.dumps(
                    build_snapshot(conn, usd_to_cny=config.usd_to_cny).payload(),
                    indent=2,
                )
            )
        finally:
            conn.close()
        return

    try:
        serve(config)
    except Exception:
        logger.exception("Companion stopped unexpectedly")
        raise
