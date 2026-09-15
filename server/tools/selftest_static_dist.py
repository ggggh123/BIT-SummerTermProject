"""自检 Flask 是否能单进程托管 web/dist。"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import app as server_app  # noqa: E402


def main() -> int:
    with tempfile.TemporaryDirectory() as temp_dir:
        dist = Path(temp_dir)
        (dist / "assets").mkdir()
        (dist / "index.html").write_text("<main>EV Part2 Dashboard</main>", encoding="utf-8")
        (dist / "assets" / "app.js").write_text("console.log('ok')\n", encoding="utf-8")

        original = server_app.DIST_DIR
        server_app.DIST_DIR = dist
        try:
            client = server_app.app.test_client()
            index = client.get("/")
            asset = client.get("/assets/app.js")
            spa = client.get("/station/1")
            api_404 = client.get("/api/not-exists")
            checks = [
                index.status_code == 200 and "EV Part2 Dashboard" in index.get_data(as_text=True),
                asset.status_code == 200 and "console.log" in asset.get_data(as_text=True),
                spa.status_code == 200 and "EV Part2 Dashboard" in spa.get_data(as_text=True),
                api_404.get_json()["code"] != 0,
            ]
            for response in (index, asset, spa, api_404):
                response.close()
        finally:
            server_app.DIST_DIR = original

    if all(checks):
        print("[OK] Flask serves web/dist and keeps /api errors in envelope")
        return 0
    print("[FAIL] static dist route check failed")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
