// ВНИМАНИЕ: файл сгенерирован tools/generate_data.py — не редактировать вручную.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gamecore {
namespace generated {

// 60 имён персонажей (UTF-8).
extern const std::vector<std::string> kCharacterNames;
// 139 текстов вопросов (UTF-8).
extern const std::vector<std::string> kQuestionTexts;
// Матрица row-major 60x139, значения -1/+1.
extern const std::vector<std::int8_t> kMatrix;

} // namespace generated
} // namespace gamecore
