#!/usr/bin/env python3
"""Rewrite translated-from markers in en/ja/ko with the current zh hash.

Use after editing book/src/*.md and propagating the change to translations.
"""

import glob
import hashlib
import os
import re
import sys

if hasattr(sys.stdout, "buffer"):
    sys.stdout = __import__("io").TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BOOK = os.path.join(ROOT, "book")
MARKER = re.compile(r"<!-- translated-from: book/src/(\S+) sha256:[0-9a-fP]+ -->")


def main():
    fixed = 0
    for lang in ("src-en", "src-ja", "src-ko"):
        for path in sorted(glob.glob(os.path.join(BOOK, lang, "*.md"))):
            name = os.path.basename(path)
            zh_path = os.path.join(BOOK, "src", name)
            if not os.path.isfile(zh_path):
                print("no zh source for %s/%s" % (lang, name))
                continue
            h = hashlib.sha256(open(zh_path, "rb").read()).hexdigest()[:12]
            marker = "<!-- translated-from: book/src/%s sha256:%s -->" % (name, h)
            s = open(path, encoding="utf-8").read()
            s2 = MARKER.sub(marker, s, count=1)
            if s2 != s:
                open(path, "w", encoding="utf-8", newline="").write(s2)
                fixed += 1
                print("updated %s/%s -> %s" % (lang, name, h))
    print("markers updated: %d" % fixed)


if __name__ == "__main__":
    main()
