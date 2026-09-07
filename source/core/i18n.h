#pragma once

#include <cstddef>
#include <string>

namespace nxp {

// Everything the app says is written in English in the code, and a language is
// a table that swaps those strings for its own:
//
//     r.text(x, y, tr("Race for free"), style);
//
// The English text is the key. There is no string id to keep in step with
// anything, an untranslated string comes out as English rather than as
// "games.race.free", and grep still finds the words that are on the screen.
//
// Nothing about the drawing had to change for this. The glyphs come from the
// console's own shared fonts - Latin, Cyrillic, Japanese, both Chinese sets and
// Korean, with per-codepoint fallback - and the renderer already measures,
// kerns, wraps and ellipsizes UTF-8, breaking CJK anywhere and everything else
// at spaces. What a language costs is its words, and the room they take.
//
// tr() returns a pointer to a literal, so it never allocates and drops into
// format() and Renderer::text() unchanged. The pointer stays good for the life
// of the app whatever the language is set to afterwards.
//
// Two things to keep in mind at a call site:
//
//   - Wrap the string where it is *used*, not where a static table is built.
//     A table of `static const char*` is filled once, so a tr() inside it
//     would freeze the language the app happened to start in.
//   - A sentence assembled out of fragments cannot be translated, because
//     word order is not ours to choose. Give tr() the whole sentence with its
//     format specifiers in it: `format(tr("%u coins to spend"), n)`.

enum Lang : int {
    Lang_English = 0, // the source language: the literals in the code
    Lang_French,
    Lang_German,
    Lang_Spanish,
    Lang_Italian,
    Lang_Dutch,
    Lang_Portuguese,
    Lang_Russian,
    Lang_Japanese,
    Lang_Korean,
    Lang_ChineseSimplified,
    Lang_ChineseTraditional,
    Lang_Count,
};

struct LangInfo {
    const char* code;    // what goes in profile.json, and what the console uses
    const char* endonym; // what the language calls itself, for the settings list
    const char* english; // what it is called in English, for the log
};

const LangInfo& langInfo(Lang lang);

// An unknown, empty or not-yet-translated code gives English.
Lang langFromCode(const char* code);

Lang currentLang();

// Takes effect on the next frame: every screen builds its words as it draws.
void setCurrentLang(Lang lang);

// The whole of the call site. Falls back to the English it was handed.
const char* tr(const char* english);

// For the places that already hold a std::string - a hint label, a button that
// is sometimes a literal and sometimes assembled. A string that was built at
// runtime simply will not be in the table, which is the right answer for it.
std::string tr(const std::string& english);

// How many of the current language's entries differ from their English source,
// for the row in Settings that says how complete a language is.
size_t translatedCount();

// ------------------------------------------------------------------ catalogs
//
// One table per language, in its own file. Sorted or not, it makes no
// difference: the table is read once into a lookup keyed on the English.

struct Translation {
    const char* source;
    const char* target;
};

struct Catalog {
    const Translation* entries = nullptr;
    size_t count = 0;
};

Catalog frenchCatalog();
Catalog germanCatalog();
Catalog spanishCatalog();
Catalog italianCatalog();
Catalog dutchCatalog();
Catalog portugueseCatalog();
Catalog russianCatalog();
Catalog japaneseCatalog();
Catalog koreanCatalog();
Catalog chineseSimplifiedCatalog();
Catalog chineseTraditionalCatalog();

} // namespace nxp
