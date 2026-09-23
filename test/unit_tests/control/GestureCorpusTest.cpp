/*
 * Xournal++
 *
 * The Plan 008 corpus gate: every fixture, in both directions
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <iostream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "control/gestures/CircleGestureRecognizer.h"
#include "control/gestures/GestureRecognizer.h"
#include "control/gestures/ScribbleGestureRecognizer.h"

#include "GestureFixture.h"

namespace {

using namespace xoj::gesture;
using xoj::gesture::test::GestureFixture;

/**
 * Run one fixture through the recognizer it names.
 *
 * This is the whole gate: a fixture that says "match" must be recognised and reach the confidence
 * it claims; a fixture that says "nomatch" must not be recognised at all. There is no third answer
 * and no fixture is excused.
 */
struct OutcomeResult {
    bool correct = false;
    bool matched = false;
    double confidence = 0.0;
};

auto run(const CircleGestureRecognizer& circle, const ScribbleGestureRecognizer& scribble,
         const GestureFixture& fixture) -> OutcomeResult {
    std::optional<GestureCandidate> candidate;
    if (fixture.recognizer == "circle") {
        candidate = circle.recognize(fixture.stroke);
    } else if (fixture.recognizer == "scribble") {
        candidate = scribble.recognize(fixture.stroke);
    } else {
        // A fixture naming a recognizer that does not exist is a mistake, not a pass.
        return {false, false, 0.0};
    }

    OutcomeResult result;
    result.matched = candidate.has_value();
    result.confidence = candidate ? candidate->confidence : 0.0;
    result.correct = fixture.expectMatch ? (candidate.has_value() && candidate->confidence >= fixture.minConfidence) :
                                           !candidate.has_value();
    return result;
}

TEST(GestureCorpusTest, everyFixtureIsClassifiedAsItSays) {
    CircleGestureRecognizer circle;
    ScribbleGestureRecognizer scribble;
    const std::vector<GestureFixture> corpus = xoj::gesture::test::loadGestureCorpus();

    ASSERT_FALSE(corpus.empty()) << "no fixtures found under test/files/gestures";

    int matched = 0;
    int notMatched = 0;
    int wrong = 0;
    for (const GestureFixture& fixture: corpus) {
        const OutcomeResult result = run(circle, scribble, fixture);
        if (result.matched) {
            matched++;
        } else {
            notMatched++;
        }
        if (!result.correct) {
            wrong++;
            std::cout << "MISCLASSIFIED " << fixture.file << " (" << fixture.name << ", " << fixture.recognizer
                      << ", expected " << (fixture.expectMatch ? "match" : "nomatch")
                      << ") -> matched=" << (result.matched ? "yes" : "no") << " confidence=" << result.confidence
                      << std::endl;
        }
    }

    std::cout << "corpus: " << corpus.size() << " fixtures, " << matched << " matched, " << notMatched
              << " not matched, " << wrong << " misclassified" << std::endl;

    EXPECT_EQ(wrong, 0);
}

/// The plan's acceptance is stated as zero false positives on the included ordinary marks. This
/// asserts it separately from the overall count so a failure says which gate broke.
TEST(GestureCorpusTest, zeroFalsePositivesOnOrdinaryMarks) {
    CircleGestureRecognizer circle;
    ScribbleGestureRecognizer scribble;
    const std::vector<GestureFixture> corpus = xoj::gesture::test::loadGestureCorpus();

    int negatives = 0;
    int falsePositives = 0;
    for (const GestureFixture& fixture: corpus) {
        if (fixture.expectMatch) {
            continue;
        }
        negatives++;
        const OutcomeResult result = run(circle, scribble, fixture);
        if (result.matched) {
            falsePositives++;
            std::cout << "FALSE POSITIVE " << fixture.file << " -> confidence=" << result.confidence << std::endl;
        }
    }

    std::cout << "corpus negatives: " << negatives << ", false positives: " << falsePositives << std::endl;
    EXPECT_GT(negatives, 0);
    EXPECT_EQ(falsePositives, 0);
}

/// And zero false negatives: a transformed example of a gesture must still be that gesture.
TEST(GestureCorpusTest, noFalseNegativesOnPositiveExamples) {
    CircleGestureRecognizer circle;
    ScribbleGestureRecognizer scribble;
    const std::vector<GestureFixture> corpus = xoj::gesture::test::loadGestureCorpus();

    int positives = 0;
    int falseNegatives = 0;
    for (const GestureFixture& fixture: corpus) {
        if (!fixture.expectMatch) {
            continue;
        }
        positives++;
        const OutcomeResult result = run(circle, scribble, fixture);
        if (!result.correct) {
            falseNegatives++;
            std::cout << "FALSE NEGATIVE " << fixture.file << " -> matched=" << (result.matched ? "yes" : "no")
                      << " confidence=" << result.confidence << std::endl;
        }
    }

    std::cout << "corpus positives: " << positives << ", false negatives: " << falseNegatives << std::endl;
    EXPECT_GT(positives, 0);
    EXPECT_EQ(falseNegatives, 0);
}

}  // namespace
