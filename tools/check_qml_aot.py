#!/usr/bin/env python3

import re
import sys
from pathlib import Path

VALUE_TYPES = re.compile(r"^(QQml\w+ValueType|QQuick\w+ValueType|QEventPoint|QQuickHandlerPoint)$")
LOOKUP = re.compile(r'init(?:Get|Set)\w*ValueLookup\(\d+, [^\n]*?fromName\("([^"]+)"\)')


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: check_qml_aot.py <build directory>")
        return 2
    root = Path(sys.argv[1])
    files = [path for path in root.rglob("*_qml.cpp") if "qmlcache" in path.parts]
    if not files:
        print(f"no compiled QML under {root}")
        return 1
    problems = []
    for path in files:
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in LOOKUP.finditer(text):
            if not VALUE_TYPES.match(match.group(1)):
                line = text.count("\n", 0, match.start()) + 1
                problems.append(f"{path}:{line}: compiled QML reads a property of {match.group(1)}, which has no "
                                "meta-object; read the C++ property directly instead of through a local")
    for problem in problems:
        print(problem)
    print(f"compiled QML: {len(files)} files, {len(problems)} lookup(s) on types without a meta-object")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
