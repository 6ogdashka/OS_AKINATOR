// protocol.h — NDJSON-протокол «Боя Акинаторов».
// Каждое сообщение — один JSON-объект в одной строке, завершённый '\n' (UTF-8).
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <vector>

namespace proto {

// Пределы протокола: обязаны совпадать с gamecore::kCharacterCount/kQuestionCount
// и числом игроков в лобби (проверяется static_assert'ами в server.cpp).
constexpr int kCharacterCount = 60;
constexpr int kQuestionCount = 139;
constexpr int kPlayerCount = 2;

// Максимальный размер незавершённой NDJSON-строки в буфере чтения.
// Защита от «забытого» '\n': иначе буфер растёт неограниченно.
constexpr int kMaxLineBytes = 64 * 1024;

// Виды подсказок (совпадают с gamecore::hint*).
enum class HintKind { Variance, RandomForest, CatBoost };

// Все типы сообщений протокола (раздел 6 ТЗ).
enum class Type {
    // клиент -> сервер
    SetSecret,
    Question,
    Hint,
    GetCandidates,
    Remove,
    Guess,
    Rematch,
    Leave,
    // сервер -> клиент
    Welcome,
    Start,
    Turn,
    Answer,
    HintStatus,
    OpponentAsked,
    Candidates,
    RemoveOk,
    GuessResult,
    MatchOver,
    Error,
    OpponentDisconnected
};

QString hintKindToString(HintKind kind);
bool hintKindFromString(const QString& text, HintKind* out);

QString typeToString(Type type);
bool typeFromString(const QString& text, Type* out);

// Разобранное сообщение: тип + типизированные поля + исходный JSON.
struct Message {
    Type type = Type::Error;
    QJsonObject raw;

    int characterId = -1;
    int questionId = -1;
    int player = 0;
    int winner = 0;
    int remaining = -1;
    int count = -1;
    bool correct = false;

    QString answer;    // "yes" / "no"
    QString character;  // имя персонажа в matchover
    QString text;       // текст ошибки / opponent_disconnected
    QString hintKindText;

    HintKind hintKind = HintKind::Variance;
    bool hasHint = false;

    QStringList players;
    QJsonObject hintsLeft;
    std::vector<int> characterIds;
};

// Разбор одной строки. false + текст ошибки в error при некорректном вводе.
bool parseMessage(const QByteArray& line, Message* out, QString* error);

// Нарезка NDJSON-буфера: возвращает готовые строки, остаток остаётся в buffer.
QStringList splitFramedLines(QByteArray* buffer);

// --- Сборка сообщений сервер -> клиент ---
QJsonObject makeWelcome(int player);
QJsonObject makeStart(const QStringList& players);
QJsonObject makeTurn(int player);
QJsonObject makeAnswer(int questionId, const QString& answer, int remaining,
                       const QJsonObject& hint = QJsonObject());
QJsonObject makeHintInfo(const QString& kind, double metric);
QJsonObject makeHintStatus(int variance, int randomForest, int catBoost);
QJsonObject makeOpponentAsked(int questionId, const QString& answer);
QJsonObject makeCandidates(const std::vector<int>& characterIds);
QJsonObject makeRemoveOk(int remaining);
QJsonObject makeGuessResult(bool correct);
QJsonObject makeMatchOver(int winner, int characterId, const QString& character);
QJsonObject makeError(const QString& message);
QJsonObject makeOpponentDisconnected(const QString& message);

// --- Сборка сообщений клиент -> сервер ---
QJsonObject makeSetSecret(int characterId);
QJsonObject makeQuestion(int questionId);
QJsonObject makeHint(HintKind kind);
QJsonObject makeGetCandidates();
QJsonObject makeRemove(const std::vector<int>& characterIds);
QJsonObject makeGuess(int characterId);
QJsonObject makeRematch();
QJsonObject makeLeave();

// JSON-объект -> одна строка NDJSON (компактный JSON + '\n').
QByteArray toLine(const QJsonObject& object);

} // namespace proto
