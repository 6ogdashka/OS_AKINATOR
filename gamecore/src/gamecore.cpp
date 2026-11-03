// gamecore.cpp — данные, фильтрация кандидатов и три детерминированные подсказки.
#include "gamecore/gamecore.h"

#include "data.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <random>
#include <vector>

namespace gamecore {
namespace {

constexpr std::size_t kCell(CharacterId character, QuestionId question) {
    return static_cast<std::size_t>(character) * static_cast<std::size_t>(kQuestionCount)
         + static_cast<std::size_t>(question);
}

bool validCharacter(CharacterId id) { return id >= 0 && id < kCharacterCount; }
bool validQuestion(QuestionId id) { return id >= 0 && id < kQuestionCount; }

bool answeredYes(CharacterId character, QuestionId question) {
    return generated::kMatrix[kCell(character, question)] > 0;
}

bool isExcluded(const std::vector<QuestionId>& excluded, QuestionId question) {
    return std::find(excluded.begin(), excluded.end(), question) != excluded.end();
}

// Приводит набор к контракту: отсортирован, без повторов, только валидные индексы.
CandidateSet normalized(const CandidateSet& candidates) {
    CandidateSet out;
    out.reserve(candidates.size());
    for (CharacterId id : candidates) {
        if (validCharacter(id)) {
            out.push_back(id);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<QuestionId> availableQuestions(const std::vector<QuestionId>& excluded) {
    std::vector<QuestionId> questions;
    questions.reserve(kQuestionCount);
    for (QuestionId q = 0; q < kQuestionCount; ++q) {
        if (!isExcluded(excluded, q)) {
            questions.push_back(q);
        }
    }
    return questions;
}

HintResult bestByImportance(const std::vector<double>& importance,
                            const std::vector<QuestionId>& questions) {
    HintResult best;
    for (QuestionId q : questions) {
        if (importance[static_cast<std::size_t>(q)] > best.score) {
            best.question = q;
            best.score = importance[static_cast<std::size_t>(q)];
        }
    }
    return best;
}

// --- Условная цель для ML-подсказок ---------------------------------------
// В игре нет разметки «кто именно загадан», поэтому для оценки важности
// вопросов строится детерминированная псевдо-цель: класс 1 получают кандидаты
// с числом ответов «да» выше медианы по текущему набору. Вопросы, которые
// хорошо объясняют эту цель, информативны и для сужения набора.
std::vector<int> popularityLabels(const CandidateSet& candidates, bool* usable) {
    const std::size_t n = candidates.size();
    std::vector<int> counts(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        int yes = 0;
        for (QuestionId q = 0; q < kQuestionCount; ++q) {
            if (answeredYes(candidates[i], q)) {
                ++yes;
            }
        }
        counts[i] = yes;
    }

    std::vector<int> sorted(counts);
    std::sort(sorted.begin(), sorted.end());
    const int median = sorted[n / 2];

    std::vector<int> labels(n, 0);
    int positives = 0;
    for (std::size_t i = 0; i < n; ++i) {
        labels[i] = counts[i] > median ? 1 : 0;
        positives += labels[i];
    }
    *usable = positives > 0 && positives < static_cast<int>(n);
    return labels;
}

double giniImpurity(std::size_t positives, std::size_t total) {
    if (total == 0) {
        return 0.0;
    }
    const double p = static_cast<double>(positives) / static_cast<double>(total);
    return 2.0 * p * (1.0 - p);
}

// --- Random Forest ---------------------------------------------------------
void rfGrow(const CandidateSet& candidates,
            const std::vector<int>& labels,
            const std::vector<std::size_t>& node,
            int depth,
            const std::vector<QuestionId>& features,
            std::size_t mtry,
            std::vector<double>& importance,
            std::mt19937& rng) {
    constexpr int kMaxDepth = 3;
    const std::size_t total = node.size();
    if (total < 2 || depth >= kMaxDepth) {
        return;
    }

    std::size_t positives = 0;
    for (std::size_t pos : node) {
        positives += static_cast<std::size_t>(labels[pos]);
    }
    if (positives == 0 || positives == total) {
        return; // узел чистый
    }

    const double parentImpurity = giniImpurity(positives, total);

    // Случайное подмножество признаков без повторов (детерминированный rng).
    std::vector<QuestionId> subset(features);
    const std::size_t take = std::min(mtry, subset.size());
    for (std::size_t i = 0; i < take; ++i) {
        std::uniform_int_distribution<std::size_t> pick(i, subset.size() - 1);
        std::swap(subset[i], subset[pick(rng)]);
    }
    subset.resize(take);

    QuestionId bestFeature = -1;
    double bestGain = 0.0;
    std::size_t bestYes = 0;

    for (QuestionId q : subset) {
        std::size_t yes = 0;
        std::size_t yesPositives = 0;
        for (std::size_t pos : node) {
            if (answeredYes(candidates[pos], q)) {
                ++yes;
                yesPositives += static_cast<std::size_t>(labels[pos]);
            }
        }
        if (yes == 0 || yes == total) {
            continue;
        }
        const std::size_t no = total - yes;
        const std::size_t noPositives = positives - yesPositives;
        const double gain = parentImpurity
            - (static_cast<double>(yes) / static_cast<double>(total))
                  * giniImpurity(yesPositives, yes)
            - (static_cast<double>(no) / static_cast<double>(total))
                  * giniImpurity(noPositives, no);
        if (gain > bestGain) {
            bestGain = gain;
            bestFeature = q;
            bestYes = yes;
        }
    }

    if (bestFeature < 0) {
        return;
    }

    // Важность = взвешенное по числу наблюдений падение Gini-примеси.
    importance[static_cast<std::size_t>(bestFeature)] += bestGain * static_cast<double>(total);

    std::vector<std::size_t> yesNode;
    std::vector<std::size_t> noNode;
    yesNode.reserve(bestYes);
    noNode.reserve(total - bestYes);
    for (std::size_t pos : node) {
        if (answeredYes(candidates[pos], bestFeature)) {
            yesNode.push_back(pos);
        } else {
            noNode.push_back(pos);
        }
    }

    rfGrow(candidates, labels, yesNode, depth + 1, features, mtry, importance, rng);
    rfGrow(candidates, labels, noNode, depth + 1, features, mtry, importance, rng);
}

// --- CatBoost-подобный бустинг --------------------------------------------
double sigmoid(double x) {
    if (x >= 0.0) {
        const double e = std::exp(-x);
        return 1.0 / (1.0 + e);
    }
    const double e = std::exp(x);
    return e / (1.0 + e);
}

void catGrow(const CandidateSet& candidates,
             const std::vector<double>& gradient,
             const std::vector<double>& hessian,
             const std::vector<std::size_t>& node,
             int depth,
             const std::vector<QuestionId>& features,
             double lambda,
             double learningRate,
             std::vector<double>& raw,
             std::vector<double>& importance) {
    constexpr int kMaxDepth = 2;

    double totalG = 0.0;
    double totalH = 0.0;
    for (std::size_t pos : node) {
        totalG += gradient[pos];
        totalH += hessian[pos];
    }

    if (node.size() < 2 || depth >= kMaxDepth || totalH <= 1e-12) {
        if (totalH > 1e-12) {
            const double leaf = -learningRate * totalG / (totalH + lambda);
            for (std::size_t pos : node) {
                raw[pos] += leaf;
            }
        }
        return;
    }

    const double base = totalG * totalG / (totalH + lambda);
    QuestionId bestFeature = -1;
    double bestGain = 0.0;

    for (QuestionId q : features) {
        double yesG = 0.0;
        double yesH = 0.0;
        std::size_t yes = 0;
        for (std::size_t pos : node) {
            if (answeredYes(candidates[pos], q)) {
                yesG += gradient[pos];
                yesH += hessian[pos];
                ++yes;
            }
        }
        if (yes == 0 || yes == node.size()) {
            continue;
        }
        const double noG = totalG - yesG;
        const double noH = totalH - yesH;
        const double gain = yesG * yesG / (yesH + lambda)
                          + noG * noG / (noH + lambda)
                          - base;
        if (gain > bestGain) {
            bestGain = gain;
            bestFeature = q;
        }
    }

    if (bestFeature < 0 || bestGain <= 1e-12) {
        if (totalH > 1e-12) {
            const double leaf = -learningRate * totalG / (totalH + lambda);
            for (std::size_t pos : node) {
                raw[pos] += leaf;
            }
        }
        return;
    }

    importance[static_cast<std::size_t>(bestFeature)] += bestGain;

    std::vector<std::size_t> yesNode;
    std::vector<std::size_t> noNode;
    for (std::size_t pos : node) {
        if (answeredYes(candidates[pos], bestFeature)) {
            yesNode.push_back(pos);
        } else {
            noNode.push_back(pos);
        }
    }
    catGrow(candidates, gradient, hessian, yesNode, depth + 1, features, lambda,
            learningRate, raw, importance);
    catGrow(candidates, gradient, hessian, noNode, depth + 1, features, lambda,
            learningRate, raw, importance);
}

} // namespace

const std::vector<std::string>& characterNames() { return generated::kCharacterNames; }

const std::vector<std::string>& questionTexts() { return generated::kQuestionTexts; }

const std::vector<std::int8_t>& matrix() { return generated::kMatrix; }

int value(CharacterId character, QuestionId question) {
    if (!validCharacter(character) || !validQuestion(question)) {
        return 0;
    }
    return static_cast<int>(generated::kMatrix[kCell(character, question)]);
}

CandidateSet filterCandidates(const CandidateSet& candidates, QuestionId question,
                              Answer answer) {
    const CandidateSet source = normalized(candidates);
    if (!validQuestion(question) || (answer != 1 && answer != -1)) {
        return source;
    }

    CandidateSet result;
    result.reserve(source.size());
    for (CharacterId character : source) {
        if (value(character, question) == answer) {
            result.push_back(character);
        }
    }
    return result;
}

HintResult hintVariance(const CandidateSet& candidates,
                        const std::vector<QuestionId>& excluded) {
    const CandidateSet remaining = normalized(candidates);
    if (remaining.size() < 2) {
        return {};
    }

    const double total = static_cast<double>(remaining.size());
    HintResult best;
    for (QuestionId q = 0; q < kQuestionCount; ++q) {
        if (isExcluded(excluded, q)) {
            continue;
        }
        std::size_t yes = 0;
        for (CharacterId character : remaining) {
            if (answeredYes(character, q)) {
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

HintResult hintRandomForest(const CandidateSet& candidates,
                            const std::vector<QuestionId>& excluded) {
    const CandidateSet remaining = normalized(candidates);
    if (remaining.size() < 2) {
        return {};
    }
    const std::vector<QuestionId> features = availableQuestions(excluded);
    if (features.empty()) {
        return {};
    }

    bool usable = false;
    const std::vector<int> labels = popularityLabels(remaining, &usable);
    if (!usable) {
        return hintVariance(remaining, excluded); // нет сигнала — базовая подсказка
    }

    std::vector<double> importance(kQuestionCount, 0.0);
    std::mt19937 rng(0x5EED1234u); // фиксированный seed → детерминизм
    constexpr int kTrees = 40;
    const std::size_t mtry = std::max<std::size_t>(2, features.size() / 3);
    std::uniform_int_distribution<std::size_t> pick(0, remaining.size() - 1);

    for (int tree = 0; tree < kTrees; ++tree) {
        // bootstrap-выборка наблюдений
        std::vector<std::size_t> sample;
        sample.reserve(remaining.size());
        for (std::size_t i = 0; i < remaining.size(); ++i) {
            sample.push_back(pick(rng));
        }
        rfGrow(remaining, labels, sample, 0, features, mtry, importance, rng);
    }

    const HintResult best = bestByImportance(importance, features);
    if (best.question < 0) {
        return hintVariance(remaining, excluded);
    }
    return best;
}

HintResult hintCatBoost(const CandidateSet& candidates,
                        const std::vector<QuestionId>& excluded) {
    const CandidateSet remaining = normalized(candidates);
    if (remaining.size() < 2) {
        return {};
    }
    const std::vector<QuestionId> features = availableQuestions(excluded);
    if (features.empty()) {
        return {};
    }

    bool usable = false;
    const std::vector<int> labels = popularityLabels(remaining, &usable);
    if (!usable) {
        return hintVariance(remaining, excluded);
    }

    const std::size_t n = remaining.size();
    int positives = 0;
    for (int label : labels) {
        positives += label;
    }
    const double baseRate = static_cast<double>(positives) / static_cast<double>(n);
    const double baseScore = std::log(baseRate / (1.0 - baseRate));

    constexpr int kRounds = 30;
    constexpr double kLearningRate = 0.25;
    constexpr double kLambda = 1.0;

    std::vector<double> raw(n, baseScore);
    std::vector<double> gradient(n, 0.0);
    std::vector<double> hessian(n, 0.0);
    std::vector<double> importance(kQuestionCount, 0.0);
    std::vector<std::size_t> node(n);
    std::iota(node.begin(), node.end(), std::size_t{0});

    for (int round = 0; round < kRounds; ++round) {
        for (std::size_t i = 0; i < n; ++i) {
            const double p = sigmoid(raw[i]);
            gradient[i] = p - static_cast<double>(labels[i]);
            hessian[i] = p * (1.0 - p);
        }
        catGrow(remaining, gradient, hessian, node, 0, features, kLambda,
                kLearningRate, raw, importance);
    }

    const HintResult best = bestByImportance(importance, features);
    if (best.question < 0) {
        return hintVariance(remaining, excluded);
    }
    return best;
}

} // namespace gamecore
