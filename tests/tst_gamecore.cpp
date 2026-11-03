// tst_gamecore.cpp — тесты данных, фильтрации и трёх подсказок gamecore.
#include <QtTest>

#include "gamecore/gamecore.h"

#include <numeric>

using namespace gamecore;

namespace {

CandidateSet allCandidates()
{
    CandidateSet candidates(kCharacterCount);
    std::iota(candidates.begin(), candidates.end(), 0);
    return candidates;
}

// Ручная (brute-force) реализация выбора вопроса по дисперсии — эталон для теста 7.
HintResult bruteForceVariance(const CandidateSet& candidates)
{
    HintResult best;
    const double total = static_cast<double>(candidates.size());
    for (QuestionId q = 0; q < kQuestionCount; ++q) {
        int yes = 0;
        for (CharacterId c : candidates) {
            if (value(c, q) == 1) {
                ++yes;
            }
        }
        const double p = static_cast<double>(yes) / total;
        const double score = 4.0 * p * (1.0 - p);
        if (score > best.score) {
            best.score = score;
            best.question = q;
        }
    }
    return best;
}

} // namespace

class TestGameCore : public QObject
{
    Q_OBJECT

private slots:
    void data_integrity();
    void value_roundtrip();
    void filter_yes();
    void filter_no();
    void filter_empty();
    void filter_preservesOrder();
    void variance_max();
    void variance_excluded();
    void variance_noInfo();
    void rf_valid();
    void rf_deterministic();
    void catboost_valid();
    void catboost_deterministic();
    void hints_noInfo();
};

// 1. Данные: 60 имён, 139 вопросов, матрица 60*139, значения только -1/+1.
void TestGameCore::data_integrity()
{
    QCOMPARE(static_cast<int>(characterNames().size()), kCharacterCount);
    QCOMPARE(static_cast<int>(questionTexts().size()), kQuestionCount);
    QCOMPARE(static_cast<int>(matrix().size()), kCharacterCount * kQuestionCount);

    for (const std::string& name : characterNames()) {
        QVERIFY(!name.empty());
    }
    for (const std::string& text : questionTexts()) {
        QVERIFY(!text.empty());
    }
    for (std::int8_t cell : matrix()) {
        QVERIFY(cell == 1 || cell == -1);
    }
}

// 2. value(c,q) совпадает с матрицей.
void TestGameCore::value_roundtrip()
{
    const std::vector<std::int8_t>& m = matrix();
    const int probes[][2] = {
        {0, 0}, {0, 138}, {59, 0}, {59, 138}, {17, 42}, {24, 12}, {33, 100},
    };
    for (const auto& probe : probes) {
        const int c = probe[0];
        const int q = probe[1];
        QCOMPARE(value(c, q),
                 static_cast<int>(m[static_cast<std::size_t>(c) * kQuestionCount + q]));
    }
}

// 3. filterCandidates(..., +1) == подмножество с value==+1.
void TestGameCore::filter_yes()
{
    const CandidateSet all = allCandidates();
    const QuestionId questions[] = {0, 5, 17, 100, 138};
    for (QuestionId q : questions) {
        CandidateSet expected;
        for (CharacterId c : all) {
            if (value(c, q) == 1) {
                expected.push_back(c);
            }
        }
        QCOMPARE(filterCandidates(all, q, 1), expected);
    }
}

// 4. filterCandidates(..., -1) == подмножество с value==-1.
void TestGameCore::filter_no()
{
    const CandidateSet all = allCandidates();
    const QuestionId questions[] = {0, 5, 17, 100, 138};
    for (QuestionId q : questions) {
        CandidateSet expected;
        for (CharacterId c : all) {
            if (value(c, q) == -1) {
                expected.push_back(c);
            }
        }
        QCOMPARE(filterCandidates(all, q, -1), expected);
    }
}

// 5. Пустой вход -> пустой выход.
void TestGameCore::filter_empty()
{
    const CandidateSet empty;
    QVERIFY(filterCandidates(empty, 0, 1).empty());
    QVERIFY(filterCandidates(empty, 42, -1).empty());
}

// 6. Несортированный вход с повтором -> отсортированный результат без повторов.
void TestGameCore::filter_preservesOrder()
{
    const CandidateSet messy = {59, 0, 30, 0, 12, 59};
    const CandidateSet result = filterCandidates(messy, 0, 1);
    QVERIFY(std::is_sorted(result.begin(), result.end()));
    const auto duplicates = std::adjacent_find(result.begin(), result.end());
    QVERIFY(duplicates == result.end());

    CandidateSet expected;
    for (CharacterId c : {0, 12, 30, 59}) {
        if (value(c, 0) == 1) {
            expected.push_back(c);
        }
    }
    QCOMPARE(result, expected);
}

