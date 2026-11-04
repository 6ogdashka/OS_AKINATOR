// tst_protocol.cpp — тесты (де)сериализации NDJSON-протокола.
#include <QtTest>

#include "protocol.h"

#include <QJsonValue>

class TestProtocol : public QObject
{
    Q_OBJECT

private slots:
    void parse_set_secret();
    void parse_question();
    void parse_hint();
    void parse_hint_invalid();
    void parse_guess();
    void serialize_answer();
    void serialize_matchover();
    void framing_two();
    void parse_unknown_type();
    void parse_numeric_overflow();
    void parse_start_validation();
    void framing_crlf();
    void type_roundtrip();
    void parse_full_candidates();
};

// 15. Разбор set_secret.
void TestProtocol::parse_set_secret()
{
    proto::Message message;
    QString error;
    QVERIFY(proto::parseMessage(QByteArrayLiteral(R"({"type":"set_secret","characterId":7})"),
                                &message, &error));
    QCOMPARE(message.type, proto::Type::SetSecret);
    QCOMPARE(message.characterId, 7);
}

// 16. Разбор question.
void TestProtocol::parse_question()
{
    proto::Message message;
    QString error;
    QVERIFY(proto::parseMessage(QByteArrayLiteral(R"({"type":"question","questionId":12})"),
                                &message, &error));
    QCOMPARE(message.type, proto::Type::Question);
    QCOMPARE(message.questionId, 12);
}

// 17. Разбор hint.
void TestProtocol::parse_hint()
{
    proto::Message message;
    QString error;
    QVERIFY(proto::parseMessage(QByteArrayLiteral(R"({"type":"hint","kind":"catboost"})"),
                                &message, &error));
    QCOMPARE(message.type, proto::Type::Hint);
    QCOMPARE(message.hintKind, proto::HintKind::CatBoost);
    QCOMPARE(message.hintKindText, QStringLiteral("catboost"));
}

// 18. Неизвестный вид подсказки — корректная ошибка, без падения.
void TestProtocol::parse_hint_invalid()
{
    proto::Message message;
    QString error;
    QVERIFY(!proto::parseMessage(QByteArrayLiteral(R"({"type":"hint","kind":"bogus"})"),
                                 &message, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("подсказки")));
}

// 19. Разбор guess.
void TestProtocol::parse_guess()
{
    proto::Message message;
    QString error;
    QVERIFY(proto::parseMessage(QByteArrayLiteral(R"({"type":"guess","characterId":24})"),
                                &message, &error));
    QCOMPARE(message.type, proto::Type::Guess);
    QCOMPARE(message.characterId, 24);
}

// 20. Сериализация answer (id, "yes", 31, hint = null).
void TestProtocol::serialize_answer()
{
    const QJsonObject object = proto::makeAnswer(0, QStringLiteral("yes"), 31);
    QCOMPARE(object.value(QStringLiteral("type")).toString(), QStringLiteral("answer"));
    QCOMPARE(object.value(QStringLiteral("questionId")).toInt(), 0);
    QCOMPARE(object.value(QStringLiteral("answer")).toString(), QStringLiteral("yes"));
    QCOMPARE(object.value(QStringLiteral("remaining")).toInt(), 31);
    QVERIFY(object.value(QStringLiteral("hint")).isNull());

    const QJsonObject withHint =
        proto::makeAnswer(12, QStringLiteral("no"), 9, proto::makeHintInfo(QStringLiteral("variance"), 0.96));
    QVERIFY(withHint.value(QStringLiteral("hint")).isObject());
    QCOMPARE(withHint.value(QStringLiteral("hint")).toObject().value(QStringLiteral("kind")).toString(),
             QStringLiteral("variance"));
    QCOMPARE(withHint.value(QStringLiteral("hint")).toObject().value(QStringLiteral("metric")).toDouble(),
             0.96);
}

// 21. Сериализация matchover (2, 24, "Monkey D. Luffy").
void TestProtocol::serialize_matchover()
{
    const QJsonObject object =
        proto::makeMatchOver(2, 24, QStringLiteral("Monkey D. Luffy"));
    QCOMPARE(object.value(QStringLiteral("type")).toString(), QStringLiteral("matchover"));
    QCOMPARE(object.value(QStringLiteral("winner")).toInt(), 2);
    QCOMPARE(object.value(QStringLiteral("characterId")).toInt(), 24);
    QCOMPARE(object.value(QStringLiteral("character")).toString(),
             QStringLiteral("Monkey D. Luffy"));
}

// 22. Два JSON в одном буфере режутся на два сообщения в правильном порядке.
void TestProtocol::framing_two()
{
    QByteArray buffer = QByteArrayLiteral(
        R"({"type":"question","questionId":1})"
        "\n"
        R"({"type":"hint","kind":"variance"})"
        "\n");

    const QStringList lines = proto::splitFramedLines(&buffer);
    QCOMPARE(lines.size(), 2);
    QVERIFY(buffer.isEmpty());

    proto::Message first;
    proto::Message second;
    QString error;
    QVERIFY(proto::parseMessage(lines.at(0).toUtf8(), &first, &error));
    QVERIFY(proto::parseMessage(lines.at(1).toUtf8(), &second, &error));
    QCOMPARE(first.type, proto::Type::Question);
    QCOMPARE(first.questionId, 1);
    QCOMPARE(second.type, proto::Type::Hint);
    QCOMPARE(second.hintKind, proto::HintKind::Variance);

    // Неполная строка остаётся в буфере до прихода '\n'.
    QByteArray partial = QByteArrayLiteral(R"({"type":"question","questionId":5})");
    QVERIFY(proto::splitFramedLines(&partial).isEmpty());
    QVERIFY(!partial.isEmpty());
    partial.append('\n');
    const QStringList completed = proto::splitFramedLines(&partial);
    QCOMPARE(completed.size(), 1);
    QVERIFY(partial.isEmpty());
}

