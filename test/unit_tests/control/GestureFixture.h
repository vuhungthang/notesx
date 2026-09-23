/*
 * Xournal++
 *
 * Reading the Plan 008 gesture fixture corpus - test support, no recognition logic
 *
 * A fixture is a small text file beside the recognizers it exercises:
 *
 *   # name: deliberate-circle-large
 *   # recognizer: circle
 *   # expect: match | nomatch
 *   # min-confidence: 0.5          (only for a match)
 *   x y
 *   x y t                          (t is milliseconds; omitted when the fixture has no timing)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <config-test.h>

#include "control/gestures/GestureRecognizer.h"

namespace xoj::gesture::test {

namespace fs = std::filesystem;

/// Strip surrounding spaces, tabs and carriage returns from a fixture directive.
inline auto trim(std::string value) -> std::string {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

struct GestureFixture {
    std::string file;
    std::string name;
    std::string recognizer;
    bool expectMatch = false;
    double minConfidence = 0.0;
    GestureStroke stroke;
};

inline auto loadGestureFixture(const fs::path& path) -> std::optional<GestureFixture> {
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }

    GestureFixture fixture;
    fixture.file = path.filename().string();

    std::vector<StrokePoint> points;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#') {
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            const std::string key = trim(line.substr(1, colon - 1));
            std::string value = trim(line.substr(colon + 1));

            if (key == "name") {
                fixture.name = value;
            } else if (key == "recognizer") {
                fixture.recognizer = value;
            } else if (key == "expect") {
                fixture.expectMatch = value == "match";
            } else if (key == "min-confidence") {
                fixture.minConfidence = std::stod(value);
            }
            continue;
        }

        std::istringstream row(line);
        StrokePoint p;
        if (row >> p.x >> p.y) {
            if (!(row >> p.t)) {
                p.t = 0.0;
            }
            points.push_back(p);
        }
    }

    fixture.stroke = GestureStroke(std::move(points));
    return fixture;
}

/// Every fixture under test/files/gestures, in a stable order.
inline auto loadGestureCorpus() -> std::vector<GestureFixture> {
    std::vector<GestureFixture> fixtures;
    const fs::path root(GET_TESTFILE(u8"gestures"));
    if (!fs::exists(root)) {
        return fixtures;
    }

    std::vector<fs::path> paths;
    for (const auto& entry: fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".txt") {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());

    for (const auto& path: paths) {
        if (auto fixture = loadGestureFixture(path)) {
            fixtures.push_back(std::move(*fixture));
        }
    }
    return fixtures;
}

}  // namespace xoj::gesture::test
