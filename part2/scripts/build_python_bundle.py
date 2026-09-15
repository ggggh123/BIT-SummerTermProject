"""为 spark-submit --py-files 构建本项目 Python 源码包，不打包数据或环境。"""
import argparse
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED


def build_bundle(output):
    root = Path(__file__).resolve().parents[2]
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(output, "x", compression=ZIP_DEFLATED) as archive:
        sources = list((root / "part2").rglob("*.py")) + list((root / "part2/contracts").glob("*.json"))
        for path in sorted(sources):
            if any(piece in {"tests", "data", "artifacts", "__pycache__"} for piece in path.relative_to(root).parts):
                continue
            archive.write(path, str(path.relative_to(root)))
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    print(build_bundle(parser.parse_args().output))
