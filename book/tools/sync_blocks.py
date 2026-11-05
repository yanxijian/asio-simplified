#!/usr/bin/env python3
"""Copy code blocks from the English tree into zh/ja/ko, keeping the
convention: code blocks are byte-identical across all four languages and
the English tree is their master. Run after editing code in the en prose.

Counts must match 1:1 or the script refuses to touch anything.
"""

import glob
import os
import re
import sys

if hasattr(sys.stdout, "buffer"):
    sys.stdout = __import__("io").TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BOOK = os.path.join(ROOT, "book")
BLOCK = re.compile(r"```[a-zA-Z0-9]*\n(.*?)```", re.S)


def sync(name):
    en = os.path.join(BOOK, "src-en", name)
    changed = 0
    for lang in ("src", "src-ja", "src-ko"):
        path = os.path.join(BOOK, lang, name)
        if not os.path.isfile(path):
            continue
        en_text = open(en, encoding="utf-8").read()
        lang_text = open(path, encoding="utf-8").read()
        en_blocks = BLOCK.findall(en_text)
        lang_blocks = BLOCK.findall(lang_text)
        if len(en_blocks) != len(lang_blocks):
            print("SKIP %s/%s: block count %d != en %d"
                  % (lang, name, len(lang_blocks), len(en_blocks)))
            continue
        it = iter(en_blocks)
        out = BLOCK.sub(lambda m: "```" + next(it) + "```", lang_text)
        if out != lang_text:
            open(path, "w", encoding="utf-8", newline="").write(out)
            changed += 1
            print("synced %s/%s" % (lang, name))
    return changed


def main():
    total = 0
    for p in sorted(glob.glob(os.path.join(BOOK, "src-en", "*.md"))):
        total += sync(os.path.basename(p))
    print("synced %d file(s)" % total)


if __name__ == "__main__":
    main()
