#!/usr/bin/env python3
"""Generate a deterministic prose corpus for Kern's performance tests.

Usage:  python3 tools/gen_prose.py <size> <outfile>
        size accepts a byte count or a K/M/MB/G suffix, e.g. 100K, 1M, 100MB.

Output is realistic Markdown-ish text (paragraphs that wrap, plus the
occasional heading, bullet list, and inline **bold** / _italic_ / `code` /
==highlight== / [[wikilink]]) so the wrap and markdown paths are exercised.
The seed is fixed, so the same size always yields byte-identical output.
"""
import sys
import random

WORDS = (
    "the quick brown fox jumps over a lazy dog lorem ipsum dolor sit amet "
    "consectetur adipiscing elit sed do eiusmod tempor incididunt ut labore "
    "et dolore magna aliqua enim ad minim veniam quis nostrud exercitation "
    "ullamco laboris nisi aliquip ex ea commodo consequat duis aute irure "
    "reprehenderit voluptate velit esse cillum eu fugiat nulla pariatur"
).split()


def parse_size(s):
    s = s.strip().upper().rstrip("B")
    mult = 1
    if s.endswith("K"): mult, s = 1024, s[:-1]
    elif s.endswith("M"): mult, s = 1024 * 1024, s[:-1]
    elif s.endswith("G"): mult, s = 1024 * 1024 * 1024, s[:-1]
    return int(float(s) * mult)


def words(rnd, n):
    return " ".join(rnd.choice(WORDS) for _ in range(n))


def make_line(rnd, n):
    r = rnd.random()
    if r < 0.04:
        return "# " + words(rnd, rnd.randint(2, 5)).title()
    if r < 0.10:
        return "## " + words(rnd, rnd.randint(2, 5)).title()
    if r < 0.25:
        indent = "  " * rnd.randint(0, 2)
        return f"{indent}- " + words(rnd, rnd.randint(3, 12))
    if r < 0.30:
        return ""  # blank line between paragraphs
    # a paragraph line, occasionally with inline markup
    w = words(rnd, rnd.randint(8, 22))
    r2 = rnd.random()
    if r2 < 0.15:
        w = w.replace(" ", " **", 1).replace(" ", "** ", 2)
    elif r2 < 0.25:
        w += " ==" + words(rnd, 2) + "=="
    elif r2 < 0.35:
        w += " [[" + words(rnd, 2).title().replace(" ", "") + "]]"
    return w


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    target = parse_size(sys.argv[1])
    out = sys.argv[2]
    rnd = random.Random(1234)
    written = 0
    n = 0
    with open(out, "w", encoding="utf-8") as f:
        while written < target:
            n += 1
            line = make_line(rnd, n)
            f.write(line)
            f.write("\n")
            written += len(line.encode("utf-8")) + 1
    print(f"wrote {out}: {written} bytes, {n} lines")


if __name__ == "__main__":
    main()