// 7. hintVariance совпадает с brute-force максимумом 4p(1-p).
void TestGameCore::variance_max()
{
    const CandidateSet all = allCandidates();
    const HintResult expected = bruteForceVariance(all);
    const HintResult actual = hintVariance(all, {});
    QVERIFY(expected.score > 0.0);
    QCOMPARE(actual.question, expected.question);
    QVERIFY(qFuzzyCompare(actual.score, expected.score));
}

// 8. Исключённые вопросы не возвращаются.
void TestGameCore::variance_excluded()
{
    const CandidateSet all = allCandidates();
    const HintResult best = hintVariance(all, {});
    QVERIFY(best.question >= 0);

    std::vector<QuestionId> excluded = {best.question};
    const HintResult next = hintVariance(all, excluded);
    QVERIFY(next.question >= 0);
    QVERIFY(next.question != best.question);

    excluded.push_back(next.question);
    const HintResult third = hintVariance(all, excluded);
    QVERIFY(third.question >= 0);
    QVERIFY(third.question != best.question);
    QVERIFY(third.question != next.question);
}

// 9. Кандидаты с одинаковыми строками -> вопроса нет.
void TestGameCore::variance_noInfo()
{
    const CandidateSet twins = {3, 3};
    const HintResult result = hintVariance(twins, {});
    QCOMPARE(result.question, -1);
    QVERIFY(qFuzzyIsNull(result.score));
}

// 10. Random Forest возвращает валидный вопрос с положительным score.
void TestGameCore::rf_valid()
{
    const HintResult result = hintRandomForest(allCandidates(), {});
    QVERIFY(result.question >= 0);
    QVERIFY(result.question < kQuestionCount);
    QVERIFY(result.score > 0.0);
}

// 11. Random Forest детерминирован.
void TestGameCore::rf_deterministic()
{
    const CandidateSet all = allCandidates();
    const HintResult first = hintRandomForest(all, {});
    const HintResult second = hintRandomForest(all, {});
    QCOMPARE(first.question, second.question);
    QCOMPARE(first.score, second.score);

    const CandidateSet filtered = filterCandidates(all, 0, 1);
    const HintResult third = hintRandomForest(filtered, {0});
    const HintResult fourth = hintRandomForest(filtered, {0});
    QCOMPARE(third.question, fourth.question);
    QCOMPARE(third.score, fourth.score);
}

// 12. CatBoost возвращает валидный вопрос с положительным score.
void TestGameCore::catboost_valid()
{
    const HintResult result = hintCatBoost(allCandidates(), {});
    QVERIFY(result.question >= 0);
    QVERIFY(result.question < kQuestionCount);
    QVERIFY(result.score > 0.0);
}

// 13. CatBoost детерминирован.
void TestGameCore::catboost_deterministic()
{
    const CandidateSet all = allCandidates();
    const HintResult first = hintCatBoost(all, {});
    const HintResult second = hintCatBoost(all, {});
    QCOMPARE(first.question, second.question);
    QCOMPARE(first.score, second.score);
}

// 14. Нет информативных вопросов -> все три подсказки возвращают -1.
void TestGameCore::hints_noInfo()
{
    const CandidateSet twins = {3, 3};
    QCOMPARE(hintVariance(twins, {}).question, -1);
    QCOMPARE(hintRandomForest(twins, {}).question, -1);
    QCOMPARE(hintCatBoost(twins, {}).question, -1);

    const CandidateSet single = {5};
    QCOMPARE(hintVariance(single, {}).question, -1);
    QCOMPARE(hintRandomForest(single, {}).question, -1);
    QCOMPARE(hintCatBoost(single, {}).question, -1);

    // Все вопросы исключены — информативного тоже нет.
    const CandidateSet all = allCandidates();
    std::vector<QuestionId> everything(kQuestionCount);
    std::iota(everything.begin(), everything.end(), 0);
    QCOMPARE(hintVariance(all, everything).question, -1);
    QCOMPARE(hintRandomForest(all, everything).question, -1);
    QCOMPARE(hintCatBoost(all, everything).question, -1);
}

QTEST_APPLESS_MAIN(TestGameCore)

#include "tst_gamecore.moc"
