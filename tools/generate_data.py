#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Генератор вшитых данных gamecore.

Читает data/selected_characters.csv (60 персонажей x (1 + 139) колонок)
и пишет gamecore/generated/data.h и gamecore/generated/data.cpp.

Запуск (из корня akinator-battle):
    python3 tools/generate_data.py

Результат закоммичен в репозиторий; в рантайме Python не используется.
"""

from __future__ import annotations

import csv
import pathlib
import sys

CHARACTER_COUNT = 60
QUESTION_COUNT = 139

ROOT = pathlib.Path(__file__).resolve().parent.parent
CSV_PATH = ROOT / "data" / "selected_characters.csv"
OUT_DIR = ROOT / "gamecore" / "generated"
OUT_H = OUT_DIR / "data.h"
OUT_CPP = OUT_DIR / "data.cpp"


def fail(message: str) -> None:
    print(f"ОШИБКА: {message}", file=sys.stderr)
    sys.exit(1)


def cpp_string(text: str) -> str:
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    escaped = escaped.replace("\r", " ").replace("\n", " ")
    return f'u8"{escaped}"'


def load_csv() -> tuple[list[str], list[str], list[list[int]]]:
    if not CSV_PATH.is_file():
        fail(f"не найден файл данных {CSV_PATH}")

    with CSV_PATH.open("r", encoding="utf-8-sig", newline="") as handle:
        rows = list(csv.reader(handle))

    if len(rows) < 2:
        fail("CSV пуст")

    header = rows[0]
    if len(header) != QUESTION_COUNT + 1:
        fail(f"ожидалось {QUESTION_COUNT + 1} колонок, получено {len(header)}")

    names: list[str] = []
    texts: list[str] = [cell.strip() for cell in header[1:]]
    matrix: list[list[int]] = []

    for row in rows[1:]:
        if not row or not row[0].strip():
            continue
        if len(row) != QUESTION_COUNT + 1:
            fail(f"строка персонажа «{row[0]}» содержит {len(row)} колонок")
        names.append(row[0].strip())
        values: list[int] = []
        for cell in row[1:]:
            raw = cell.strip()
            if raw not in ("1", "-1"):
                fail(f"недопустимое значение «{raw}» у персонажа «{row[0]}»")
            values.append(int(raw))
        matrix.append(values)

    if len(names) != CHARACTER_COUNT:
        fail(f"ожидалось {CHARACTER_COUNT} персонажей, получено {len(names)}")
    for text in texts:
        if not text:
            fail("найден пустой текст вопроса")

    return names, texts, matrix


def write_header() -> None:
    content = f"""// ВНИМАНИЕ: файл сгенерирован tools/generate_data.py — не редактировать вручную.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gamecore {{
namespace generated {{

// {CHARACTER_COUNT} имён персонажей (UTF-8).
extern const std::vector<std::string> kCharacterNames;
// {QUESTION_COUNT} текстов вопросов (UTF-8).
extern const std::vector<std::string> kQuestionTexts;
// Матрица row-major {CHARACTER_COUNT}x{QUESTION_COUNT}, значения -1/+1.
extern const std::vector<std::int8_t> kMatrix;

}} // namespace generated
}} // namespace gamecore
"""
    OUT_H.write_text(content, encoding="utf-8")


def write_source(names: list[str], texts: list[str], matrix: list[list[int]]) -> None:
    parts: list[str] = []
    parts.append("// ВНИМАНИЕ: файл сгенерирован tools/generate_data.py — не редактировать вручную.\n")
    parts.append('#include "data.h"\n\n')
    parts.append("namespace gamecore {\nnamespace generated {\n\n")

    parts.append("const std::vector<std::string> kCharacterNames = {\n")
    for name in names:
        parts.append(f"    {cpp_string(name)},\n")
    parts.append("};\n\n")

    parts.append("const std::vector<std::string> kQuestionTexts = {\n")
    for text in texts:
        parts.append(f"    {cpp_string(text)},\n")
    parts.append("};\n\n")

    parts.append("const std::vector<std::int8_t> kMatrix = {\n")
    for row in matrix:
        cells = ",".join(str(value) for value in row)
        parts.append(f"    {cells},\n")
    parts.append("};\n\n")

    parts.append("} // namespace generated\n} // namespace gamecore\n")
    OUT_CPP.write_text("".join(parts), encoding="utf-8")


def main() -> None:
    names, texts, matrix = load_csv()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    write_header()
    write_source(names, texts, matrix)
    print(
        f"OK: {len(names)} персонажей, {len(texts)} вопросов, "
        f"{len(names) * len(texts)} значений матрицы"
    )
    print(f"  -> {OUT_H.relative_to(ROOT)}")
    print(f"  -> {OUT_CPP.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
