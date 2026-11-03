# ТЗ: Онлайн-игра «Бой Акинаторов» — задание для агента-разработчика

## 0. Как работать с этим документом (прочитай первым)

Ты — coding-агент. Реализуй проект целиком по этому документу.
**Не задавай уточняющих вопросов.** Если что-то неоднозначно — бери вариант,
указанный здесь, и продолжай работу.

- Создай проект в новой папке `akinator-battle/` рядом с этим файлом.
- Данные: скопируй `/home/Rvi/Big-Three-Akinator/selected_characters.csv`
  в `akinator-battle/data/selected_characters.csv`.
- Выполняй задачи строго по порядку из раздела 11 (Задача 1 → 2 → … → 11).
- После каждой задачи проект обязан собираться: `cmake --build build`.
  Тесты, где указаны, должны быть зелёными.
- После каждой задачи коротко отчитайся: что сделал, какие файлы создал/изменил,
  вывод команд проверки.
- Не делай «всё сразу» и не выходи за рамки текущей задачи.

---

## 1. Допущения и риски

**Допущения (не менять):**
- Игра — дуэль Акинаторов: 2 игрока, каждый тайно загадывает персонажа из 60
  отобранных (`data/selected_characters.csv`), по очереди задают вопросы да/нет,
  кто первым угадает персонажа противника — тот победил.
- Таблица: 60 персонажей × 139 бинарных вопросов; значения только `1` (да) и
  `-1` (нет), варианта «не знаю» нет. Источник истины — `gamecore` (данные вшиты).
- Сервер авторитетный: хранит секреты и наборы кандидатов обоих игроков, сам
  считает ответ на вопрос (по строке секрета противника) и сам фильтрует
  кандидатов (для обычного вопроса — по запросу `remove`, для подсказки —
  автоматически; см. примечание в разделе 7). Клиент только отображает.
  Обоснование: данные детерминированы —
  ответ всегда известен, нет рассинхрона и читерства.
- Подсказки (дисперсия / Random Forest / CatBoost) — три детерминированных
  in-memory алгоритма на чистом C++ внутри `gamecore` (без Python и libcatboost).
  Обоснование: таблица маленькая и фиксированная, ноль внешних ML-зависимостей,
  самодостаточная SHARED-библиотека.
- Протокол — newline-delimited JSON (NDJSON), разделитель `\n`, UTF-8.
- Персонажи и вопросы передаются по сети индексами (`characterId` 0..59,
  `questionId` 0..138); тексты/имена стороны берут локально из `gamecore`.
- Версии: Qt 6.5+, CMake ≥ 3.16, C++17.
- Публичный доступ к серверу (VPS/проброс порта) — вне проекта; никакого NAT
  traversal и P2P.
- Один матч одновременно; третий подключившийся отклоняется.

**Риски (и как закрываем):**
- In-memory RF/бустинг может быть капризным → фиксированные seed и глубина дают
  детерминизм (тесты раздела 10 проверяют детерминированность и валидность).
- Файрвол/NAT публичного сервера — не решается кодом (чек-лист раздела 12).
- Разрыв соединения — `opponent_disconnected` + сброс лобби (раздел 7).
- Windows не находит `gamecore.dll` — единая папка вывода бинарников (раздел 4).
- Русский текст — файлы в UTF-8, строки через `u8"..."`.

---

## 2. Архитектура и потоки данных

```
[Клиент A: Windows]                     [Сервер: Linux/VPS]                        [Клиент B: Linux/macOS]
 QtWidgets (GUI)                        QTcpServer (listen)                         QtWidgets (GUI)
 QTcpSocket ──TCP/NDJSON──►  accept ◄──  секреты + кандидаты обоих  ──►  accept ◄──TCP/NDJSON── QTcpSocket
 akinator_protocol (JSON)                  gamecore: matrix/filter/3 подсказки          akinator_protocol (JSON)
 gamecore (имена/вопросы/данные)           akinator_protocol (JSON)                     gamecore (имена/вопросы/данные)
```

**Роли целей:**
- `gamecore` (SHARED, чистый C++) — данные (имена/тексты/матрица), `filterCandidates`,
  три подсказки. Используют **сервер, клиент, тесты**.
- `akinator_protocol` (STATIC) — (де)сериализация NDJSON на `QJsonDocument`.
  Используют **сервер, клиент, тесты**.
- `akinator-server` (EXE) — авторитетный сервер.
- `akinator-client` (EXE) — GUI.
- `tst_gamecore`, `tst_protocol` (EXE) — unit-тесты.

