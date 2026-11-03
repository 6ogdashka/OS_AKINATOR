// gamecore.h — публичный API игрового ядра «Боя Акинаторов».
// Чистый C++17, без Qt. Данные вшиты и генерируются tools/generate_data.py.
#pragma once

#include "gamecore/export.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gamecore {

using CharacterId = int;   // 0..59
using QuestionId  = int;   // 0..138
using Answer      = int;   // -1 = нет, +1 = да
using CandidateSet = std::vector<CharacterId>; // отсортирован, без повторов

constexpr int kCharacterCount = 60;
constexpr int kQuestionCount  = 139;

// --- Данные (генерируются из selected_characters.csv) ---
GAMECORE_EXPORT const std::vector<std::string>& characterNames(); // 60 имён, UTF-8
GAMECORE_EXPORT const std::vector<std::string>& questionTexts();  // 139 текстов, UTF-8
// Матрица row-major: matrix[charId * kQuestionCount + questionId] ∈ {-1, +1}
GAMECORE_EXPORT const std::vector<std::int8_t>& matrix();

// --- Элементарные операции ---
// Значение ответа персонажа на вопрос: +1 «да», -1 «нет», 0 при недопустимых id.
GAMECORE_EXPORT int value(CharacterId character, QuestionId question);
// Оставить только тех кандидатов, у кого value == answer.
// Недопустимые id в candidates отбрасываются; недопустимый вопрос/ответ
// возвращает нормализованный исходный набор без фильтрации.
GAMECORE_EXPORT CandidateSet filterCandidates(const CandidateSet& candidates,
                                              QuestionId question, Answer answer);

// --- Подсказки (лучший вопрос) ---
struct HintResult {
    QuestionId question = -1; // -1, если информативного вопроса нет
    double     score    = 0.0;
};

// Дисперсия: максимум 4p(1-p) по оставшимся кандидатам.
GAMECORE_EXPORT HintResult hintVariance(const CandidateSet& candidates,
                                        const std::vector<QuestionId>& excluded);
// Random Forest: важность по усреднённому падению Gini-примеси
// (ансамбль малых случайных деревьев, фиксированный seed).
GAMECORE_EXPORT HintResult hintRandomForest(const CandidateSet& candidates,
                                            const std::vector<QuestionId>& excluded);
// CatBoost-подобный жадный градиентный бустинг: суммарный gain
// (shallow-деревья, квадратичная аппроксимация logloss, фиксированный seed).
GAMECORE_EXPORT HintResult hintCatBoost(const CandidateSet& candidates,
                                        const std::vector<QuestionId>& excluded);

} // namespace gamecore
