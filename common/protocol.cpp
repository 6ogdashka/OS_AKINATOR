// protocol.cpp — реализация NDJSON-протокола на QJsonDocument.
#include "protocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QList>
#include <QPair>

#include <cmath>

namespace proto {
namespace {

bool fail(QString* error, const QString& text) {
    if (error) {
        *error = text;
    }
    return false;
}

const QList<QPair<Type, QString>>& typeTable() {
    static const QList<QPair<Type, QString>> table = {
        {Type::SetSecret, QStringLiteral("set_secret")},
        {Type::Question, QStringLiteral("question")},
        {Type::Hint, QStringLiteral("hint")},
        {Type::GetCandidates, QStringLiteral("get_candidates")},
        {Type::Remove, QStringLiteral("remove")},
        {Type::Guess, QStringLiteral("guess")},
        {Type::Rematch, QStringLiteral("rematch")},
        {Type::Leave, QStringLiteral("leave")},
        {Type::Welcome, QStringLiteral("welcome")},
        {Type::Start, QStringLiteral("start")},
        {Type::Turn, QStringLiteral("turn")},
        {Type::Answer, QStringLiteral("answer")},
        {Type::HintStatus, QStringLiteral("hint_status")},
        {Type::OpponentAsked, QStringLiteral("opponent_asked")},
        {Type::Candidates, QStringLiteral("candidates")},
        {Type::RemoveOk, QStringLiteral("remove_ok")},
        {Type::GuessResult, QStringLiteral("guess_result")},
        {Type::MatchOver, QStringLiteral("matchover")},
        {Type::Error, QStringLiteral("error")},
        {Type::OpponentDisconnected, QStringLiteral("opponent_disconnected")},
    };
    return table;
}

bool readInt(const QJsonObject& object, const QString& key, int minValue, int maxValue,
             int* out, QString* error) {
    const QJsonValue value = object.value(key);
    if (!value.isDouble()) {
        return fail(error, QStringLiteral("Поле \"%1\" должно быть числом").arg(key));
    }
    const double raw = value.toDouble();
    if (std::floor(raw) != raw) {
        return fail(error, QStringLiteral("Поле \"%1\" должно быть целым числом").arg(key));
    }
    // Сравнение в double: приведение к int вне диапазона — UB (например, 1e18).
    if (!(raw >= static_cast<double>(minValue) && raw <= static_cast<double>(maxValue))) {
        return fail(error, QStringLiteral("Поле \"%1\" вне диапазона %2..%3")
                               .arg(key).arg(minValue).arg(maxValue));
    }
    if (out) {
        *out = static_cast<int>(raw);
    }
    return true;
}

bool readAnswerText(const QJsonObject& object, QString* out, QString* error) {
    const QJsonValue value = object.value(QStringLiteral("answer"));
    if (!value.isString()) {
        return fail(error, QStringLiteral("Поле \"answer\" должно быть строкой"));
    }
    const QString text = value.toString();
    if (text != QLatin1String("yes") && text != QLatin1String("no")) {
        return fail(error, QStringLiteral("Поле \"answer\" должно быть \"yes\" или \"no\""));
    }
    if (out) {
        *out = text;
    }
    return true;
}

bool readCharacterArray(const QJsonObject& object, std::vector<int>* out, QString* error) {
    const QJsonValue value = object.value(QStringLiteral("characterIds"));
    if (!value.isArray()) {
        return fail(error, QStringLiteral("Поле \"characterIds\" должно быть массивом"));
    }
    const QJsonArray array = value.toArray();
    std::vector<int> ids;
    ids.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue& item : array) {
        if (!item.isDouble()) {
            return fail(error, QStringLiteral("Элементы \"characterIds\" должны быть числами"));
        }
        const double raw = item.toDouble();
        if (std::floor(raw) != raw || raw < 0 || raw > kCharacterCount - 1) {
            return fail(error, QStringLiteral("Недопустимый characterId: %1").arg(raw));
        }
        ids.push_back(static_cast<int>(raw));
    }
    if (out) {
        *out = ids;
    }
    return true;
}

} // namespace

QString hintKindToString(HintKind kind) {
    switch (kind) {
    case HintKind::Variance:
        return QStringLiteral("variance");
    case HintKind::RandomForest:
        return QStringLiteral("randomforest");
    case HintKind::CatBoost:
        return QStringLiteral("catboost");
    }
    return QStringLiteral("variance");
}

bool hintKindFromString(const QString& text, HintKind* out) {
    if (text == QLatin1String("variance")) {
        if (out) *out = HintKind::Variance;
        return true;
    }
    if (text == QLatin1String("randomforest")) {
        if (out) *out = HintKind::RandomForest;
        return true;
    }
    if (text == QLatin1String("catboost")) {
        if (out) *out = HintKind::CatBoost;
        return true;
    }
    return false;
}

QString typeToString(Type type) {
    for (const auto& entry : typeTable()) {
        if (entry.first == type) {
            return entry.second;
        }
    }
    return QStringLiteral("error");
}