**Поток данных (счастливый путь):**
1. Клиент читает env → предзаполняет host/port → `Connect`.
2. Оба клиента шлют `set_secret` (выбор персонажа из 60). Сервер хранит секреты.
3. Когда оба загадали → `start` + `turn` (ходит игрок 1).
4. Ход: игрок шлёт `question` (или `hint`). Сервер берёт значение вопроса у
   **секрета противника** → формирует ответ. Для `hint` сервер дополнительно
   фильтрует **кандидатов спрашивающего** (см. примечание в разделе 7), для
   обычного `question` набор не меняется → шлёт ему `answer` (+`hint` для
   подсказки), противнику `opponent_asked` (инфо) → `turn` второму.
5. Игрок может `get_candidates` / `remove` (просмотр и ручное удаление из своего
   списка) — без смены хода.
6. Игрок шлёт `guess`. Верно → `matchover` обоим; неверно →
   `guess_result(correct:false)` + смена хода.
7. Первый верно угадавший побеждает.
---

## 3. Дерево файлов проекта

```
akinator-battle/
├── CMakeLists.txt              # корневой: проект, C++17, find_package(Qt6), enable_testing(), единая папка bin/
├── README.md                   # описание, сборка, запуск, env, демо (структура — раздел 13)
├── .gitignore                  # build/, *.user, CMakeCache.txt
│
├── data/
│   └── selected_characters.csv # 60 персонажей × 140 колонок (Character + 139 вопросов) — источник данных
│
├── tools/
│   └── generate_data.py        # CSV → gamecore/generated/data.{h,cpp} (запускается вручную, результат закоммичен)
│
├── gamecore/                   # Динамическая библиотека (SHARED), без Qt
│   ├── CMakeLists.txt          # add_library(gamecore SHARED ...), GAMECORE_BUILD
│   ├── include/gamecore/
│   │   ├── export.h            # макрос GAMECORE_EXPORT (dllimport/dllexport / visibility)
│   │   └── gamecore.h          # типы, данные, filterCandidates, HintResult, 3 подсказки
│   ├── src/
│   │   └── gamecore.cpp        # filterCandidates + hintVariance/RandomForest/CatBoost
│   └── generated/
│       ├── data.h              # объявления kCharacterNames/kQuestionTexts/kMatrix (ГЕНЕРИРУЕТСЯ)
│       └── data.cpp            # данные: 60 имён, 139 текстов, матрица 60×139 (±1) (ГЕНЕРИРУЕТСЯ)
│
├── common/                     # Статическая библиотека протокола (STATIC), использует Qt::Core
│   ├── CMakeLists.txt          # add_library(akinator_protocol STATIC ...), link Qt6::Core
│   ├── protocol.h              # объявления функций (де)сериализации сообщений
│   └── protocol.cpp            # реализация на QJsonDocument
│
├── server/
│   ├── CMakeLists.txt          # add_executable(akinator-server ...), link Qt6::Core Qt6::Network akinator_protocol gamecore
│   ├── main.cpp                # QCoreApplication, чтение env, запуск Server
│   ├── server.h                # класс Server: QTcpServer, лобби, секреты, кандидаты, ходы
│   └── server.cpp              # accept, set_secret/question/hint/guess/remove, disconnect, рассылка
│
├── client/
│   ├── CMakeLists.txt          # add_executable(akinator-client ...), link Qt6::Widgets Qt6::Network akinator_protocol gamecore
│   ├── main.cpp                # QApplication, чтение env, создание MainWindow
│   ├── mainwindow.h            # GUI: выбор персонажа, вопросы, подсказки, таблица, угадать, статус
│   ├── mainwindow.cpp          # слоты кнопок, реакции на сообщения
│   ├── connection.h            # обёртка QTcpSocket + NDJSON-фрейминг
│   └── connection.cpp          # readyRead → накопление буфера → эмит строк по '\n'
│
└── tests/
    ├── CMakeLists.txt          # add_executable(tst_gamecore, tst_protocol), add_test(...)
    ├── tst_gamecore.cpp        # тесты данных/filter/подсказок (QtTest)
    └── tst_protocol.cpp        # тесты (де)сериализации протокола (QtTest)
```

---

## 4. Структура CMake

**Корневой `CMakeLists.txt`:**
- `cmake_minimum_required(VERSION 3.16)`
- `project(akinator LANGUAGES CXX)`
- `set(CMAKE_CXX_STANDARD 17)` + `set(CMAKE_CXX_STANDARD_REQUIRED ON)`
- `find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets Test)`
- **Единая папка вывода**: `set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)` (важно для Windows DLL).
- `enable_testing()`
- `add_subdirectory(gamecore)`, `add_subdirectory(common)`, `add_subdirectory(server)`, `add_subdirectory(client)`, `add_subdirectory(tests)`

