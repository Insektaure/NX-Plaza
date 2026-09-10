#!/usr/bin/env python3
"""What the app says in English, and what a language has for it.

The English text *is* the key (see source/core/i18n.h), so a translation is
matched by the exact bytes of the literal in the code. That makes two mistakes
possible and invisible: an entry whose English no longer exists anywhere (a
line was reworded, the translation now sits there doing nothing), and a string
the app says that no language has been given (it comes out in English).

This finds both.

    tools/i18n_scan.py                 what French is missing, and what is stale
    tools/i18n_scan.py --lang de       the same for German, and so on
    tools/i18n_scan.py --all           one line of coverage per language
    tools/i18n_scan.py --check         exit 1 if anything is stale
    tools/i18n_scan.py --stub          print the missing entries as C++ rows
    tools/i18n_scan.py --loose         also guess at labels no sink can see

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
import signal
import sys

# Piping into head should end quietly rather than in a traceback.
try:
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)
except (AttributeError, ValueError):
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "source")
CATALOGS = {
    code: os.path.join(SOURCE, "core", "lang_%s.cpp" % code)
    for code in ("fr", "de", "es", "it", "nl", "pt", "ru", "ja", "ko",
                 "zh_hans", "zh_hant")
}

# name -> which argument holds the label, zero based. A tuple takes several.
SINKS = {
    "tr": (0,),
    "hint": (1,),
    "textInput": (0,),
    "askConfirm": (0, 1, 2),
    "toast": (0, 1),
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

# Files whose literals are data rather than words, for the loose pass. The
# things a pass can carry travel to other consoles and are compared between
# them - two people "carrying the same thing" is a trophy - so translating that
# catalogue would break the comparison rather than help anybody.
LOOSE_SKIP_FILES = ("carry.cpp",)

# A named constant whose literal is a label, used through the name: the pass
# card's own title is drawn twice and measured once, so it is written down once.
CONSTANTS = (
    {"file": "passport.cpp", "name": "kPassTitle"},
    # Where a piece came from when it came from the app rather than from a
    # person. Written into the provenance by the store and read back by the
    # puzzle's own panel, which translates the app's three and leaves a
    # handle alone.
    {"file": "pieces.h", "name": "kShopSource"},
    {"file": "pieces.h", "name": "kWheelSource"},
)

# Small functions that exist to turn a value into a word - a tier into
# "bronze", a reel symbol into "bells". Their own `return "..."` lines are
# labels, and the call sites read `tr(tierName(t))`, where the literal is out
# of sight of the call.
RETURNERS = ("tierName", "filterName", "symbolName", "ordinal", "sortLabel",
             "nextSortHint", "stateLabel", "proximityLabel", "fillHintFor",
             "caption", "subtitleText", "className")

# A table of rows whose labels are translated where they are drawn rather than
# where the table is filled (see the note in i18n.h about statics). `fields` is
# which column of a row is a label; None means every literal in it is.
TABLES = (
    {"file": "app.cpp", "name": "kTabs", "fields": (1,)},
    {"file": "games.cpp", "name": "kShelf", "fields": (1, 2, 5)},
    # A trophy's name and what earns it. Column 0 is its id, which is a key in
    # profile.json and never shown.
    {"file": "trophies.cpp", "name": "kTrophies", "fields": (1, 2)},
    # The short month names relativeTime falls back to for anything older than
    # a week.
    {"file": "util.cpp", "name": "months", "fields": None},
    # The short weekday names. Nothing calls weekdayShort() at the moment, so
    # these are translated and waiting rather than on a screen.
    {"file": "util.cpp", "name": "days", "fields": None},
    # The quest's loot: the six qualities, the four pegs, and the four names
    # a piece of each kind can wear. All three are read through a function
    # (qualityName, slotName, itemNoun) whose caller wraps the result, so the
    # literal is out of sight of every tr() in the tree.
    {"file": "quest_rules.cpp", "name": "kQualities", "fields": None},
    {"file": "quest_rules.cpp", "name": "kSlots", "fields": None},
    {"file": "quest_rules.cpp", "name": "kNouns", "fields": None},
    # The blessings a climb can be offered: a name and a line saying what it
    # does, both read back through boonInfo() and wrapped at the call site.
    {"file": "quest_boons.cpp", "name": "kPool", "fields": (1, 2)},
    # The Mii editor's rows: a label, then which part of the Mii it moves.
    {"file": "mii_editor.cpp", "name": "kParts", "fields": (0,)},
    # The card themes, which sit in the palettes beside their colours. Both
    # palettes name them the same; listing one is enough for the tool, and the
    # names are translated where the pass card draws them.
    {"file": "theme.cpp", "name": "kDark", "fields": None},
    # The settings scene's three segmented rows. The pills are translated by
    # ui::segmented, so the arrays themselves stay in English.
    {"file": "settings.cpp", "name": "placeOptions", "fields": None},
    {"file": "settings.cpp", "name": "reachOptions", "fields": None},
    {"file": "settings.cpp", "name": "themeOptions", "fields": None},
    # The puzzles: a name, then the picture file it needs. The file name is not
    # shown and must not change.
    {"file": "pieces.cpp", "name": "kSets", "fields": (0,)},
)

# A string literal, with escapes, and the C++ habit of writing a long one as
# several adjacent literals across lines.
LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')

# A C++ character literal: 'a', '\n', '\'', '"'.
CHAR_LITERAL = re.compile(r"'(?:[^'\\]|\\.)'")

# A run of adjacent literals, which C++ joins into one string.
LITERAL_RUN = re.compile(r'(?:"(?:[^"\\]|\\.)*"\s*)+')


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
        elif text[i] == "'":
            # A character literal, normalised to 'x'. What it held is never a
            # label, and `c == '"'` or `c == '('` would otherwise look like the
            # start of a string or an unbalanced bracket to everything
            # downstream of here - which is how one such line in mii_file.cpp
            # made half that file look like one enormous string.
            m = CHAR_LITERAL.match(text, i)
            if m:
                out.append("'x'")
                i = m.end()
            else:
                out.append(text[i])
                i += 1
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


def literals_in(arg):
    """Every literal in an argument, for the ones that are not just a string.

    A label is often a choice rather than a constant - `hint("A", dropping ?
    "-" : "drop")` - and both sides of that go through the sink, so both are
    labels. Adjacent literals are joined first, so a sentence written across
    several lines counts as one.
    """
    whole = whole_literal(arg)
    if whole is not None:
        return [whole]
    out = []
    parts = []
    i = 0
    while i < len(arg):
        m = LITERAL.match(arg, i)
        if m:
            parts.append(m.group(1))
            i = m.end()
            while i < len(arg) and arg[i] in " \t\n":
                i += 1
            continue
        if parts:
            out.append(unescape("".join(parts)))
            parts = []
        i += 1
    if parts:
        out.append(unescape("".join(parts)))
    return out


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


def top_level_braces(body):
    """The '{' offsets of the rows of a table, ignoring braces inside them."""
    starts = []
    depth = 0
    i = 0
    while i < len(body):
        c = body[i]
        if c == '"':
            m = LITERAL.match(body, i)
            if m:
                i = m.end()
                continue
        if c == "{":
            if depth == 0:
                starts.append(i)
            depth += 1
        elif c == "}":
            depth -= 1
        i += 1
    return starts


def scan_table(text, name, fields, path, found):
    # `kTabs[Tab::Count] = {`, but also `const std::vector<Trophy> kTrophies = {`.
    m = re.search(r"\b" + name + r"\s*(?:\[[^\]]*\])?\s*=\s*\{", text)
    if not m:
        print("i18n_scan: no table %s in %s" % (name, os.path.basename(path)),
              file=sys.stderr)
        return
    body = brace_body(text, m.end() - 1)
    if body is None:
        return
    # A table of rows is a list of brace groups; a flat table - the twelve
    # month names - is one row with no braces of its own.
    starts = top_level_braces(body)
    entries = [brace_body(body, start) for start in starts] if starts else [body]
    for entry in entries:
        if entry is None:
            continue
        if fields is None:
            # Every literal in the row, however deeply it sits: a flat table of
            # month names, or the card themes buried in a palette next to their
            # colours.
            for literal in literals_in(entry):
                if literal:
                    found.setdefault(literal, set()).add(os.path.basename(path))
            continue
        columns = split_args(entry)
        for pos in fields:
            if pos >= len(columns):
                continue
            literal = whole_literal(columns[pos])
            if literal:
                found.setdefault(literal, set()).add(os.path.basename(path))


def scan_constants(text, path, found):
    for entry in CONSTANTS:
        if os.path.basename(path) != entry["file"]:
            continue
        m = re.search(r"\b" + entry["name"] + r"\s*=\s*((?:\"(?:[^\"\\]|\\.)*\"\s*)+);",
                      text)
        if not m:
            print("i18n_scan: no constant %s in %s"
                  % (entry["name"], os.path.basename(path)), file=sys.stderr)
            continue
        literal = whole_literal(m.group(1))
        if literal:
            found.setdefault(literal, set()).add(os.path.basename(path))


def scan_returners(text, path, found):
    for name in RETURNERS:
        for m in re.finditer(r"\b" + name + r"\s*\([^)]*\)\s*(?:const\s*)?\{", text):
            body = brace_body(text, m.end() - 1)
            if body is None:
                continue
            for ret in re.finditer(r"\breturn\s+((?:\"(?:[^\"\\]|\\.)*\"\s*)+);", body):
                literal = whole_literal(ret.group(1))
                if literal:
                    found.setdefault(literal, set()).add(os.path.basename(path))


def scan_file(path, found):
    with open(path, encoding="utf-8") as handle:
        text = strip_comments(handle.read())

    scan_constants(text, path, found)
    scan_returners(text, path, found)

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
                for literal in literals_in(args[pos]):
                    if literal:
                        found.setdefault(literal, set()).add(os.path.basename(path))


INCLUDE_LINE = re.compile(r"^\s*#\s*include.*$", re.MULTILINE)


def loose_labels(said, catalog_keys):
    """Every literal that reads like a sentence, minus the ones already known.

    The precise scan above follows the code: a literal handed to a sink, or to
    tr(), or sitting in a table it knows about. What it cannot see is a literal
    parked in a local variable that reaches a sink several lines later - which
    is exactly how the update row in Settings was left in English. This is the
    net for that: a guess, printed only when asked for, and noisy on purpose.
    """
    out = {}
    for folder, _, names in os.walk(SOURCE):
        for name in sorted(names):
            if not name.endswith((".cpp", ".h")):
                continue
            if name.startswith("lang_") or name in LOOSE_SKIP_FILES:
                continue
            path = os.path.join(folder, name)
            with open(path, encoding="utf-8") as handle:
                text = INCLUDE_LINE.sub("", strip_comments(handle.read()))
            for run in LITERAL_RUN.findall(text):
                label = whole_literal(run)
                if label is None:
                    continue
                if label in said or label in catalog_keys:
                    continue
                if not looks_like_a_label(label):
                    continue
                out.setdefault(label, set()).add(name)
    return out


def looks_like_a_label(text):
    stripped = text.strip()
    if len(stripped) < 4:
        return False
    if "/" in stripped or "\\" in stripped or ":" in stripped:
        return False  # a path, a url, a log prefix
    if not re.search(r"[A-Za-z]{3}", stripped):
        return False
    words = [w for w in re.split(r"\s+", stripped) if w]
    if len(words) == 1:
        # One word is usually a key or an id; a capitalised one may be a label.
        return bool(re.fullmatch(r"[A-Z][a-z]{3,}", words[0]))
    # A sentence in the app's voice, not a format template or an sql-ish blob.
    return bool(re.search(r"[a-z]{3}", stripped))


def scan_sources():
    found = {}
    for folder, _, names in os.walk(SOURCE):
        for name in sorted(names):
            if name.endswith((".cpp", ".h")) and not name.startswith("lang_"):
                scan_file(os.path.join(folder, name), found)
    return found


# A printf specifier, as format() understands them.
SPECIFIER = re.compile(
    r"%[-+ #0]*[0-9]*(?:\.[0-9]+)?(?:hh|ll|[hljzt]|L)?[a-zA-Z%]")


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
    parser.add_argument("--all", action="store_true",
                        help="one line of coverage per language, then stop")
    parser.add_argument("--loose", action="store_true",
                        help="also list literals that read like labels but reach "
                             "no sink this tool knows about")
    args = parser.parse_args()

    said = scan_sources()

    if args.all:
        # --check here used to print the stale count and still exit 0, which
        # made `--all --check` in a script a check that could not fail. It
        # answers for every language now, and the summary is only as good as
        # its exit code.
        bad = 0
        for code in sorted(CATALOGS):
            rows = read_catalog(CATALOGS[code])
            seen = {}
            duplicated = 0
            for source, target in rows:
                if source in seen:
                    duplicated += 1
                    continue
                seen[source] = target
            done = sum(1 for s, t in seen.items() if t != s and t)
            same = sum(1 for s, t in seen.items() if t == s)
            stale = sum(1 for s in seen if s not in said)
            notes = ""
            if stale:
                notes += ", %d stale" % stale
            if duplicated:
                notes += ", %d duplicated" % duplicated
            print("%s: %4d of %d translated, %2d the same in both%s"
                  % (code, done, len(said), same, notes))
            if stale or duplicated:
                bad += 1
        if args.check and bad:
            print("%d of %d catalogues have stale or duplicated entries; "
                  "--lang <code> --check says which" % (bad, len(CATALOGS)))
            return 1
        return 0

    rows = read_catalog(CATALOGS[args.lang])

    seen = {}
    duplicates = []
    for source, target in rows:
        if source in seen:
            duplicates.append(source)
        else:
            seen[source] = target

    # A translation whose specifiers do not match its English is not a
    # cosmetic mistake: the line goes to format(), and an extra %s reads a
    # varargs slot that was never passed.
    mismatched = [
        (source, target)
        for source, target in seen.items()
        if SPECIFIER.findall(source) != SPECIFIER.findall(target)
    ]

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
    for source, target in mismatched:
        print("\nformat mismatch - %s\n            vs %s"
              % (escape(source), escape(target)))

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

    loose = loose_labels(said, set(seen))
    if loose:
        if args.loose:
            print("\nnot going through any sink this tool knows about (%d) - some of "
                  "these are labels parked in a variable, most are not labels at all:"
                  % len(loose))
            for label in sorted(loose):
                print("  %-60s %s" % (escape(label)[:60], ", ".join(sorted(loose[label]))))
        else:
            print("\n%d literals read like labels but reach no sink this tool knows "
                  "about; --loose lists them." % len(loose))

    if args.check and (stale or duplicates or mismatched):
        print("\nstale, duplicated or mismatched entries: fix lang_%s.cpp"
              % args.lang)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