// 23. Неизвестный тип — ошибка, без падения.
void TestProtocol::parse_unknown_type()
{
    proto::Message message;
    QString error;
    QVERIFY(!proto::parseMessage(QByteArrayLiteral(R"({"type":"bogus"})"), &message, &error));
    QVERIFY(error.contains(QStringLiteral("Неизвестный тип")));

    QVERIFY(!proto::parseMessage(QByteArrayLiteral("not a json"), &message, &error));
    QVERIFY(!error.isEmpty());
}

// 24. Числа вне int-диапазона и дробные значения отвергаются без переполнения.
void TestProtocol::parse_numeric_overflow()
{
    const QByteArray lines[] = {
        QByteArrayLiteral(R"({"type":"guess","characterId":1e18})"),
        QByteArrayLiteral(R"({"type":"question","questionId":-1e18})"),
        QByteArrayLiteral(R"({"type":"question","questionId":1.5})"),
        QByteArrayLiteral(R"({"type":"remove","characterIds":[1e18]})"),
        QByteArrayLiteral(R"({"type":"remove","characterIds":[1.5]})"),
    };
    for (const QByteArray& line : lines) {
        proto::Message message;
        QString error;
        QVERIFY2(!proto::parseMessage(line, &message, &error), line.constData());
        QVERIFY2(!error.isEmpty(), line.constData());
    }
}

// 25. start: ровно два имени, и это именно строки.
void TestProtocol::parse_start_validation()
{
    proto::Message message;
    QString error;
    QVERIFY(proto::parseMessage(QByteArrayLiteral(R"({"type":"start","players":["A","B"]})"),
                                &message, &error));
    QCOMPARE(message.type, proto::Type::Start);
    QCOMPARE(message.players.size(), 2);
    QCOMPARE(message.players.at(1), QStringLiteral("B"));

    QVERIFY(!proto::parseMessage(QByteArrayLiteral(R"({"type":"start","players":["A"]})"),
                                 &message, &error));
    QVERIFY(!proto::parseMessage(QByteArrayLiteral(R"({"type":"start","players":[1,2]})"),
                                 &message, &error));
    QVERIFY(!proto::parseMessage(QByteArrayLiteral(R"({"type":"start","players":"A,B"})"),
                                 &message, &error));
}

// 26. Framing: CRLF и пустые строки не создают лишних сообщений.
void TestProtocol::framing_crlf()
{
    QByteArray buffer = QByteArrayLiteral(
        "{\"type\":\"rematch\"}\r\n"
        "\r\n"
        "\n"
        "{\"type\":\"leave\"}\r\n");
    const QStringList lines = proto::splitFramedLines(&buffer);
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.at(0), QStringLiteral("{\"type\":\"rematch\"}"));
    QCOMPARE(lines.at(1), QStringLiteral("{\"type\":\"leave\"}"));
    QVERIFY(buffer.isEmpty());
}

// 27. Каждый тип сообщения и вид подсказки сериализуются и разбираются обратно.
void TestProtocol::type_roundtrip()
{
    const proto::Type types[] = {
        proto::Type::SetSecret,     proto::Type::Question,   proto::Type::Hint,
        proto::Type::GetCandidates, proto::Type::Remove,     proto::Type::Guess,
        proto::Type::Rematch,       proto::Type::Leave,      proto::Type::Welcome,
        proto::Type::Start,         proto::Type::Turn,       proto::Type::Answer,
        proto::Type::HintStatus,    proto::Type::OpponentAsked, proto::Type::Candidates,
        proto::Type::RemoveOk,      proto::Type::GuessResult, proto::Type::MatchOver,
        proto::Type::Error,         proto::Type::OpponentDisconnected,
    };
    for (proto::Type type : types) {
        const QString text = proto::typeToString(type);
        QVERIFY2(!text.isEmpty(), qPrintable(text));
        proto::Type parsed = proto::Type::Error;
        QVERIFY2(proto::typeFromString(text, &parsed), qPrintable(text));
        QCOMPARE(parsed, type);
    }

    const proto::HintKind kinds[] = {proto::HintKind::Variance, proto::HintKind::RandomForest,
                                     proto::HintKind::CatBoost};
    for (proto::HintKind kind : kinds) {
        const QString text = proto::hintKindToString(kind);
        proto::HintKind parsed = proto::HintKind::Variance;
        QVERIFY(proto::hintKindFromString(text, &parsed));
        QCOMPARE(parsed, kind);
    }
}

// 28. remaining/count = 60 допустимы: верхняя граница — размер таблицы, поэтому
//     полный набор (старт партии, вопрос без отсева) валиден.
void TestProtocol::parse_full_candidates()
{
    {
        proto::Message message;
        QString error;
        QVERIFY(proto::parseMessage(
            QByteArrayLiteral(R"({"type":"answer","questionId":3,"answer":"yes","remaining":60})"),
            &message, &error));
        QCOMPARE(message.remaining, 60);

        QVERIFY(proto::parseMessage(
            QByteArrayLiteral(R"({"type":"remove_ok","remaining":60})"), &message, &error));
        QCOMPARE(message.remaining, 60);

        QVERIFY(proto::parseMessage(
            QByteArrayLiteral(R"({"type":"candidates","characterIds":[0],"count":60})"),
            &message, &error));
        QCOMPARE(message.count, 60);

        QVERIFY(!proto::parseMessage(
            QByteArrayLiteral(R"({"type":"answer","questionId":3,"answer":"yes","remaining":61})"),
            &message, &error));
    }
}

QTEST_APPLESS_MAIN(TestProtocol)

#include "tst_protocol.moc"