**Qt-компоненты (обязательные):**

| Компонент | Кто использует | Зачем |
|---|---|---|
| `Core` | protocol, server, client, tests | `QJsonDocument`, `QObject`, `QCoreApplication` |
| `Network` | server, client | `QTcpServer`, `QTcpSocket` |
| `Widgets` | client | GUI |
| `Test` | tests | `QTest` |

**Цели и типы:**

| Цель | Тип | Линкует | Примечание |
|---|---|---|---|
| `gamecore` | `SHARED` | (ничего) | `target_compile_definitions(gamecore PRIVATE GAMECORE_BUILD)`; `target_include_directories(gamecore PUBLIC include)` |
| `akinator_protocol` | `STATIC` | `Qt6::Core` | `target_include_directories(akinator_protocol PUBLIC .)` |
| `akinator-server` | EXE | `Qt6::Core`, `Qt6::Network`, `akinator_protocol`, `gamecore` | консольное |
| `akinator-client` | EXE | `Qt6::Widgets`, `Qt6::Network`, `akinator_protocol`, `gamecore` | на MSVC можно `WIN32_EXECUTABLE TRUE` |
| `tst_gamecore` | EXE | `gamecore`, `Qt6::Test` | — |
| `tst_protocol` | EXE | `akinator_protocol`, `Qt6::Core`, `Qt6::Test` | — |

**Подключение тестов и CTest:**
- В `tests/CMakeLists.txt`: `find_package(Qt6 REQUIRED COMPONENTS Test)`.
- `add_test(NAME tst_gamecore COMMAND tst_gamecore)` и `add_test(NAME tst_protocol COMMAND tst_protocol)`.
- Запуск: `ctest --test-dir build --output-on-failure`.
- Тесты находят `gamecore.dll` благодаря общей папке `bin/`.
---

## 5. Публичный API `gamecore`

Файл `gamecore/include/gamecore/export.h` (экспорт символов):

```cpp
// export.h
#pragma once
#if defined(_WIN32)
  #if defined(GAMECORE_BUILD)
    #define GAMECORE_EXPORT __declspec(dllexport)
  #else
    #define GAMECORE_EXPORT __declspec(dllimport)
  #endif
#else
  #define GAMECORE_EXPORT __attribute__((visibility("default")))
#endif
```

Файл `gamecore/include/gamecore/gamecore.h` (только объявления):

```cpp
// gamecore.h
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
GAMECORE_EXPORT int value(CharacterId character, QuestionId question);
// Оставить только тех кандидатов, у кого value == answer.
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
```

**Контракты:**
- `value`, `filterCandidates`, подсказки **не бросают исключений**; входные
  индексы считаются валидными (валидация — на уровне сервера/протокола).
- `excluded` — уже заданные вопросы (исключаются из выбора; пустой вектор =
  ничего не исключено).
- Подсказки **детерминированы**: одинаковые входы → одинаковый `HintResult`.
- Если кандидатов < 2 или нет вопроса, который различает кандидатов и не входит
  в `excluded`, вернуть `{question = -1, score = 0}`.
---

## 6. Спецификация сетевого протокола (NDJSON)

Каждое сообщение — один JSON-объект в одной строке, завершён `\n`. Обязательное
поле — `type`.

### 6.1. Сообщения клиент → сервер

| `type` | Поля | Описание |
|---|---|---|
| `set_secret` | `characterId`: int (0..59) | Выбор секретного персонажа (один раз до старта) |
| `question` | `questionId`: int (0..138) | Задать вопрос о персонаже противника |
| `hint` | `kind`: `"variance"`\|`"randomforest"`\|`"catboost"` | Использовать подсказку |
| `get_candidates` | — | Запросить свой текущий список кандидатов |
| `remove` | `characterIds`: [int] | Удалить персонажей из своего списка вручную |
| `guess` | `characterId`: int | Угадать персонажа противника |
| `rematch` | — | Запрос новой партии (после `matchover`) |
| `leave` | — | Вежливое отключение |

Примеры:
```json
{"type":"set_secret","characterId":1}
{"type":"question","questionId":0}
{"type":"hint","kind":"variance"}
{"type":"get_candidates"}
{"type":"remove","characterIds":[3,17,42]}
{"type":"guess","characterId":24}
{"type":"rematch"}
```

### 6.2. Сообщения сервер → клиент

