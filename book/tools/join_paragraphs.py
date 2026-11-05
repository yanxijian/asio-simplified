#!/usr/bin/env python3
"""Join wrapped continuation lines inside paragraphs, for CJK books.

mdBook renders a soft line break as a space, so a wrapped CJK paragraph
shows stray spaces mid-sentence ("黑 盒"). This tool merges continuation
lines into single-line paragraphs, inserting boundary-aware separators:

  CJK <-> CJK            no space (CJK needs none)
  ascii alnum <-> ascii  one space (never glue "completion"+"token")
  CJK <-> ascii          one space (matches the book's CJK-Latin convention)
  punctuation involved   no space

Structural lines are never touched:
  fenced code blocks (``` and ~~~), headings, table rows (|), blockquote
  lines (>), HTML blocks/comments (leading <), link reference definitions
  ([text]: url), thematic breaks (---, ***, ===), and list-item STARTS at
  any indent (nested list markers are structure, not continuation).

List continuation lines (indented, not starting a new item) are joined
into their item. Hard line breaks (trailing 2+ spaces or a backslash) are
preserved. Blockquote paragraphs are left as-is on purpose (none exist in
the zh source; flagged for the ja/ko batches).

Usage:  python join_paragraphs.py [--dry-run] [paths...]
        default paths: book/src/*.md relative to the repo root
"""

import glob
import os
import re
import sys

if sys.stdout.encoding and sys.stdout.encoding.lower().replace("-", "") != "utf8":
    sys.stdout = __import__("io").TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

CJK = re.compile(r"[\u4e00-\u9fff\u3040-\u30ff\uac00-\ud7af]")
ALNUM = re.compile(r"[A-Za-z0-9]")
# full-width CJK punctuation: never gets a space on either side
CN_PUNCT = re.compile(r"[\u3000-\u303f\uff0c\uff0e\uff1a\uff1b\uff01\uff1f\u201c\u201d\u2018\u2019\uff08\uff09\u300a\u300b\u3008\u3009\u2014\u2026\u00b7]")
# ASCII operators / arrows: always keep one space on both sides
OP = re.compile(r"[+\-*/=<>&|~^%\u2192]")
# markdown markers (backtick, asterisk): behave like word characters
MARK = re.compile(r"[`*]")
FENCE = re.compile(r"^\s*(```|~~~)")
LIST_ITEM = re.compile(r"^\s*([-*+]|\d+[.)])\s")
HRULE = re.compile(r"^\s*(-{3,}|\*{3,}|_{3,})\s*$")
SETEXT = re.compile(r"^\s*(={3,}|-{3,})\s*$")
LINK_REF = re.compile(r"^\s*\[[^\]]+\]:\s")
HTMLISH = re.compile(r"^\s*<")
HARD_BREAK = re.compile(r"(\s{2,}|\\)$")


def _cls(ch):
    if not ch:
        return "empty"
    if CJK.match(ch):
        return "cjk"
    if CN_PUNCT.match(ch):
        return "cnpunct"
    if ch == "`":
        return "backtick"
    if ch == "*":
        return "star"
    if OP.match(ch):
        return "op"
    if ALNUM.match(ch):
        return "alnum"
    return "aspunct"


def boundary_join(left, right):
    """Separator to use when joining `left` and `right` text fragments.

    Boundary-aware, including markdown emphasis markers:
      - CJK+CJK                 -> no space
      - CJK punctuation either  -> no space (，。 etc never carry spaces)
      - backtick or ASCII operator involved -> one space ("Windows +
        WSAPoll" must not become "+WSAPoll")
      - `*` runs are emphasis markers: decide by the adjacent CONTENT char
        (before a closing run / after an opening run) - CJK content takes
        no space ("是**就绪通知**"), latin content takes one
        ("叫 **asio-simplified**")
      - otherwise ascii-alnum pairs and CJK/ascii pairs -> one space
    """
    a = left[-1:] if left else ""
    b = right[:1] if right else ""
    if not a or not b:
        return ""
    ca, cb = _cls(a), _cls(b)

    if ca == "star":  # closing emphasis: decide by content char before it
        k = len(left)
        while k > 0 and left[k - 1] == "*":
            k -= 1
        ca = _cls(left[k - 1]) if k > 0 else "aspunct"
    if cb == "star":  # opening emphasis: decide by content char after it
        k = 0
        while k < len(right) and right[k] == "*":
            k += 1
        cb = _cls(right[k]) if k < len(right) else "aspunct"

    if ca == "cjk" and cb == "cjk":
        return ""
    if ca == "cnpunct" or cb == "cnpunct":
        return ""
    # operators must come BEFORE the backtick rule: "slot +`code`" needs the
    # space even though neither side is cjk/alnum
    if ca == "op" or cb == "op":
        return " "
    if ca == "backtick" or cb == "backtick":
        return " " if (ca in ("cjk", "alnum") or cb in ("cjk", "alnum")) else ""
    if ca == "alnum" and cb == "alnum":
        return " "
    if (ca == "cjk") != (cb == "cjk"):
        return " "
    return ""  # remaining ascii punctuation pairs


def indent_of(line):
    return len(line) - len(line.lstrip())


# stray spaces around emphasis spans whose content is CJK (e.g. "zai **hou**
# chu" style wrapping) render as visible gaps; word-chars/backticks excluded
# from the span edges so we never match inside `code` or across list markers
BOLD_SPAN = re.compile(r"(?<![A-Za-z0-9_`*])(\*{1,2})([^*\n]+?)(\*{1,2})(?!`)")


