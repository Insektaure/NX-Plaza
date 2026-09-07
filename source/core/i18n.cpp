#include "core/i18n.h"

#include "core/log.h"

#include <cstring>
#include <string_view>
#include <unordered_map>

namespace nxp {
namespace {

    // Named in itself first, because that is the name somebody looking for
    // their own language is looking for.
    const LangInfo kLangs[Lang_Count] = {
        { "en", "English", "English" },
        { "fr", "Français", "French" },
        { "de", "Deutsch", "German" },
        { "es", "Español", "Spanish" },
        { "it", "Italiano", "Italian" },
        { "nl", "Nederlands", "Dutch" },
        { "pt", "Português", "Portuguese" },
        { "ru", "Русский", "Russian" },
        { "ja", "日本語", "Japanese" },
    };

    Catalog catalogFor(Lang lang)
    {
        switch (lang) {
        case Lang_French:
            return frenchCatalog();
        case Lang_German:
            return germanCatalog();
        case Lang_Spanish:
            return spanishCatalog();
        case Lang_Italian:
            return italianCatalog();
        case Lang_Dutch:
            return dutchCatalog();
        case Lang_Portuguese:
            return portugueseCatalog();
        case Lang_Russian:
            return russianCatalog();
        case Lang_Japanese:
            return japaneseCatalog();
        default:
            return Catalog {};
        }
    }

    // The table, keyed on the English. string_view keys are safe here because
    // every key is a literal in the binary: they outlive the map by definition.
    //
    // Built once when the language is set and then only read, which is what
    // makes tr() safe to call from the sync thread's toasts as well as from
    // the frame.
    struct Table {
        std::unordered_map<std::string_view, const char*> lookup;
        size_t translated = 0;
    };

    Lang g_lang = Lang_English;
    bool g_built = false;
    Table& table()
    {
        static Table t;
        return t;
    }

    void build(Lang lang)
    {
        Table& t = table();
        t.lookup.clear();
        t.translated = 0;

        Catalog cat = catalogFor(lang);
        t.lookup.reserve(cat.count * 2);
        for (size_t i = 0; i < cat.count; i++) {
            const Translation& e = cat.entries[i];
            if (!e.source || !e.target || e.source[0] == '\0')
                continue;
            // An entry whose target is empty or the same as its source is not a
            // translation, and counting it would flatter the language.
            if (e.target[0] == '\0' || std::strcmp(e.source, e.target) == 0)
                continue;
            auto added = t.lookup.emplace(std::string_view(e.source), e.target);
            if (!added.second) {
                // Two rows for one English string: the first wins, and the
                // second is a copy-paste that would otherwise sit there
                // silently doing nothing.
                LOG("i18n: %s lists \"%s\" twice", langInfo(lang).english, e.source);
                continue;
            }
            t.translated++;
        }
    }

} // namespace

const LangInfo& langInfo(Lang lang)
{
    int index = static_cast<int>(lang);
    if (index < 0 || index >= Lang_Count)
        index = Lang_English;
    return kLangs[index];
}

Lang langFromCode(const char* code)
{
    if (!code || code[0] == '\0')
        return Lang_English;
    for (int i = 0; i < Lang_Count; i++) {
        if (std::strcmp(kLangs[i].code, code) == 0)
            return static_cast<Lang>(i);
    }
    return Lang_English;
}

Lang currentLang() { return g_lang; }

void setCurrentLang(Lang lang)
{
    int index = static_cast<int>(lang);
    if (index < 0 || index >= Lang_Count)
        lang = Lang_English;
    if (lang == g_lang && g_built)
        return;

    g_lang = lang;
    build(lang);
    g_built = true;
    LOG("i18n: %s, %zu strings", langInfo(lang).english, table().translated);
}

const char* tr(const char* english)
{
    if (g_lang == Lang_English || !english || english[0] == '\0')
        return english;
    const Table& t = table();
    auto it = t.lookup.find(std::string_view(english));
    return it == t.lookup.end() ? english : it->second;
}

std::string tr(const std::string& english)
{
    if (g_lang == Lang_English || english.empty())
        return english;
    const Table& t = table();
    auto it = t.lookup.find(std::string_view(english));
    return it == t.lookup.end() ? english : std::string(it->second);
}

size_t translatedCount() { return table().translated; }

} // namespace nxp