| `type` | Поля | Описание |
|---|---|---|
| `welcome` | `player`: 1\|2 | Ваш номер в лобби |
| `start` | `players`: [name1, name2] | Оба загадали, игра начинается |
| `turn` | `player`: 1\|2 | Чей сейчас ход |
| `answer` | `questionId`, `answer`: `"yes"`\|`"no"`, `remaining`: int, `hint`: null \| {`kind`, `metric`} | Итог вопроса/подсказки (спрашивающему) |
| `hint_status` | `hintsLeft`: {`variance`, `randomforest`, `catboost`} | Остаток подсказок (при старте и после подсказки) |
| `opponent_asked` | `questionId`, `answer` | Инфо противнику: что у него спросили |
| `candidates` | `characterIds`: [int], `count`: int | Текущий список кандидатов (ответ на `get_candidates`) |
| `remove_ok` | `remaining`: int | Подтверждение удаления |
| `guess_result` | `correct`: bool | Результат неверной догадки (true не шлётся — сразу `matchover`) |
| `matchover` | `winner`: 1\|2, `characterId`: int, `character`: string | Конец игры |
| `error` | `message`: string | Ошибка |
| `opponent_disconnected` | `message`: string | Противник отключился |

Примеры:
```json
{"type":"welcome","player":1}
{"type":"start","players":["Player 1","Player 2"]}
{"type":"turn","player":1}
{"type":"hint_status","hintsLeft":{"variance":1,"randomforest":1,"catboost":1}}
{"type":"answer","questionId":0,"answer":"yes","remaining":31,"hint":null}
{"type":"answer","questionId":12,"answer":"no","remaining":9,"hint":{"kind":"variance","metric":0.96}}
{"type":"opponent_asked","questionId":12,"answer":"no"}
{"type":"candidates","characterIds":[0,5,8,11],"count":4}
{"type":"remove_ok","remaining":4}
{"type":"guess_result","correct":false}
{"type":"matchover","winner":2,"characterId":24,"character":"Monkey D. Luffy"}
{"type":"error","message":"Сейчас не ваш ход"}
{"type":"opponent_disconnected","message":"Противник отключился"}
```

### 6.3. Конечный автомат

**Сервер (стейтмашина матча):**
```
WAITING_SECRETS ──(получены set_secret от обоих)──► IN_GAME (turn=1)
IN_GAME:
   question/hint ──► (ответ из секрета противника) ──► filterCandidates ──► answer(спрашивающему)
                     + opponent_asked(противнику) ──► switch turn ──► turn
   guess ──► верно? ──► MATCHOVER
             неверно? ──► guess_result(false) ──► switch turn
   get_candidates / remove ──► (без смены хода)
MATCHOVER ──(оба прислали rematch)──► WAITING_SECRETS (сброс секретов и кандидатов)
Любое состояние ──(disconnect любого игрока)──► opponent_disconnected другому ──► LOBBY (освободить оба слота)
```
- `question`/`hint`/`guess` вне своего хода → `error` «Сейчас не ваш ход».
- `question` с уже заданным вопросом или `hint` с израсходованным видом → `error`.
- `set_secret` повторно → `error`.
- `guess` с несуществующим `characterId` или персонажем не из кандидатов → `error`.

**Клиент:**
```
DISCONNECTED ──Connect──► CONNECTING ──(welcome)──► WAITING_OPPONENT(лобби)
WAITING_OPPONENT ──(start + turn)──► MY_TURN | OPPONENT_TURN
MY_TURN: доступны «вопрос/подсказка/таблица/угадать» ──(question/hint)──► WAIT_RESULT
WAIT_RESULT ──(answer)──► (matchover?) иначе OPPONENT_TURN
OPPONENT_TURN: интерфейс хода заблокирован; оповещается через opponent_asked
Любое состояние ──(matchover)──► MATCHOVER
Любое состояние ──(opponent_disconnected | error)──► показать сообщение ──► DISCONNECTED/WAITING
```
---

## 7. Поведение сервера

- **Запуск:** читает env (раздел 9), создаёт `QTcpServer`, `listen(QHostAddress::Any, port)`, лог в stdout.
- **Лобби на 2 игрока:** 2 слота `QTcpSocket*`. При подключении: слот 1 →
  `welcome(player=1)`; слот 2 → `welcome(player=2)`; оба заняты →
  `error("Сервер занят")` + `disconnectFromHost()` (третий отклоняется).
- **Секреты:** хранит `secret[player]` (CharacterId или «не задан»). `set_secret`
  принимает только в WAITING_SECRETS; когда оба заданы → `start` + `turn(1)` +
  `hint_status` обоим.