def _desired(outer, content_edge):
    """Desired separator on one bold edge.

    outer: the char just outside the span ("" at line edge)
    content_edge: the span content's edge char
    Fullwidth punctuation dominates; CJK-CJK takes none; anything with a
    latin-alnum on either side takes one.
    """
    if not outer:
        return ""
    if CN_PUNCT.match(outer):
        return ""
    if CN_PUNCT.match(content_edge):
        return ""
    if CJK.match(outer) and CJK.match(content_edge):
        return ""
    return " "


def normalize_bold_spaces(line):
    """Normalize spaces around **emphasis** spans in a CJK paragraph."""
    def is_cjk_ish(ch):
        return bool(CJK.match(ch) or CN_PUNCT.match(ch))

    spans = list(BOLD_SPAN.finditer(line))
    if not spans:
        return line
    # process from the end so earlier offsets stay valid
    for m in reversed(spans):
        start, end = m.span()
        stars, content = m.group(1), m.group(2)

        # leading edge: outer char is line[start-1]; a single space may sit
        # between it and the span
        k = start
        had_space = k > 0 and line[k - 1] == " "
        if had_space:
            k -= 1
        outer = line[k - 1] if k > 0 else ""
        lead_desired = _desired(outer, content[:1])
        if had_space and lead_desired == "":
            # drop the stray space
            line = line[:k] + line[start:]
        elif (not had_space) and lead_desired == " " and k > 0:
            line = line[:k] + " " + line[start:]

        # trailing edge: recompute span offsets after the leading edit
        m2 = BOLD_SPAN.search(line, max(0, start - 2))
        if not m2 or m2.group(2) != content:
            continue
        start2, end2 = m2.span()
        j = end2
        had_space2 = j < len(line) and line[j] == " "
        if had_space2:
            j += 1
        outer2 = line[j] if j < len(line) else ""
        tail_desired = _desired(outer2, content[-1:])
        if had_space2 and tail_desired == "":
            line = line[:end2] + line[j:]
        elif (not had_space2) and tail_desired == " " and j < len(line):
            line = line[:end2] + " " + line[j:]

    return line


def join_lines(lines):
    """Return (new_lines, joined_count) for a list of source lines."""
    out = []
    joined = 0
    in_code = False
    fence_marker = None
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]

        # blank line: paragraph terminator, never a block start
        if not line.strip():
            out.append(line)
            i += 1
            continue

        # fenced code block: verbatim
        if FENCE.match(line):
            marker = FENCE.match(line).group(1)
            if not in_code:
                in_code, fence_marker = True, marker[:3]
            elif marker.startswith(fence_marker):
                in_code, fence_marker = False, None
            out.append(line)
            i += 1
            continue
        if in_code:
            out.append(line)
            i += 1
            continue

        # structural lines pass through untouched
        if (line.startswith("#") or line.startswith("|") or line.startswith(">")
                or HRULE.match(line) or LINK_REF.match(line)
                or HTMLISH.match(line)):
            out.append(line)
            i += 1
            continue

        # start or continue a paragraph / list-item block
        block = [line]
        is_list = bool(LIST_ITEM.match(line))
        base_indent = indent_of(line)
        i += 1
        while i < n:
            nxt = lines[i]
            if not nxt.strip():
                break
            if FENCE.match(nxt):
                break
            if nxt.startswith("#") or nxt.startswith("|"):
                break
            if nxt.startswith(">") or HTMLISH.match(nxt) or LINK_REF.match(nxt):
                break
            if HRULE.match(nxt) or SETEXT.match(nxt):
                break
            # a list-item start at any indent is structure, not continuation
            if LIST_ITEM.match(nxt):
                break
            # blockquote lines never join
            if nxt.lstrip().startswith(">"):
                break
            # hard line break: keep it
            if HARD_BREAK.search(lines[i - 1]) if i > 0 else False:
                break
            # list continuation: only lines at least as indented as the item
            if is_list and indent_of(nxt) < base_indent:
                break
            block.append(nxt)
            i += 1

        if len(block) == 1:
            out.append(line)
            continue

        merged = block[0].rstrip()
        for cont in block[1:]:
            stripped = cont.strip()
            sep = boundary_join(merged, stripped)
            merged += sep + stripped
            joined += 1
        out.append(normalize_bold_spaces(merged))

    # normalization pass over untouched single lines too (code excluded)
    for k, ln in enumerate(out):
        if not in_code_marker(out, k):
            out[k] = normalize_bold_spaces(ln)

    return out, joined


def in_code_marker(lines, idx):
    """True if lines[idx] sits inside a fenced code block."""
    in_code = False
    marker = None
    for i, ln in enumerate(lines):
        m = FENCE.match(ln)
        if m:
            if not in_code:
                in_code, marker = True, m.group(1)[:3]
            elif m.group(1).startswith(marker):
                in_code, marker = False, None
        if i == idx:
            return in_code
    return False


def process(path, dry_run=False):
    text = open(path, encoding="utf-8").read()
    new, joined = join_lines(text.split("\n"))
    result = "\n".join(new)
    if result != text and not dry_run:
        open(path, "w", encoding="utf-8", newline="").write(result)
    return joined


def main():
    dry = "--dry-run" in sys.argv
    paths = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not paths:
        paths = sorted(glob.glob(os.path.join(ROOT, "book", "src", "*.md")))
    total = 0
    for p in paths:
        n = process(p, dry)
        total += n
        print("%-24s joined %d line(s)%s" % (os.path.basename(p), n,
                                             " (dry-run)" if dry else ""))
    print("total joined: %d%s" % (total, " (dry-run)" if dry else ""))


if __name__ == "__main__":
    main()
