#include "CommandMetadata.h"

#include <algorithm>  // for stable_sort, find_if
#include <cctype>     // for tolower
#include <map>        // for map
#include <set>        // for set
#include <utility>    // for move, pair

#include <glib.h>  // for g_utf8_normalize, g_unichar_tolower

namespace xoj::command {

namespace {

/// A word of a searchable field, folded, as the matcher compares against it.
auto splitWords(const std::string& folded) -> std::vector<std::string> {
    std::vector<std::string> words;
    std::string current;
    for (char c: folded) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            current.push_back(c);
        } else if (!current.empty()) {
            words.emplace_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) {
        words.emplace_back(std::move(current));
    }
    return words;
}

auto startsWith(const std::string& text, const std::string& prefix) -> bool {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

/**
 * How well `token` matches one of the words of a field, or 0 when it does not.
 *
 * The scores are what make the ranking readable: the whole title beats a word of the title, a word
 * that starts with the token beats a word that merely contains it, and a title beats a keyword,
 * which beats a category.
 */
auto scoreWords(const std::vector<std::string>& words, const std::string& token, int wordScore, int containsScore)
        -> int {
    int best = 0;
    for (const std::string& word: words) {
        if (word == token) {
            best = std::max(best, wordScore + 20);
        } else if (startsWith(word, token)) {
            best = std::max(best, wordScore);
        } else if (word.find(token) != std::string::npos) {
            best = std::max(best, containsScore);
        }
    }
    return best;
}

/// Whether `token` can be read out of `text` by skipping characters, as fuzzy matching does.
auto subsequenceScore(const std::string& text, const std::string& token) -> int {
    size_t pos = 0;
    int consecutive = 0;
    int wordStarts = 0;
    bool previousMatched = false;
    for (char c: text) {
        if (pos < token.size() && c == token[pos]) {
            if (previousMatched) {
                consecutive++;
            }
            if (!previousMatched) {
                wordStarts++;
            }
            previousMatched = true;
            pos++;
        } else {
            previousMatched = false;
        }
    }
    if (pos < token.size()) {
        return 0;
    }
    // The characters of the field the token did not use: the tighter the match, the higher the
    // score, so "Expected PDF" beats "Export as PDF" for "exppdf".
    const int unmatched = static_cast<int>(text.size() - token.size());
    return 10 + 3 * wordStarts + 2 * consecutive - unmatched;
}

constexpr int SCORE_TITLE_WORD = 100;
constexpr int SCORE_TITLE_CONTAINS = 60;
constexpr int SCORE_KEYWORD_WORD = 80;
constexpr int SCORE_KEYWORD_CONTAINS = 40;
constexpr int SCORE_CATEGORY_WORD = 70;
constexpr int SCORE_CATEGORY_CONTAINS = 30;
/// The whole title is the query: the strongest evidence that this is the command meant.
constexpr int SCORE_WHOLE_TITLE = 40;

/// What one token of the query is worth against one command, or 0 when it does not match at all.
auto tokenScore(const CommandMetadata& command, const std::string& foldedTitle, const std::string& foldedCategory,
                const std::string& token) -> int {
    int best = scoreWords(splitWords(foldedTitle), token, SCORE_TITLE_WORD, SCORE_TITLE_CONTAINS);
    for (const std::string& keyword: command.keywords) {
        best = std::max(best,
                        scoreWords(splitWords(foldedForSearch(keyword)), token, SCORE_KEYWORD_WORD,
                                   SCORE_KEYWORD_CONTAINS));
    }
    best = std::max(best, scoreWords(splitWords(foldedCategory), token, SCORE_CATEGORY_WORD, SCORE_CATEGORY_CONTAINS));
    best = std::max(best, subsequenceScore(foldedTitle, token));
    return best;
}

auto tokenize(const std::string& folded) -> std::vector<std::string> {
    return splitWords(folded);
}

}  // namespace

auto foldedForSearch(std::string_view text) -> std::string {
    if (text.empty()) {
        return {};
    }

    std::string source(text);
    gchar* normalized = g_utf8_normalize(source.c_str(), static_cast<gssize>(source.size()), G_NORMALIZE_NFD);
    if (normalized == nullptr) {
        // Not valid UTF-8: fold what can be folded and keep the rest as it stands.
        std::string fallback = source;
        std::transform(fallback.begin(), fallback.end(), fallback.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return fallback;
    }

    std::string folded;
    folded.reserve(source.size());
    for (const char* p = normalized; *p != '\0';) {
        const gunichar c = g_utf8_get_char(p);
        p = g_utf8_next_char(p);

        // The accents the decomposition separated from their letter are what makes a query typed
        // without a diacritic find a translated title that carries one: dropping them is the whole
        // point of normalizing to NFD.
        if (g_unichar_combining_class(c) != 0) {
            continue;
        }

        char buffer[7];
        const int length = g_unichar_to_utf8(g_unichar_tolower(c), buffer);
        folded.append(buffer, static_cast<size_t>(length));
    }

    g_free(normalized);
    return folded;
}

auto validateCommands(const std::vector<CommandMetadata>& commands) -> std::vector<std::string> {
    std::vector<std::string> problems;
    std::set<std::string> seen;

    for (const CommandMetadata& command: commands) {
        if (command.id.empty()) {
            problems.emplace_back("a command without an id");
            continue;
        }
        if (!seen.insert(command.id).second) {
            problems.emplace_back("two commands share the id \"" + command.id + "\"");
        }
        if (command.title.empty()) {
            problems.emplace_back("command \"" + command.id + "\" has no title");
        }
        if (command.category.empty()) {
            problems.emplace_back("command \"" + command.id + "\" has no category");
        }
    }

    return problems;
}

auto rankCommands(const std::vector<CommandMetadata>& commands, std::string_view query) -> std::vector<size_t> {
    std::vector<size_t> order;
    const std::string foldedQuery = foldedForSearch(query);
    const std::vector<std::string> tokens = tokenize(foldedQuery);

    if (tokens.empty()) {
        order.resize(commands.size());
        for (size_t i = 0; i < commands.size(); i++) {
            order[i] = i;
        }
        return order;
    }

    std::vector<std::pair<size_t, int>> scored;
    for (size_t i = 0; i < commands.size(); i++) {
        const CommandMetadata& command = commands[i];
        const std::string foldedTitle = foldedForSearch(command.title);
        const std::string foldedCategory = foldedForSearch(command.category);

        int total = 0;
        bool matches = true;
        for (const std::string& token: tokens) {
            const int score = tokenScore(command, foldedTitle, foldedCategory, token);
            if (score == 0) {
                matches = false;
                break;
            }
            total += score;
        }
        if (!matches) {
            continue;
        }

        if (foldedTitle == foldedQuery) {
            total += SCORE_WHOLE_TITLE;
        }
        scored.emplace_back(i, total);
    }

    // Stable, so that two commands the query fits equally well stay in the order the user's menus
    // put them in rather than in an order of their own.
    std::stable_sort(scored.begin(), scored.end(),
                     [](const std::pair<size_t, int>& a, const std::pair<size_t, int>& b) { return a.second > b.second; });

    order.reserve(scored.size());
    for (const auto& [index, score]: scored) {
        order.emplace_back(index);
    }
    return order;
}

auto findAcceleratorConflicts(const std::vector<CommandMetadata>& commands) -> std::vector<AcceleratorConflict> {
    struct Group {
        AcceleratorConflict conflict;
        /// The actions already seen on this accelerator, so that one action reached twice stays one.
        std::set<std::string> actions;
    };
    std::vector<Group> groups;

    for (const CommandMetadata& command: commands) {
        if (command.accelerator.empty()) {
            continue;
        }

        auto known = std::find_if(groups.begin(), groups.end(), [&command](const Group& group) {
            return group.conflict.accelerator == command.accelerator;
        });
        if (known == groups.end()) {
            groups.push_back(Group{AcceleratorConflict{command.accelerator, {command.id}}, {command.actionName}});
            continue;
        }

        // The same action reached twice - a menu entry and the tool button beside it - is not a
        // conflict: pressing the keys runs the same command either way.
        if (known->actions.insert(command.actionName).second) {
            known->conflict.commandIds.emplace_back(command.id);
        }
    }

    std::vector<AcceleratorConflict> conflicts;
    for (Group& group: groups) {
        if (group.conflict.commandIds.size() >= 2) {
            conflicts.emplace_back(std::move(group.conflict));
        }
    }
    return conflicts;
}

}  // namespace xoj::command