- **Кандидаты:** хранит `candidates[player]` (изначально все 60). После каждого
  `question`/`hint` игрока сервер берёт `value(secret[opponent], questionId)`,
  формирует `answer` ("yes"/"no"), вызывает
  `gamecore::filterCandidates(candidates[player], questionId, answer)` и обновляет
  счётчик `remaining`.
  > **Принятое в реализации поведение (согласовано с заказчиком):** авторитетно
  > фильтрует только `hint` — подсказка сама выбирает вопрос по текущему набору
  > кандидатов, поэтому после неё сервер применяет `filterCandidates` и присылает
  > уже сокращённый `remaining`. Обычный `question` `answer` возвращает, но набор
  > кандидатов **не** меняет: `remaining` равен прежнему размеру, а игрок сам
  > сокращает таблицу через `get_candidates` + `remove`. Причина: ручная работа с
  > таблицей — часть игрового процесса, иначе список схлопывается автоматически и
  > кнопка «Удалить выбранных» становится бесполезной.
- **Ответ — авторитетный:** значение берётся из таблицы секрета противника
  (детерминировано), противник вручную не отвечает; противник получает
  `opponent_asked` (инфо-лог).
- **Подсказки:** у каждого игрока `hintsLeft = {variance:1, randomforest:1, catboost:1}`.
  `hint(kind)` → если остаток 0 → `error`; иначе вызвать соответствующую
  `gamecore::hint*` на `candidates[player]` с `excluded` = уже заданным вопросам.
  Если `question == -1` → `error("Нет информативного вопроса")` (подсказка не
  тратится). Иначе вычесть подсказку, применить вопрос как обычный (ответ+фильтр),
  вернуть `answer` с `hint:{kind,metric}` + `hint_status`.
- **Угадывание:** `guess(characterId)` → сравнить с `secret[opponent]`. Верно →
  `matchover(winner, characterId, character)` обоим. Неверно → `guess_result(false)`
  угадавшему + смена хода.
- **Валидация:** `questionId` вне 0..138, `characterId` вне 0..59, не тот ход,
  повторный вопрос → `error`, соединение не рвать.
- **Отключения:** `disconnected` в любом состоянии → другому
  `opponent_disconnected`, закрыть оба сокета, вернуть лобби к исходному
  (оба слота свободны, секреты/кандидаты сброшены).
- **Повтор (rematch):** в MATCHOVER запомнить, кто прислал `rematch`; когда оба —
  сбросить секреты/кандидаты/подсказки и перейти в WAITING_SECRETS (перезагадывание).

---

## 8. Поведение GUI (клиент)

**Экран «Подключение»:**
- `QLineEdit hostField` (из env, раздел 9), `QSpinBox portField` (1–65535, из env),
  `QPushButton connectButton`.

**Экран «Выбор персонажа» (после `welcome`, состояние WAITING_OPPONENT):**
- `QLineEdit searchField` + `QListWidget characterList` (60 имён из
  `gamecore::characterNames()`, с фильтром по строке поиска).
- `QPushButton confirmSecretButton` — шлёт `set_secret`. После выбора кнопка
  блокируется, статус «Ожидание второго игрока…».

**Экран «Игра» (после `start`):**
- `QLabel statusLabel` — «Ваш ход» / «Ход соперника» / «Ожидание ответа…».
- `QLabel candidatesLabel` — «Кандидатов: N».
- `QLabel hintsLabel` — остаток подсказок (или отдельные кнопки с цифрами).
- `QPlainTextEdit logText` (read-only) — история: «Вы спросили … → Да (осталось
  31)», «Соперник спросил … → Нет».
- Кнопки действия (активны только в MY_TURN):
  - `QPushButton askButton` («Задать вопрос») → диалог со списком 139 вопросов
    (`gamecore::questionTexts()`) + поиск; исключает уже заданные.
  - `QPushButton hintVarianceButton`, `hintCatBoostButton`, `hintRandomForestButton`
    («Подсказка: …») — блокируются при остатке 0.
  - `QPushButton tableButton` («Открыть таблицу») → диалог со списком текущих
    кандидатов и кнопкой «Удалить выбранных» (шлёт `remove`).
  - `QPushButton guessButton` («Угадать») → диалог-выбор из текущих кандидатов.

**Состояния и блокировки:**

| Состояние | host/port/connect | Выбор персонажа | Кнопки хода | Таблица/Угадать |
|---|---|---|---|---|
| DISCONNECTED | активны | скрыт | скрыты | скрыты |
| CONNECTING | заблокированы | скрыт | скрыты | скрыты |
| WAITING_OPPONENT | заблокированы | активен | скрыты | скрыты |
| MY_TURN | заблокированы | скрыт | активны | активны |
| WAIT_RESULT | заблокированы | скрыт | заблокированы | заблокированы |
| OPPONENT_TURN | заблокированы | скрыт | заблокированы | скрыты |
| MATCHOVER | заблокированы | скрыт | скрыты | скрыты |

