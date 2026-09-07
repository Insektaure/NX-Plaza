#!/usr/bin/env python3
"""What the app says in English, and what a language has for it.

The English text *is* the key (see source/core/i18n.h), so a translation is
matched by the exact bytes of the literal in the code. That makes two mistakes
possible and invisible: an entry whose English no longer exists anywhere (a
line was reworded, the translation now sits there doing nothing), and a string
the app says that no language has been given (it comes out in English).

This finds both.

    tools/i18n_scan.py                 what French is missing, and what is stale
    tools/i18n_scan.py --lang fr       the same, said out loud
    tools/i18n_scan.py --check         exit 1 if anything is stale
    tools/i18n_scan.py --stub          print the missing entries as C++ rows

Strings reach tr() two ways, and both are collected here:

  * explicitly, `tr("Race for free")` at the point it is drawn;
  * through a sink - App::hint, ui::actionButton, ui::eyebrow, the segmented
    pills, the stat captions, and the three row builders in the settings scene
    - which translate whatever label they are handed, so the literal at the
    call site never mentions tr() at all.

The sinks are listed below by name and by which argument is the label. A sink
that is not listed is not scanned, which shows up as a "missing" string rather
than as silence.
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "source")
CATALOGS = {"fr": os.path.join(SOURCE, "core", "lang_fr.cpp")}

# name -> which argument holds the label, zero based. A tuple takes several.
SINKS = {
    "tr": (0,),
    "hint": (1,),
    "eyebrow": (2,),
    "actionButton": (2,),
    "actionButtonWidth": (1,),
    "segmentWidth": (1,),
    "statBlock": (3,),
    "statCard": (3,),
    # The four gambling scenes share the shape of their button row:
    # (app, r, row, plainLabel, canStake).
    "drawButtons": (3,),
    # The settings scene's own row builders: (id, label, hint, ...).
    "toggle": (1, 2),
    "choice": (1, 2),
    "value": (1, 2, 3),
    "segmented": (1, 2),
}

# A table of rows whose labels are translated where they are drawn rather than
# where the table is filled (see the note in i18n.h about statics). `fields` is
# which column of a row is a label; None means every literal in it is.
TABLES = (
    {"file": "app.cpp", "name": "kTabs", "fields": (1,)},
    {"file": "games.cpp", "name": "kShelf", "fields": (1, 2, 5)},
)

# A string literal, with escapes, and the C++ habit of writing a long one as
# several adjacent literals across lines.
LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')


def unescape(text):
    return (
        text.replace("\\n", "\n")
        .replace('\\"', '"')
        .replace("\\t", "\t")
        .replace("\\\\", "\\")
    )


def escape(text):
    return (
        text.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\t", "\\t")
    )


def strip_comments(text):
    out = []
    i = 0
    n = len(text)
    while i < n:
        two = text[i : i + 2]
        if two == "//":
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif two == "/*":
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif text[i] == '"':
            m = LITERAL.match(text, i)
            if not m:
                out.append(text[i])
                i += 1
                continue
            out.append(m.group(0))
            i = m.end()
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def split_args(text):
    """The top level arguments of a call, given what is inside the brackets."""
    args = []
    depth = 0
    current = []
    i = 0
    while i < len(text):
        c = text[i]
        if c == '"':
            m = LITERAL.match(text, i)
            if m:
                current.append(m.group(0))
                i = m.end()
                continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        if c == "," and depth == 0:
            args.append("".join(current))
            current = []
        else:
            current.append(c)
        i += 1
    if current:
        args.append("".join(current))
    return args


def call_body(text, open_paren):
    """From the '(' to its match, or None if it never closes."""
    depth = 0
    i = open_paren
    while i < len(text):
        c = text[i]
        if c == '"':
            m = LITERAL.match(text, i)
            if m:
                i = m.end()
                continue
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return text[open_paren + 1 : i]
        i += 1
    return None


def whole_literal(arg):
    """An argument that is nothing but adjacent string literals, joined."""
    rest = arg.strip()
    parts = []
    while rest:
        m = LITERAL.match(rest)
        if not m:
            return None
        parts.append(m.group(1))
        rest = rest[m.end() :].strip()
    return unescape("".join(parts)) if parts else None


def brace_body(text, open_brace):
    """From the '{' to its match."""
    depth = 0
    i = open_brace
    while i < len(text):
        c = text[i]
        if c == '"':
            m = LITERAL.match(text, i)
            if m:
                i = m.end()
                continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace + 1 : i]
        i += 1
    return None


def scan_table(text, name, fields, path, found):
    m = re.search(r"\b" + name + r"\s*\[[^\]]*\]\s*=\s*\{", text)
    if not m:
        print("i18n_scan: no table %s in %s" % (name, os.path.basename(path)),
              file=sys.stderr)
        return
    body = brace_body(text, m.end() - 1)
    if body is None:
        return
    for row in re.finditer(r"\{", body):
        entry = brace_body(body, row.start())
        if entry is None:
            continue
        columns = split_args(entry)
        wanted = range(len(columns)) if fields is None else fields
        for pos in wanted:
            if pos >= len(columns):
                continue
            literal = whole_literal(columns[pos])
            if literal:
                found.setdefault(literal, set()).add(os.path.basename(path))


def scan_file(path, found):
    with open(path, encoding="utf-8") as handle:
        text = strip_comments(handle.read())

    for table in TABLES:
        if os.path.basename(path) == table["file"]:
            scan_table(text, table["name"], table["fields"], path, found)

    for name, positions in SINKS.items():
        for m in re.finditer(r"\b" + name + r"\s*\(", text):
            body = call_body(text, m.end() - 1)
            if body is None:
                continue
            args = split_args(body)
            for pos in positions:
                if pos >= len(args):
                    continue
                literal = whole_literal(args[pos])
                if literal:
                    found.setdefault(literal, set()).add(os.path.basename(path))


def scan_sources():
    found = {}
    for folder, _, names in os.walk(SOURCE):
        for name in sorted(names):
            if name.endswith((".cpp", ".h")) and not name.startswith("lang_"):
                scan_file(os.path.join(folder, name), found)
    return found


ENTRY = re.compile(r"\{\s*((?:\"(?:[^\"\\]|\\.)*\"\s*)+),\s*((?:\"(?:[^\"\\]|\\.)*\"\s*)+)\}")


def read_catalog(path):
    """The table's rows, in file order, as (source, target)."""
    with open(path, encoding="utf-8") as handle:
        text = strip_comments(handle.read())
    rows = []
    for m in ENTRY.finditer(text):
        source = whole_literal(m.group(1))
        target = whole_literal(m.group(2))
        if source is not None and target is not None:
            rows.append((source, target))
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--lang", default="fr", choices=sorted(CATALOGS))
    parser.add_argument("--check", action="store_true",
                        help="exit 1 on a stale or duplicated entry")
    parser.add_argument("--stub", action="store_true",
                        help="print missing strings as C++ rows to paste in")
    args = parser.parse_args()

    said = scan_sources()
    rows = read_catalog(CATALOGS[args.lang])

    seen = {}
    duplicates = []
    for source, target in rows:
        if source in seen:
            duplicates.append(source)
        else:
            seen[source] = target

    stale = [s for s in seen if s not in said]
    missing = [s for s in sorted(said) if s not in seen]
    untranslated = [s for s, t in seen.items() if t == s or not t]

    if args.stub:
        for source in missing:
            print('    { "%s",\n        "%s" },' % (escape(source), escape(source)))
        return 0

    print("%s: %d of %d strings, %d the same in both, %d not looked at"
          % (args.lang, len(seen) - len(untranslated), len(said), len(untranslated),
             len(missing)))
    for label, items in (
        ("stale - the English is gone", sorted(stale)),
        ("listed twice", sorted(set(duplicates))),
        ("the same in both, on purpose", sorted(untranslated)),
        ("not looked at", missing),
    ):
        if not items:
            continue
        print("\n%s (%d):" % (label, len(items)))
        for item in items:
            print("  %s" % escape(item))

    if args.check and (stale or duplicates):
        print("\nstale or duplicated entries: fix lang_%s.cpp" % args.lang)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