bool typeFromString(const QString& text, Type* out) {
    for (const auto& entry : typeTable()) {
        if (entry.second == text) {
            if (out) {
                *out = entry.first;
            }
            return true;
        }
    }
    return false;
}

bool parseMessage(const QByteArray& line, Message* out, QString* error) {
    if (!out) {
        return fail(error, QStringLiteral("Внутренняя ошибка: out == nullptr"));
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(error, QStringLiteral("Некорректный JSON: %1").arg(parseError.errorString()));
    }
    if (!document.isObject()) {
        return fail(error, QStringLiteral("Ожидался JSON-объект"));
    }

    const QJsonObject object = document.object();
    Message message;
    message.raw = object;

    const QJsonValue typeValue = object.value(QStringLiteral("type"));
    if (!typeValue.isString()) {
        return fail(error, QStringLiteral("Отсутствует строковое поле \"type\""));
    }
    const QString typeText = typeValue.toString();
    if (!typeFromString(typeText, &message.type)) {
        return fail(error, QStringLiteral("Неизвестный тип сообщения: %1").arg(typeText));
    }

    switch (message.type) {
    case Type::SetSecret:
    case Type::Guess:
        if (!readInt(object, QStringLiteral("characterId"), 0, kCharacterCount - 1,
                     &message.characterId, error)) {
            return false;
        }
        break;
    case Type::Question:
        if (!readInt(object, QStringLiteral("questionId"), 0, kQuestionCount - 1,
                     &message.questionId, error)) {
            return false;
        }
        break;
    case Type::Hint: {
        const QJsonValue kindValue = object.value(QStringLiteral("kind"));
        if (!kindValue.isString()) {
            return fail(error, QStringLiteral("Отсутствует строковое поле \"kind\""));
        }
        message.hintKindText = kindValue.toString();
        if (!hintKindFromString(message.hintKindText, &message.hintKind)) {
            return fail(error, QStringLiteral("Неизвестный вид подсказки: %1")
                                   .arg(message.hintKindText));
        }
        break;
    }
    case Type::Remove:
        if (!readCharacterArray(object, &message.characterIds, error)) {
            return false;
        }
        break;
    case Type::GetCandidates:
    case Type::Rematch:
    case Type::Leave:
        break;
    case Type::Welcome:
    case Type::Turn:
        if (!readInt(object, QStringLiteral("player"), 1, 2, &message.player, error)) {
            return false;
        }
        break;
    case Type::Start: {
        const QJsonValue playersValue = object.value(QStringLiteral("players"));
        if (!playersValue.isArray()) {
            return fail(error, QStringLiteral("Поле \"players\" должно быть массивом"));
        }
        const QJsonArray players = playersValue.toArray();
        if (players.size() != kPlayerCount) {
            return fail(error, QStringLiteral("В поле \"players\" должно быть %1 имени")
                                   .arg(kPlayerCount));
        }
        for (const QJsonValue& item : players) {
            if (!item.isString()) {
                return fail(error, QStringLiteral("Элементы \"players\" должны быть строками"));
            }
            message.players.push_back(item.toString());
        }
        break;
    }
    case Type::Answer:
        if (!readInt(object, QStringLiteral("questionId"), 0, kQuestionCount - 1,
                     &message.questionId, error)) {
            return false;
        }
        if (!readAnswerText(object, &message.answer, error)) {
            return false;
        }
        if (!readInt(object, QStringLiteral("remaining"), 0, kCharacterCount,
                     &message.remaining, error)) {
            return false;
        }
        if (object.value(QStringLiteral("hint")).isObject()) {
            message.hasHint = true;
        }
        break;
    case Type::HintStatus: {
        const QJsonValue hintsValue = object.value(QStringLiteral("hintsLeft"));
        if (!hintsValue.isObject()) {
            return fail(error, QStringLiteral("Поле \"hintsLeft\" должно быть объектом"));
        }
        message.hintsLeft = hintsValue.toObject();
        break;
    }
    case Type::OpponentAsked:
        if (!readInt(object, QStringLiteral("questionId"), 0, kQuestionCount - 1,
                     &message.questionId, error)) {
            return false;
        }
        if (!readAnswerText(object, &message.answer, error)) {
            return false;
        }
        break;
    case Type::Candidates:
        if (!readCharacterArray(object, &message.characterIds, error)) {
            return false;
        }
        if (!readInt(object, QStringLiteral("count"), 0, kCharacterCount,
                     &message.count, error)) {
            return false;
        }
        break;
    case Type::RemoveOk:
        if (!readInt(object, QStringLiteral("remaining"), 0, kCharacterCount,
                     &message.remaining, error)) {
            return false;
        }
        break;
    case Type::GuessResult: {
        const QJsonValue correctValue = object.value(QStringLiteral("correct"));
        if (!correctValue.isBool()) {
            return fail(error, QStringLiteral("Поле \"correct\" должно быть bool"));
        }
        message.correct = correctValue.toBool();
        break;
    }
    case Type::MatchOver:
        if (!readInt(object, QStringLiteral("winner"), 1, 2, &message.winner, error)) {
            return false;
        }
        if (!readInt(object, QStringLiteral("characterId"), 0, kCharacterCount - 1,
                     &message.characterId, error)) {
            return false;
        }
        message.character = object.value(QStringLiteral("character")).toString();
        break;
    case Type::Error:
    case Type::OpponentDisconnected:
        message.text = object.value(QStringLiteral("message")).toString();
        break;
    }

    *out = message;
    return true;
}

