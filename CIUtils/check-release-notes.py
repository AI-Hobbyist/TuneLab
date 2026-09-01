#!/usr/bin/env python3
"""检查 GitHub Release 正文里有没有 Markdown 代码语法。

为什么要这道闸：应用内的更新弹窗直接渲染 release 正文（服务端 /api/app/get-update
把最新 release 的 body 原样转发给客户端）。1.5.10 / 1.6.0 这两版客户端用的
Markdown.Avalonia 11.0.3-a1 与 Avalonia 11.2.x 不兼容——渲染任何「代码」元素
（行内 `code`、``` 围栏、四空格缩进块）时会抛
NotSupportedException: Unsupported IBinding implementation 'StaticBinding'。
弹窗是在 async void 里构造的，异常无人接管，进程直接退出：老用户一开软件就闪退，
而且崩在弹窗渲染阶段，连「忽略此版本」都点不到，无法自救。

这些客户端已经在用户机器上，改不了；能改的只有我们写的正文。所以只要还有
1.5.10 / 1.6.0 的存量用户，release 正文就不能出现代码语法。

判定用 CommonMark 解析器（markdown-it-py）看 AST，不是正则猜：嵌套列表的四空格
缩进不是代码块，不会误报。

用法：
    python CIUtils/check-release-notes.py <body-file>            # 检查，发现即失败
    gh release view v2.0.2 --json body -q .body | python CIUtils/check-release-notes.py -
    python CIUtils/check-release-notes.py <body-file> --fix      # 剥掉代码语法后打到 stdout

--fix 只给 CI 自动生成的 release notes 用：那份文案由 commit message 拼出来、没人斟酌过，
剥掉反引号不损语义，卡住发版反而没有意义（commit 已经推了，改不了）。人手写的正文一律走
检查模式——怎么改写该由写的人决定。
"""

import sys

try:
    from markdown_it import MarkdownIt
except ImportError:
    print("ERROR: markdown-it-py is required. Install it with: pip install markdown-it-py", file=sys.stderr)
    sys.exit(2)

# token 类型 -> (人类可读的元素名, 建议改法)
FORBIDDEN = {
    "code_inline": ("inline code (`...`)", "drop the backticks and write the text plainly"),
    "fence": ("fenced code block (``` / ~~~)", "rewrite as plain paragraphs or a list"),
    "code_block": ("indented code block (4 spaces / tab)", "unindent it, or turn it into a list item"),
}


def find_violations(body: str):
    """返回 [(行号, 元素名, 建议, 摘录)]，行号 1-based。"""
    tokens = MarkdownIt("commonmark").parse(body)
    lines = body.splitlines()
    found = []

    def record(token_type, line_index):
        name, hint = FORBIDDEN[token_type]
        excerpt = lines[line_index].strip() if 0 <= line_index < len(lines) else ""
        found.append((line_index + 1, name, hint, excerpt))

    for token in tokens:
        if token.type in FORBIDDEN:
            record(token.type, token.map[0] if token.map else 0)
        elif token.type == "inline":
            # 行内 token 自身没有 map，用所属段落的起始行定位
            line_index = token.map[0] if token.map else 0
            for child in token.children or []:
                if child.type in FORBIDDEN:
                    record(child.type, line_index)

    return sorted(found)


def strip_code_syntax(body: str) -> str:
    """剥掉代码语法，保留其中的文字。行的删改从后往前做，避免行号漂移。"""
    tokens = MarkdownIt("commonmark").parse(body)
    blocks = [t for t in tokens if t.type in ("fence", "code_block") and t.map]
    if not blocks:
        # 没有块级代码时不重排行，原样保留换行风格与结尾空行
        return body.replace("`", "")

    lines = body.splitlines(keepends=True)
    for token in sorted(blocks, key=lambda t: t.map[0], reverse=True):
        start, end = token.map
        if token.type == "fence":
            # 去掉围栏行（首行、以及末行若确实是围栏）
            kept = [ln for ln in lines[start:end] if not ln.lstrip().startswith(("```", "~~~"))]
        else:
            # 缩进块：去掉四空格 / 制表符的前导缩进
            kept = [ln[4:] if ln.startswith("    ") else ln.lstrip("\t") for ln in lines[start:end]]
        lines[start:end] = kept

    # 行内代码：反引号在正文里没有别的用途，剥完块级之后直接去掉
    return "".join(lines).replace("`", "")


def main():
    # release 正文是中文的，别让宿主的本地编码（Windows 上是 GBK）把输出撞碎
    # newline="" 保证 --fix 原样吐回换行（Windows 上默认会把 LF 改写成 CRLF）
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8", newline="")

    args = sys.argv[1:]
    fix = "--fix" in args
    args = [a for a in args if a != "--fix"]
    if len(args) != 1:
        print(__doc__, file=sys.stderr)
        return 2

    path = args[0]
    body = sys.stdin.read() if path == "-" else open(path, encoding="utf-8").read()

    if fix:
        cleaned = strip_code_syntax(body)
        sys.stdout.write(cleaned)
        if find_violations(cleaned):
            print("ERROR: code syntax survived --fix, refusing to pass it on.", file=sys.stderr)
            return 1
        if cleaned != body:
            print("NOTE: stripped code syntax from the generated release notes.", file=sys.stderr)
        return 0

    violations = find_violations(body)
    if not violations:
        print("OK: release notes contain no code syntax.")
        return 0

    for line, name, hint, excerpt in violations:
        # ::error:: 让 GitHub Actions 把它显示成注解
        print(f"::error::Line {line}: {name} is not allowed in release notes - {hint}. >>> {excerpt}")

    sys.stdout.flush()  # 让 ::error:: 注解排在下面的总结之前
    print("", file=sys.stderr)
    print(f"FAILED: {len(violations)} code element(s) found in the release notes.", file=sys.stderr)
    print("TuneLab 1.5.10 and 1.6.0 crash on startup while rendering code syntax in the update", file=sys.stderr)
    print("dialog. Those clients cannot be patched - the release notes must avoid code syntax.", file=sys.stderr)
    print("Edit the release body on GitHub; the update API forwards it to clients immediately.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
