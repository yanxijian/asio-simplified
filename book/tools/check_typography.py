#!/usr/bin/env python3
"""Typography assertions for the zh book (QA-zh batch).

Scope notes:
  - fenced code blocks and inline code spans are excluded (code must stay
    byte-identical across languages; its spacing is not CJK typography);
  - table rows are excluded (cell layout is structural);
  - bold markers (**), inline-code backticks and full-width parentheses
    carry no space semantics, so they are stripped before scanning;
  - operator gluing is NOT asserted globally: prose legitimately contains
    99%, 1.38.2, I/O, C++20, ch3-11. The join tool's operator rule
    ("Windows + WSAPoll" keeps its spaces) is covered by fixtures 24/25
    in test_join_fixtures.py instead.

Checks (prose only):
  1. no CJK-space-CJK            (stray spaces inside Chinese)
  2. no space around CJK punct   (，。；：、！？」 never carries a space)
  3. no glued CJK-Latin          (every ASCII-alnum<->CJK boundary has one
                                  space; full-width ）( are exempt)

Exit 0 clean, 1 with problems listed per file.
"""

import glob
import os
import re
import sys

if sys.stdout.encoding and sys.stdout.encoding.lower().replace("-", "") != "utf8":
    sys.stdout = __import__("io").TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "book", "src")

P1 = re.compile(r"[\u4e00-\u9fff][ \t]+[\u4e00-\u9fff]")
P2 = re.compile(r"[，。；：、！」？][ \t]+|[\u4e00-\u9fff][ \t]+[，。；：、！」？]")
P4 = re.compile(r"[\u4e00-\u9fff][A-Za-z0-9]|[A-Za-z0-9][\u4e00-\u9fff]")
# 5. plus glued to a following inline-code backtick (slot +`std` style);
#    bold-close + backtick and wildcards (`async_*`) are legitimate
P5 = re.compile(r"\+`")


def prose_of(path):
    text = open(path, encoding="utf-8").read()
    prose = re.sub(r"```[a-zA-Z0-9]*\n(.*?)```", "", text, flags=re.S)
    prose = re.sub(r"`[^`\n]*`", "X", prose)         # latin placeholder: preserves CJK-Latin boundary semantics
    prose = prose.replace("**", "")                  # bold markers: no semantics
    prose = prose.replace("C++20", "CPPSTD").replace("C++", "CPP")
    lines = [ln for ln in prose.split("\n")
             if not ln.lstrip().startswith("|")]     # table rows: structural
    return lines


def main():
    problems = []
    for path in sorted(glob.glob(os.path.join(SRC, "*.md"))):
        name = os.path.basename(path)
        raw_lines = []
        text = open(path, encoding="utf-8").read()
        in_code = False
        for ln in text.split("\n"):
            if ln.startswith("```"):
                in_code = not in_code
                continue
            if not in_code:
                raw_lines.append(ln)
        for i, line in enumerate(raw_lines, 1):
            for m in P5.finditer(line):
                problems.append("%s L%d op glued to backtick: ...%s..." %
                                (name, i, line[max(0, m.start() - 14):m.end() + 14]))
        for i, line in enumerate(prose_of(path), 1):
            for pat, msg in ((P1, "CJK space CJK"),
                             (P2, "space around CJK punctuation"),
                             (P4, "glued CJK-Latin")):
                for m in pat.finditer(line):
                    problems.append("%s L%d %s: ...%s..." %
                                    (name, i, msg,
                                     line[max(0, m.start() - 14):m.end() + 14]))

    if problems:
        print("%d typography problem(s):" % len(problems))
        for p in problems:
            print("  " + p)
        return 1
    print("typography checks: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
