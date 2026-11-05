"""Fixture self-test for join_paragraphs.py (R.1.9 checklist).

Run:  python test_join_fixtures.py   (from book/tools)
Every assertion prints PASS/FAIL; exit code 1 on any failure.
"""

import io
import os
import sys

if sys.stdout.encoding and sys.stdout.encoding.lower().replace("-", "") != "utf8":
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from join_paragraphs import join_lines  # noqa: E402

FAILS = []


def check(name, src, expect):
    out, joined = join_lines(src.split("\n"))
    got = "\n".join(out)
    if got == expect:
        print("PASS  %s (joined %d)" % (name, joined))
    else:
        FAILS.append(name)
        print("FAIL  %s\n  expect: %r\n  got:    %r" % (name, expect, got))


# 1. plain CJK paragraph joins with NO space
check("cjk-cjk no space",
      "先看已经\n会用的一段代码\n继续。",
      "先看已经会用的一段代码继续。")

# 2. ascii-ascii gets ONE space (never glue completion+token)
check("ascii-ascii one space",
      "associated executor 与\ncompletion token 两层参数",
      "associated executor 与 completion token 两层参数")

# 3. cjk-ascii gets ONE space (preserve convention)
check("cjk-ascii one space",
      "先穿过\n`detail/reactive_socket_recv_op` 的对象",
      "先穿过 `detail/reactive_socket_recv_op` 的对象")

# 4. ascii-cjk gets ONE space
check("ascii-cjk one space",
      "the run() loop\n会继续",
      "the run() loop 会继续")

# 5. punctuation boundary: no space
check("punct no space",
      "返回（nothing）\n——就这样。",
      "返回（nothing）——就这样。")

# 6. fenced code block untouched
check("fence untouched",
      "```cpp\nint a = b\n  + c;\n```\n后面一段\n继续。",
      "```cpp\nint a = b\n  + c;\n```\n后面一段继续。")

# 7. tilde fence untouched
check("tilde fence untouched",
      "~~~\nraw\nlines\n~~~\n后面一段\n继续。",
      "~~~\nraw\nlines\n~~~\n后面一段继续。")

# 8. list item start breaks paragraph; continuation joins item
check("list item + continuation",
      "段落结束。\n- 第一项\n  续行内容\n- 第二项\n  另一续行",
      "段落结束。\n- 第一项续行内容\n- 第二项另一续行")

# 9. nested list indent is structure, not continuation
check("nested list preserved",
      "- 父项\n  - 子项甲\n  - 子项乙",
      "- 父项\n  - 子项甲\n  - 子项乙")

# 10. ordered list
check("ordered list",
      "步骤：\n1. 第一步\n   补充说明\n2. 第二步",
      "步骤：\n1. 第一步补充说明\n2. 第二步")

# 11. table rows untouched
check("table untouched",
      "| a | b |\n| --- | --- |\n| 1 | 2 |\n\n段内\n折行。",
      "| a | b |\n| --- | --- |\n| 1 | 2 |\n\n段内折行。")

# 12. blockquote untouched
check("blockquote untouched",
      "> 引用一行\n> 引用两行\n\n段内\n折行。",
      "> 引用一行\n> 引用两行\n\n段内折行。")

# 13. heading untouched
check("heading untouched",
      "## 标题\n正文第一行\n正文第二行",
      "## 标题\n正文第一行正文第二行")

# 14. thematic break untouched
check("hrule untouched",
      "段落\n继续。\n\n---\n\n下一段\n开始。",
      "段落继续。\n\n---\n\n下一段开始。")

# 15. hard break (two trailing spaces) preserved
check("hard break kept",
      "第一行  \n第二行",
      "第一行  \n第二行")

# 16. link reference definition untouched
check("link ref untouched",
      "[ref]: https://example.com\n\n正文\n继续。",
      "[ref]: https://example.com\n\n正文继续。")

# 17. html comment untouched
check("html comment untouched",
      "<!-- note -->\n\n正文\n继续。",
      "<!-- note -->\n\n正文继续。")

# 18. bold span across lines joins cleanly
check("bold across lines",
      "这是**黑盒\n拆开**的操作",
      "这是**黑盒拆开**的操作")

