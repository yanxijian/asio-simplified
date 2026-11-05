#!/usr/bin/env python3
"""Mechanical checks for the four-language book trees.

Run from anywhere; paths are resolved relative to this script's location
(book/tools/ -> repo root).

Checks:
  1. file sets identical across src / src-en / src-ja / src-ko
  2. translated-from marker hash matches the zh source (first 12 hex)
  3. code blocks byte-identical across languages (trailing blank lines
     inside a block are ignored, matching the review-rounds convention)
  4. no CJK inside any code block (any language)
  5. zh-only vocabulary leaks in the ja/ko prose, plus hybrid leftovers
     (a CJK ideograph immediately followed by an arrow)
  6. ja/ko prose terminology: technical terms must stay in English
     (katakana tech words and hangul tech words are listed below)

Exit code 0 = clean, 1 = problems found. Problems are printed per file.
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
LANGS = ["src", "src-en", "src-ja", "src-ko"]

CODE_BLOCK = re.compile(r"```[a-zA-Z0-9]*\n(.*?)```", re.S)
CJK_IN_CODE = re.compile(r"[\u4e00-\u9fff\u3040-\u30ff\uac00-\ud7af]")
HYBRID = re.compile(r"[\u4e00-\u9fff]\u2192")  # ideograph followed by ->

# zh-only words that must not survive in ja/ko prose
ZH_LEAKS = [
    "投递", "登记", "队列", "唤醒", "描述符", "串行化", "拆开", "排空",
    "精读", "回归测试", "堆序", "账本", "句号",
    "发起", "挂起", "递归", "缺省", "服务注册", "读满",
    "销毁", "于是", "兑现", "单独", "析构",
]

# technical terms that must stay Latin in ja/ko prose (per review rounds)
JA_TERMS = ["ハンドラー", "ストランド", "コルーチン", "エグゼキューター",
            "リアクター", "プロアクター", "ディスクリプタ", "リゾルバー"]
KO_TERMS = ["코루틴", "핸들러", "스트랜드", "리액터", "프로액터",
            "실행자", "디스크립터", "리졸버"]


def norm_block(block):
    """Trailing blank lines inside a block are insignificant (convention)."""
    return block.rstrip("\n")


def files_of(lang):
    d = os.path.join(BOOK, lang)
    return {os.path.basename(p): p for p in sorted(glob.glob(os.path.join(d, "*.md")))}


def text_of(path):
    return open(path, encoding="utf-8").read()


def main():
    problems = []

    trees = {lang: files_of(lang) for lang in LANGS}
    zh = trees["src"]

    # 1. identical file sets
    base = set(zh)
    for lang in LANGS[1:]:
        extra = set(trees[lang]) - base
        missing = base - set(trees[lang])
        if extra:
            problems.append("%s: files not in zh: %s" % (lang, ", ".join(sorted(extra))))
        if missing:
            problems.append("%s: missing files: %s" % (lang, ", ".join(sorted(missing))))

    for lang in LANGS[1:]:
        for name, path in sorted(trees[lang].items()):
            if name not in zh:
                continue
            t = text_of(path)

            # 2. marker hash
            zh_hash = hashlib.sha256(open(zh[name], "rb").read()).hexdigest()[:12]
            marker = "<!-- translated-from: book/src/%s sha256:%s -->" % (name, zh_hash)
            if marker not in t:
                m = re.search(r"<!-- translated-from: [^>]* -->", t)
                if m:
                    problems.append("%s/%s: marker hash mismatch (expected %s)"
                                    % (lang, name, zh_hash))
                else:
                    problems.append("%s/%s: translated-from marker missing" % (lang, name))

            # 3. code blocks vs zh
            zh_blocks = [norm_block(b) for b in CODE_BLOCK.findall(text_of(zh[name]))]
            blocks = [norm_block(b) for b in CODE_BLOCK.findall(t)]
            if len(blocks) != len(zh_blocks):
                problems.append("%s/%s: block count %d != zh %d"
                                % (lang, name, len(blocks), len(zh_blocks)))
            else:
                for i, (a, b) in enumerate(zip(blocks, zh_blocks)):
                    if a != b:
                        problems.append("%s/%s: code block %d differs from zh"
                                        % (lang, name, i))

            # 4. CJK inside code blocks
            for i, b in enumerate(CODE_BLOCK.findall(t)):
                if CJK_IN_CODE.search(b):
                    problems.append("%s/%s: CJK in code block %d" % (lang, name, i))

            # 5. zh leaks + hybrids in ja/ko prose (prose = text minus blocks)
            if lang in ("src-ja", "src-ko"):
                prose = CODE_BLOCK.sub("", t)
                for w in ZH_LEAKS:
                    if w in prose:
                        problems.append("%s/%s: zh leak %r" % (lang, name, w))
                if HYBRID.search(prose):
                    problems.append("%s/%s: hybrid 'ideograph->' leftover" % (lang, name))

            # 6. terminology
            if lang == "src-ja":
                for w in JA_TERMS:
                    if w in CODE_BLOCK.sub("", t):
                        problems.append("%s/%s: katakana term %r (use English)" % (lang, name, w))
            if lang == "src-ko":
                for w in KO_TERMS:
                    if w in CODE_BLOCK.sub("", t):
                        problems.append("%s/%s: hangul term %r (use English)" % (lang, name, w))

    if problems:
        print("%d problem(s):" % len(problems))
        for p in problems:
            print("  " + p)
        return 1
    print("translation checks: clean (%d files x %d languages)"
          % (len(zh), len(LANGS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
