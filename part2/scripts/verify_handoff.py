"""校验接收到的 ODS 或测试样本包；校验失败返回非零退出码。"""
import argparse
import json
from pathlib import Path
from part2.common.handoff import verify_handoff

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory")
    parser.add_argument("--output", type=Path, help="保存校验后的读取描述；不修改原交接包")
    parser.add_argument(
        "--require-full",
        action="store_true",
        help="正式运行闸门：拒绝测试夹具，只接受 #4 原生 kind=ods-handoff",
    )
    args = parser.parse_args()
    manifest = verify_handoff(args.directory, require_full=args.require_full)
    if args.output:
        with args.output.open("x", encoding="utf-8") as target:
            json.dump(manifest, target, ensure_ascii=False, indent=2)
    print(json.dumps({"ok": True, "run_id": manifest["run_id"], "kind": manifest["kind"], "file_count": len(manifest["files"])}, ensure_ascii=False))