QStringList splitFramedLines(QByteArray* buffer) {
    QStringList lines;
    if (!buffer || buffer->isEmpty()) {
        return lines;
    }

    int start = 0;
    int index = 0;
    while ((index = buffer->indexOf('\n', start)) >= 0) {
        QByteArray chunk = buffer->mid(start, index - start);
        start = index + 1;
        if (chunk.endsWith('\r')) {
            chunk.chop(1);
        }
        const QString text = QString::fromUtf8(chunk).trimmed();
        if (!text.isEmpty()) {
            lines.push_back(text);
        }
    }
    if (start > 0) {
        buffer->remove(0, start);
    }
    return lines;
}

QJsonObject makeWelcome(int player) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("welcome"));
    object.insert(QStringLiteral("player"), player);
    return object;
}

QJsonObject makeStart(const QStringList& players) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("start"));
    object.insert(QStringLiteral("players"), QJsonArray::fromStringList(players));
    return object;
}

QJsonObject makeTurn(int player) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("turn"));
    object.insert(QStringLiteral("player"), player);
    return object;
}

QJsonObject makeHintInfo(const QString& kind, double metric) {
    QJsonObject hint;
    hint.insert(QStringLiteral("kind"), kind);
    hint.insert(QStringLiteral("metric"), metric);
    return hint;
}

QJsonObject makeAnswer(int questionId, const QString& answer, int remaining,
                       const QJsonObject& hint) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("answer"));
    object.insert(QStringLiteral("questionId"), questionId);
    object.insert(QStringLiteral("answer"), answer);
    object.insert(QStringLiteral("remaining"), remaining);
    object.insert(QStringLiteral("hint"), hint.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                         : QJsonValue(hint));
    return object;
}

QJsonObject makeHintStatus(int variance, int randomForest, int catBoost) {
    QJsonObject hintsLeft;
    hintsLeft.insert(QStringLiteral("variance"), variance);
    hintsLeft.insert(QStringLiteral("randomforest"), randomForest);
    hintsLeft.insert(QStringLiteral("catboost"), catBoost);

    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("hint_status"));
    object.insert(QStringLiteral("hintsLeft"), hintsLeft);
    return object;
}

QJsonObject makeOpponentAsked(int questionId, const QString& answer) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("opponent_asked"));
    object.insert(QStringLiteral("questionId"), questionId);
    object.insert(QStringLiteral("answer"), answer);
    return object;
}

QJsonObject makeCandidates(const std::vector<int>& characterIds) {
    QJsonArray array;
    for (int id : characterIds) {
        array.append(id);
    }
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("candidates"));
    object.insert(QStringLiteral("characterIds"), array);
    object.insert(QStringLiteral("count"), static_cast<int>(characterIds.size()));
    return object;
}

QJsonObject makeRemoveOk(int remaining) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("remove_ok"));
    object.insert(QStringLiteral("remaining"), remaining);
    return object;
}

QJsonObject makeGuessResult(bool correct) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("guess_result"));
    object.insert(QStringLiteral("correct"), correct);
    return object;
}

QJsonObject makeMatchOver(int winner, int characterId, const QString& character) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("matchover"));
    object.insert(QStringLiteral("winner"), winner);
    object.insert(QStringLiteral("characterId"), characterId);
    object.insert(QStringLiteral("character"), character);
    return object;
}

QJsonObject makeError(const QString& message) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("error"));
    object.insert(QStringLiteral("message"), message);
    return object;
}

QJsonObject makeOpponentDisconnected(const QString& message) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("opponent_disconnected"));
    object.insert(QStringLiteral("message"), message);
    return object;
}

QJsonObject makeSetSecret(int characterId) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("set_secret"));
    object.insert(QStringLiteral("characterId"), characterId);
    return object;
}

QJsonObject makeQuestion(int questionId) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("question"));
    object.insert(QStringLiteral("questionId"), questionId);
    return object;
}

QJsonObject makeHint(HintKind kind) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("hint"));
    object.insert(QStringLiteral("kind"), hintKindToString(kind));
    return object;
}

QJsonObject makeGetCandidates() {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("get_candidates"));
    return object;
}

QJsonObject makeRemove(const std::vector<int>& characterIds) {
    QJsonArray array;
    for (int id : characterIds) {
        array.append(id);
    }
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("remove"));
    object.insert(QStringLiteral("characterIds"), array);
    return object;
}

QJsonObject makeGuess(int characterId) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("guess"));
    object.insert(QStringLiteral("characterId"), characterId);
    return object;
}

QJsonObject makeRematch() {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("rematch"));
    return object;
}

QJsonObject makeLeave() {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("leave"));
    return object;
}

QByteArray toLine(const QJsonObject& object) {
    QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact);
    line.append('\n');
    return line;
}

} // namespace proto