- `answer` → обновить `logText`, `candidatesLabel`; если `answer.hint` не null —
  обновить `hintsLabel`.
- `matchover` → показать победителя и загаданного персонажа; кнопка «Реванш»
  (шлёт `rematch`) и «Выход».
- `opponent_disconnected` / `error` → `statusLabel`, разблокировать экран подключения.

---

## 9. Переменные окружения

| Переменная | Кто читает | Дефолт | Назначение |
|---|---|---|---|
| `GAME_SERVER_HOST` | клиент | `127.0.0.1` | адрес сервера (предзаполняет поле host) |
| `GAME_SERVER_PORT` | клиент и сервер | `9999` | порт (клиент — подключение, сервер — listen) |

- **Сервер** слушает на `0.0.0.0` (Any) и порту из `GAME_SERVER_PORT`.
- **Клиент** использует значения полей GUI в момент «Подключиться»; поля
  предзаполняются из env (или дефолтом).
- **Некорректный порт** (не число / вне `[1,65535]`) → дефолт + предупреждение в
  stderr (сервер) / `statusLabel` (клиент).
- **Пустой host** → дефолт `127.0.0.1`.
- Приоритет (клиент): **значение в поле GUI** > env > дефолт.
---

## 10. Unit-тесты

### `tst_gamecore.cpp`

| # | Тест | Вход | Ожидаемый результат |
|---|---|---|---|
| 1 | `data_integrity` | обращение к данным | `characterNames().size()==60`, `questionTexts().size()==139`, `matrix().size()==60*139`, все значения ∈ {−1,+1} |
| 2 | `value_roundtrip` | `value(c,q)` для нескольких (c,q) | равно `matrix()[c*139+q]` |
| 3 | `filter_yes` | `filterCandidates(all60, q, +1)` | результат = вручную вычисленное по `matrix()` подмножество с `value==+1` |
| 4 | `filter_no` | `filterCandidates(all60, q, -1)` | подмножество с `value==-1` |
| 5 | `filter_empty` | пустой `candidates` | пустой результат |
| 6 | `filter_preservesOrder` | несортированный вход | результат отсортирован, без повторов |
| 7 | `variance_max` | `hintVariance(all60, {})` | `question` == вопросу с максимальным `4p(1-p)` (brute-force сравнение), `score>0` |
| 8 | `variance_excluded` | `hintVariance(c, excluded={best})` | возвращённый вопрос ∉ `excluded` |
| 9 | `variance_noInfo` | кандидаты с одинаковыми строками | `question==-1`, `score==0` |
| 10 | `rf_valid` | `hintRandomForest(all60, {})` | `0 <= question < 139`, `score > 0` |
| 11 | `rf_deterministic` | два вызова с одинаковым входом | идентичные `HintResult` |
| 12 | `catboost_valid` | `hintCatBoost(all60, {})` | `0 <= question < 139`, `score > 0` |
| 13 | `catboost_deterministic` | два вызова с одинаковым входом | идентичные `HintResult` |
| 14 | `hints_noInfo` | кандидаты с одинаковыми строками | все три подсказки возвращают `question==-1` |

### `tst_protocol.cpp`

| # | Тест | Вход | Ожидаемый результат |
|---|---|---|---|
| 15 | `parse_set_secret` | `{"type":"set_secret","characterId":7}` | тип `set_secret`, `characterId==7` |
| 16 | `parse_question` | `{"type":"question","questionId":12}` | тип `question`, `questionId==12` |
| 17 | `parse_hint` | `{"type":"hint","kind":"catboost"}` | тип `hint`, `kind==catboost` |
| 18 | `parse_hint_invalid` | `{"type":"hint","kind":"bogus"}` | корректная ошибка, не падает |
| 19 | `parse_guess` | `{"type":"guess","characterId":24}` | тип `guess`, `characterId==24` |
| 20 | `serialize_answer` | собрать `answer` (id, "yes", 31, null) | JSON содержит все поля с верными значениями |
| 21 | `serialize_matchover` | собрать `matchover` (2, 24, "Monkey D. Luffy") | JSON содержит все поля |
| 22 | `framing_two` | строка с двумя JSON через `\n` | 2 отдельных сообщения в правильном порядке |
| 23 | `parse_unknown_type` | `{"type":"bogus"}` | ошибка «неизвестный тип», не падает |
---

## 11. Пошаговый план (11 задач)

После каждой задачи проект собирается и не ломает предыдущее.