# 19. inline code across lines: a newline INSIDE inline code rendered as a
#     space pre-join, so the space is kept (render-preserving)
check("inline code across lines",
      "在 `run_one_impl\n` 里",
      "在 `run_one_impl ` 里")

# 20. mixed paragraph with trailing table start does not over-join
check("blank-line separated",
      "第一段\n第二行。\n\n第二段\n开始。",
      "第一段第二行。\n\n第二段开始。")

# 21. CJK + Chinese punctuation: no space after the fullwidth comma
check("cn punct no space",
      "也算上，\n约 19.7 万行",
      "也算上，约 19.7 万行")

# 22. Chinese punctuation before CJK
check("cn punct before cjk",
      "两个句号，\n一条执行路径",
      "两个句号，一条执行路径")

# 23. CJK + closing quote no space
check("cn quote no space",
      "所谓「黑盒」\n拆开",
      "所谓「黑盒」拆开")

# 24. operator + ascii: keeps the space (never glue +WSAPoll)
check("operator space kept",
      "Windows +\nWSAPoll",
      "Windows + WSAPoll")

# 25. arrow operator keeps space
check("arrow space kept",
      "取出 →\n执行 → 再取",
      "取出 → 执行 → 再取")

# 26. CJK + backtick: space (matches CJK-Latin convention)
check("cjk backtick space",
      "在\n`run_one_impl` 里",
      "在 `run_one_impl` 里")

# 27. backtick + CJK: space
check("backtick cjk space",
      "在 `run_one_impl`\n里",
      "在 `run_one_impl` 里")

# 28. CJK + opening bold: no space
check("cjk opening bold no space",
      "它给你的是\n**就绪通知**。",
      "它给你的是**就绪通知**。")

# 29. closing bold after CJK punct + CJK: no space
check("closing bold no space",
      "通知只有三个来源。**\nhandler 队列",
      "通知只有三个来源。**handler 队列")

# 30. latin bold content keeps the space
check("latin bold keeps space",
      "这个小实现叫\n**asio-simplified**（下文）",
      "这个小实现叫 **asio-simplified**（下文）")

# 31. pre-existing bold space artifacts normalized mid-line
check("bold space normalized",
      "断言在 **之后** 触发：两个对象",
      "断言在**之后**触发：两个对象")

# 32. latin bold mid-line space kept
check("latin bold mid-line kept",
      "实现叫 **asio-simplified**（下文）",
      "实现叫 **asio-simplified**（下文）")

# 33. bold CJK content glued to latin follower: space added
check("bold cjk + latin follower",
      "用它们**模拟**proactor——登记",
      "用它们**模拟** proactor——登记")

# 34. bold latin content glued to latin follower: space added
check("bold latin + latin follower",
      "写到**至少**n 字节就算完成",
      "写到**至少** n 字节就算完成")

# 35. bold latin after CJK without space: space added
check("cjk + bold latin no space",
      "交给它的**handler** 串行化",
      "交给它的 **handler** 串行化")

# 36. punct-ended bold: space after removed
check("punct bold space removed",
      "1. **poll 集合是快照。** WSAPoll 看到的",
      "1. **poll 集合是快照。**WSAPoll 看到的")

# 37. duplicated run() tail deduped by direct replacement (not join)
check("bold cjk both sides kept",
      "第 2 章契约一的总**账**——三条",
      "第 2 章契约一的总**账**——三条")

# 38. operator + backtick: space kept (op rule wins over backtick rule)
check("operator + backtick space",
      "成员槽位 +\n`std::function` 代替",
      "成员槽位 + `std::function` 代替")

# 39. CJK + backtick after an operator line still spaced
check("backtick + cjk after op",
      "428 行）+\n`detail/impl/socket_ops.ipp` 的包装",
      "428 行）+ `detail/impl/socket_ops.ipp` 的包装")

# 40. Korean is EXCLUDED from joining (QA-ko R.1.1): Hangul-Hangul soft
#     breaks render as word spaces in Korean, which are CORRECT — joining
#     would delete them (see the Korean sample below). Documented
#     exclusion; ko files must never be passed to this tool.  # cjk-ok
check("korean excluded from joining (current behavior)",
      "호출\n순간에서 베껴낸",
      "호출순간에서 베껴낸")

print()
if FAILS:
    print("FAILED: %s" % ", ".join(FAILS))
    sys.exit(1)
print("all fixtures passed")