**Задача 1. Каркас и CMake**
- Цель: пустой проект собирается целиком.
- Файлы: корневой `CMakeLists.txt`, `.gitignore`, пустые `gamecore/`, `common/`, `server/`, `client/`, `tests/` со своими `CMakeLists.txt` и `main.cpp` (печатают и выходят).
- Готово, если: `cmake -S . -B build` и `cmake --build build` без ошибок; exe в `build/bin`.
- Проверка: `cmake -S . -B build && cmake --build build`.

**Задача 2. Данные gamecore (генератор + доступ)**
- Цель: вшитые данные 60×139 и функции доступа к ним.
- Файлы: `tools/generate_data.py`, `gamecore/generated/data.h`, `gamecore/generated/data.cpp`, `gamecore/include/gamecore/export.h`, `gamecore/gamecore.h`, `gamecore/src/gamecore.cpp` (частично), правка `gamecore/CMakeLists.txt`.
- Готово, если: скрипт читает `data/selected_characters.csv` и генерирует `data.{h,cpp}` (имена/тексты/матрица); библиотека собирается и экспортирует `characterNames/questionTexts/matrix/value`.
- Проверка: `python3 tools/generate_data.py` (без ошибок, файлы созданы) → `cmake --build build`.

**Задача 3. gamecore: filterCandidates + hintVariance**
- Цель: базовая фильтрация и подсказка по дисперсии.
- Файлы: `gamecore/src/gamecore.cpp`, `gamecore/gamecore.h`.
- Готово, если: `filterCandidates` и `hintVariance` работают по контракту раздела 5.
- Проверка: `cmake --build build`.

**Задача 4. gamecore: hintRandomForest + hintCatBoost**
- Цель: две ML-подсказки (in-memory, детерминированные).
- Файлы: `gamecore/src/gamecore.cpp`, `gamecore/gamecore.h`.
- Готово, если: обе функции возвращают валидный вопрос / `-1`, детерминированы.
- Проверка: `cmake --build build`.

**Задача 5. Тесты gamecore + ctest**
- Цель: покрыть данные/filter/подсказки тестами 1–14.
- Файлы: `tests/tst_gamecore.cpp`, правка `tests/CMakeLists.txt`.
- Готово, если: `ctest` зелёный.
- Проверка: `cmake --build build && ctest --test-dir build --output-on-failure`.

**Задача 6. akinator_protocol (STATIC) + тесты**
- Цель: NDJSON (де)сериализация всех сообщений раздела 6.
- Файлы: `common/protocol.h`, `common/protocol.cpp`, правка `common/CMakeLists.txt`, `tests/tst_protocol.cpp`, правка `tests/CMakeLists.txt`.
- Готово, если: тесты 15–23 зелёные.
- Проверка: `cmake --build build && ctest --test-dir build --output-on-failure`.

**Задача 7. Сервер: listen + env + лобби + set_secret**
- Цель: принимать 2 клиентов, собирать секреты, отклонять третьего.
- Файлы: `server/server.h`, `server/server.cpp`, `server/main.cpp`, правка `server/CMakeLists.txt`.
- Готово, если: сервер слушает порт из `GAME_SERVER_PORT`; два клиента получают `welcome`, после двух `set_secret` — `start`; третий получает `error`.
- Проверка: запуск сервера + ручная проверка `welcome/start/error` через `nc`/telnet (слать NDJSON).

**Задача 8. Сервер: вопросы/подсказки/кандидаты/угадывание/победа + отключения**
- Цель: полный игровой цикл по разделу 7.
- Файлы: `server/server.cpp`, `server/server.h`.
- Готово, если: `question`/`hint` → `answer`+`opponent_asked`+смена хода; `guess` → `guess_result`/`matchover`; `get_candidates`/`remove`; отключение → `opponent_disconnected`; `rematch` → сброс.
- Проверка: ручной тест двумя `nc`-сессиями по протоколу раздела 6 (полный матч до угадывания).

**Задача 9. Клиент: GUI + env + выбор персонажа**
- Цель: окна подключения и выбора секрета; чтение env.
- Файлы: `client/main.cpp`, `client/mainwindow.h/.cpp`, `client/connection.h/.cpp`, правка `client/CMakeLists.txt`.
- Готово, если: окно открывается, host/port из env, список 60 персонажей с поиском, `set_secret` отправляется.
- Проверка: `cmake --build build`, запуск `akinator-client` против запущенного сервера.

**Задача 10. Клиент: полный игровой цикл**
- Цель: вопросы/подсказки/таблица/угадать/матч по разделу 8.
- Файлы: `client/mainwindow.cpp`, `client/connection.cpp`.
- Готово, если: два GUI-клиента играют полный матч на localhost; состояния и блокировки как в разделе 8.
- Проверка: сервер + 2 клиента локально.

**Задача 11. Интеграция, README, финальный прогон**
- Цель: документация и сквозная проверка.
- Файлы: `README.md`, финальные правки.
- Готово, если: полный цикл «сборка → ctest → сервер → 2 клиента» по README; чек-лист раздела 12 выполним.
- Проверка: `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure` + локальный матч.
---

## 12. Чек-лист демонстрации

1. `cmake -S . -B build && cmake --build build` — собирается на **Windows** и **Linux/macOS**.
2. `ctest --test-dir build --output-on-failure` — все тесты зелёные (показать вывод).
3. Показать, что `gamecore` — **SHARED**-библиотека (`.dll`/`.so`), реально используемая сервером/клиентом/тестами (там живут `filterCandidates` и 3 подсказки).
4. Показать `tools/generate_data.py` → генерирует данные из `selected_characters.csv`.
5. Запустить **сервер**: `GAME_SERVER_PORT=9999 ./akinator-server` (лог listen).
6. Порт доступен извне (фаервол/проброс).
7. **Windows**: `GAME_SERVER_HOST=<публичный_IP> GAME_SERVER_PORT=9999 akinator-client`.
8. **Linux/macOS**: второй клиент с тем же адресом.
9. Оба выбирают персонажей из 60 → `start` → ход 1.
10. Продемонстрировать: обычный вопрос → `answer` (таблица не изменилась, `remaining` прежний); «Открыть таблицу» + ручное удаление → `remove_ok` с новым `remaining`; **3 подсказки** (дисперсия, Random Forest, CatBoost) → лучший вопрос + автофильтрация; угадывание → `matchover`.
11. Показать **env**: без переменных — дефолты; с `GAME_SERVER_HOST/PORT` — подключение к заданному адресу.
12. (Опционально) разрыв: закрыть клиент → второй видит «Противник отключился».
13. Показать отказ третьему игроку (`error` «Сервер занят»).

---

## 13. Структура README

1. **Название и суть** — «Бой Акинаторов», 2 игрока, авторитетный сервер + 2 GUI-клиента.
2. **Требования** — Qt 6.5+, CMake ≥ 3.16, компилятор C++17.
3. **Сборка** — `cmake -S . -B build`, `cmake --build build`.
4. **Генерация данных** — `python3 tools/generate_data.py` (результат уже закоммичен).
5. **Тесты** — `ctest --test-dir build --output-on-failure`.
6. **Запуск сервера** — команда + env `GAME_SERVER_PORT`.
7. **Запуск клиента** — команда + env `GAME_SERVER_HOST`/`GAME_SERVER_PORT` + поля GUI.
8. **Как играть** — правила дуэли, вопросы, 3 подсказки, таблица кандидатов, угадывание.
9. **Сетевой доступ** — VPS/проброс порта, что P2P не используется.
10. **Архитектура** — модули (`gamecore`, `akinator_protocol`, `server`, `client`, `tests`).
11. **Протокол** — ссылка на раздел 6.
12. **Демо-чек-лист** — копия раздела 12.
13. **Авторы** (до 2 человек).

---

## 14. Чего делать НЕЛЬЗЯ

- **Не менять архитектуру** (авторитетный TCP-сервер + 2 GUI-клиента, не P2P).
- **Не добавлять** БД, файловое хранилище, auth, статистику.
- **Не реализовывать** NAT traversal, UPnP, STUN/TURN, peer-to-peer, relay.
- **Не подключать** сторонние библиотеки (Boost, nlohmann/json, libcatboost, сокеты не из Qt) — только Qt6 + STL.
- **Не добавлять** лишние Qt-модули (QML, Multimedia, WebEngine) — только Core/Network/Widgets/Test.
- **Не использовать** Python в рантайме (только офлайн-генератор `tools/generate_data.py`).
- **Не менять** формат данных (`1`=да, `-1`=нет) и количество персонажей/вопросов (60/139).
- **Не менять** публичный API `gamecore` (имена, сигнатуры, семантику `HintResult`).
- **Не менять** имена env-переменных и дефолты.
- **Не разрешать** клиенту считать ответ/победителя — только сервер.
- **Не поддерживать** больше 2 игроков и несколько одновременных матчей.
- **Не генерировать** весь проект одной задачей — строго по разделу 11.
- **Не удалять/игнорировать** тесты и `enable_testing()`.

---

## 15. Итоговая инструкция по выполнению

Работай по разделу 11 строго последовательно. Начинай с **Задачи 1** сразу, без
дополнительных вопросов. Когда все 11 задач выполнены, ещё раз прогони:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

и убедись, что README.md содержит все пункты раздела 13. На этом работа завершена.
