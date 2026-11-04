// server.cpp — реализация авторитетного сервера.
#include "server.h"

#include <QDebug>
#include <QJsonValue>

#include <algorithm>
#include <numeric>

// Пределы протокола и игрового ядра должны совпадать: числа в NDJSON-валидации
// (common/protocol.h) обязаны соответствовать размеру данных gamecore.
static_assert(proto::kCharacterCount == gamecore::kCharacterCount,
              "protocol::kCharacterCount расходится с gamecore::kCharacterCount");
static_assert(proto::kQuestionCount == gamecore::kQuestionCount,
              "protocol::kQuestionCount расходится с gamecore::kQuestionCount");

namespace {

constexpr int kHintsPerKind = 1; // по одной подсказке каждого вида (раздел 7 ТЗ)

} // namespace

Server::Server(QObject* parent)
    : QObject(parent)
{
    connect(&server_, &QTcpServer::newConnection, this, &Server::onNewConnection);
}

Server::~Server() = default;

bool Server::start(quint16 port, const QHostAddress& address)
{
    if (!server_.listen(address, port)) {
        qCritical().noquote() << QStringLiteral("Не удалось слушать адрес %1:%2: %3")
                                     .arg(address.toString())
                                     .arg(port)
                                     .arg(server_.errorString());
        return false;
    }
    qInfo().noquote() << QStringLiteral("Сервер слушает %1:%2")
                             .arg(server_.serverAddress().toString())
                             .arg(server_.serverPort());
    return true;
}

quint16 Server::port() const
{
    return server_.serverPort();
}

int Server::slotOf(const QTcpSocket* socket) const
{
    if (!socket) {
        return -1;
    }
    for (int i = 0; i < kPlayerCount; ++i) {
        if (players_[i].socket == socket) {
            return i;
        }
    }
    return -1;
}

int Server::opponentSlot(int slot)
{
    return slot == 0 ? 1 : 0;
}

bool Server::bothConnected() const
{
    return players_[0].socket != nullptr && players_[1].socket != nullptr;
}

void Server::send(int slot, const QJsonObject& message)
{
    if (slot < 0 || slot >= kPlayerCount) {
        return;
    }
    Player& player = players_[slot];
    if (!player.socket) {
        return;
    }
    player.socket->write(proto::toLine(message));
    player.socket->flush();
}

void Server::broadcast(const QJsonObject& message)
{
    for (int i = 0; i < kPlayerCount; ++i) {
        send(i, message);
    }
}

void Server::sendError(int slot, const QString& text)
{
    qWarning().noquote() << QStringLiteral("Ошибка игроку %1: %2")
                                .arg(slot + 1)
                                .arg(text);
    send(slot, proto::makeError(text));
}

void Server::sendHintStatus(int slot)
{
    const Player& player = players_[slot];
    send(slot, proto::makeHintStatus(player.hintsLeft.value(QStringLiteral("variance"), 0),
                                     player.hintsLeft.value(QStringLiteral("randomforest"), 0),
                                     player.hintsLeft.value(QStringLiteral("catboost"), 0)));
}

void Server::beginWaitingSecrets()
{
    // Секреты (hasSecret/secret) намеренно не сбрасываются: игрок вправе загадать
    // персонажа сразу после welcome, ещё до подключения второго игрока.
    // После реванша секреты очищает clearSecrets().
    for (Player& player : players_) {
        player.candidates.clear();
        player.asked.clear();
        player.hintsLeft.clear();
        player.lastTurnWasHint = false;
        player.wantsRematch = false;
    }
    turn_ = 1;
    state_ = State::WaitingSecrets;
    qInfo().noquote() << QStringLiteral("Ожидание секретов от обоих игроков");
}

void Server::clearSecrets()
{
    for (Player& player : players_) {
        player.hasSecret = false;
        player.secret = -1;
    }
}

void Server::maybeStartMatch()
{
    if (state_ != State::WaitingSecrets) {
        return;
    }
    if (players_[0].hasSecret && players_[1].hasSecret) {
        startMatch();
    }
}

void Server::startMatch()
{
    state_ = State::InGame;
    turn_ = 1;

    gamecore::CandidateSet all(gamecore::kCharacterCount);
    std::iota(all.begin(), all.end(), 0);

    for (Player& player : players_) {
        player.candidates = all;
        player.asked.clear();
        player.hintsLeft.insert(QStringLiteral("variance"), kHintsPerKind);
        player.hintsLeft.insert(QStringLiteral("randomforest"), kHintsPerKind);
        player.hintsLeft.insert(QStringLiteral("catboost"), kHintsPerKind);
        player.lastTurnWasHint = false;
        player.wantsRematch = false;
    }

    const QStringList names = {QStringLiteral("Player 1"), QStringLiteral("Player 2")};
    for (int slot = 0; slot < kPlayerCount; ++slot) {
        send(slot, proto::makeStart(names));
        send(slot, proto::makeTurn(turn_));
        sendHintStatus(slot);
    }

    qInfo().noquote() << QStringLiteral("Игра началась: ход игрока %1 (кандидатов у каждого: %2)")
                             .arg(turn_)
                             .arg(players_[0].candidates.size());
}

void Server::onNewConnection()
{
    while (server_.hasPendingConnections()) {
        QTcpSocket* socket = server_.nextPendingConnection();

        int slot = -1;
        for (int i = 0; i < kPlayerCount; ++i) {
            if (!players_[i].socket) {
                slot = i;
                break;
            }
        }

        if (slot < 0) {
            socket->write(proto::toLine(proto::makeError(QStringLiteral("Сервер занят"))));
            socket->flush();
            socket->disconnectFromHost();
            socket->deleteLater();
            qWarning().noquote() << QStringLiteral("Третье подключение отклонено (сервер занят)");
            continue;
        }

        Player& player = players_[slot];
        player = Player{};
        player.socket = socket;
        player.id = slot + 1;

        connect(socket, &QTcpSocket::readyRead, this, &Server::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &Server::onDisconnected);

        send(slot, proto::makeWelcome(player.id));
        qInfo().noquote() << QStringLiteral("Игрок %1 подключился (%2:%3)")
                                 .arg(player.id)
                                 .arg(socket->peerAddress().toString())
                                 .arg(socket->peerPort());

        if (bothConnected() && state_ == State::Lobby) {
            beginWaitingSecrets();
            qInfo().noquote() << QStringLiteral("Оба игрока на месте");
        }
    }
}

void Server::onReadyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) {
        return;
    }
    int slot = slotOf(socket);
    if (slot < 0) {
        socket->readAll();
        return;
    }

    players_[slot].buffer.append(socket->readAll());

    if (players_[slot].buffer.size() > proto::kMaxLineBytes) {
        // '\n' так и не пришёл: рвём соединение, иначе буфер растёт бесконечно.
        sendError(slot, QStringLiteral("Слишком длинное сообщение"));
        players_[slot].buffer.clear();
        socket->disconnectFromHost();
        return;
    }

    const QStringList lines = proto::splitFramedLines(&players_[slot].buffer);
    for (const QString& line : lines) {
        const int current = slotOf(socket);
        if (current < 0) {
            return; // соединение уже сброшено
        }

        proto::Message message;
        QString error;
        if (!proto::parseMessage(line.toUtf8(), &message, &error)) {
            sendError(current, error);
            continue;
        }
        handleMessage(current, message);
    }
}

void Server::onDisconnected()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    const int slot = slotOf(socket);
    if (slot < 0) {
        if (socket) {
            socket->deleteLater();
        }
        return;
    }
    qInfo().noquote() << QStringLiteral("Игрок %1 отключился").arg(players_[slot].id);
    dropLobby(slot);
}

void Server::dropLobby(int closingSlot)
{
    const int other = opponentSlot(closingSlot);
    QTcpSocket* dying = players_[closingSlot].socket;
    QTcpSocket* survivor = players_[other].socket;

    if (survivor) {
        send(other, proto::makeOpponentDisconnected(QStringLiteral("Противник отключился")));
    }

    state_ = State::Lobby;
    for (Player& player : players_) {
        player = Player{};
    }
    turn_ = 1;

    if (dying) {
        QObject::disconnect(dying, nullptr, this, nullptr);
        dying->disconnectFromHost();
        dying->deleteLater();
    }
    if (survivor) {
        QObject::disconnect(survivor, nullptr, this, nullptr);
        survivor->disconnectFromHost();
        survivor->deleteLater();
    }

    qInfo().noquote() << QStringLiteral("Лобби сброшено, сервер ждёт новых игроков");
}

void Server::handleMessage(int slot, const proto::Message& message)
{
    switch (message.type) {
    case proto::Type::SetSecret:
        handleSetSecret(slot, message);
        break;
    case proto::Type::Question:
        handleQuestion(slot, message.questionId);
        break;
    case proto::Type::Hint:
        handleHint(slot, message.hintKind);
        break;
    case proto::Type::GetCandidates:
        handleGetCandidates(slot);
        break;
    case proto::Type::Remove:
        handleRemove(slot, message);
        break;
    case proto::Type::Guess:
        handleGuess(slot, message.characterId);
        break;
    case proto::Type::Rematch:
        handleRematch(slot);
        break;
    case proto::Type::Leave:
        handleLeave(slot);
        break;
    default:
        sendError(slot, QStringLiteral("Сервер не принимает сообщение этого типа"));
        break;
    }
}

bool Server::ensureInGame(int slot)
{
    if (slot < 0 || slot >= kPlayerCount || !players_[slot].socket) {
        return false;
    }
    if (state_ != State::InGame) {
        sendError(slot, QStringLiteral("Сейчас игра не идёт"));
        return false;
    }
    return true;
}

bool Server::ensureTurn(int slot)
{
    if (!ensureInGame(slot)) {
        return false;
    }
    if (turn_ != players_[slot].id) {
        sendError(slot, QStringLiteral("Сейчас не ваш ход"));
        return false;
    }
    return true;
}

bool Server::isAsked(int slot, int questionId) const
{
    const std::vector<int>& asked = players_[slot].asked;
    return std::find(asked.begin(), asked.end(), questionId) != asked.end();
}

void Server::handleSetSecret(int slot, const proto::Message& message)
{
    Player& player = players_[slot];

    if (state_ != State::WaitingSecrets && state_ != State::Lobby) {
        sendError(slot, QStringLiteral("Сейчас нельзя загадывать персонажа"));
        return;
    }
    if (player.hasSecret) {
        sendError(slot, QStringLiteral("Персонаж уже загадан"));
        return;
    }
    if (message.characterId < 0 || message.characterId >= gamecore::kCharacterCount) {
        sendError(slot, QStringLiteral("Недопустимый characterId"));
        return;
    }

    player.secret = message.characterId;
    player.hasSecret = true;
    qInfo().noquote() << QStringLiteral("Игрок %1 загадал персонажа #%2 (%3)")
                             .arg(player.id)
                             .arg(player.secret)
                             .arg(QString::fromStdString(
                                 gamecore::characterNames()[static_cast<std::size_t>(player.secret)]));

    maybeStartMatch();
}

void Server::handleQuestion(int slot, int questionId)
{
    if (!ensureTurn(slot)) {
        return;
    }
    if (questionId < 0 || questionId >= gamecore::kQuestionCount) {
        sendError(slot, QStringLiteral("Недопустимый questionId"));
        return;
    }
    if (isAsked(slot, questionId)) {
        sendError(slot, QStringLiteral("Этот вопрос уже задан"));
        return;
    }
    players_[slot].lastTurnWasHint = false; // обычный вопрос «разрешает» подсказку
    applyQuestion(slot, questionId, QJsonObject());
}

void Server::handleHint(int slot, proto::HintKind kind)
{
    if (!ensureTurn(slot)) {
        return;
    }

    Player& player = players_[slot];
    const QString key = proto::hintKindToString(kind);
    if (player.hintsLeft.value(key, 0) <= 0) {
        sendError(slot, QStringLiteral("Подсказка «%1» уже использована").arg(key));
        return;
    }
    if (player.lastTurnWasHint) {
        // Раздел 7 ТЗ: подсказки не подряд — между ними нужен обычный ход
        // (вопрос или угадывание), иначе ими можно вычистить таблицу за три хода.
        sendError(slot, QStringLiteral("Подсказку нельзя использовать два хода подряд — "
                                       "сначала задайте обычный вопрос или угадайте"));
        return;
    }

    gamecore::HintResult result;
    switch (kind) {
    case proto::HintKind::Variance:
        result = gamecore::hintVariance(player.candidates, player.asked);
        break;
    case proto::HintKind::RandomForest:
        result = gamecore::hintRandomForest(player.candidates, player.asked);
        break;
    case proto::HintKind::CatBoost:
        result = gamecore::hintCatBoost(player.candidates, player.asked);
        break;
    }

    if (result.question < 0) {
        sendError(slot, QStringLiteral("Нет информативного вопроса"));
        return;
    }

    player.hintsLeft.insert(key, player.hintsLeft.value(key) - 1);
    player.lastTurnWasHint = true;

    const QJsonObject hint = proto::makeHintInfo(key, result.score);
    qInfo().noquote() << QStringLiteral("Игрок %1 использует подсказку %2 -> вопрос #%3 (score %4)")
                             .arg(player.id)
                             .arg(key)
                             .arg(result.question)
                             .arg(result.score);

    // Сначала сообщаем обновлённый остаток подсказок, затем применяем вопрос:
    // порядок у спрашивающего hint_status -> answer -> turn.
    sendHintStatus(slot);
    applyQuestion(slot, result.question, hint);
}

void Server::applyQuestion(int slot, int questionId, const QJsonObject& hint)
{
    Player& player = players_[slot];
    const Player& opponent = players_[opponentSlot(slot)];

    const bool yes = gamecore::value(opponent.secret, questionId) > 0;
    const QString answer = yes ? QStringLiteral("yes") : QStringLiteral("no");

    player.asked.push_back(questionId);
    // Ответ авторитетный, поэтому сервер сам сокращает набор кандидатов — и для
    // обычного вопроса, и для подсказки: игроку не нужно чистить таблицу руками.
    player.candidates =
        gamecore::filterCandidates(player.candidates, questionId, yes ? 1 : -1);

    send(slot, proto::makeAnswer(questionId, answer,
                                 static_cast<int>(player.candidates.size()), hint));
    send(opponentSlot(slot), proto::makeOpponentAsked(questionId, answer));

    qInfo().noquote() << QStringLiteral("Игрок %1 спросил #%2 (%3) -> %4, %5: %6")
                             .arg(player.id)
                             .arg(questionId)
                             .arg(QString::fromStdString(
                                 gamecore::questionTexts()[static_cast<std::size_t>(questionId)]))
                             .arg(answer)
                             .arg(hint.isEmpty() ? QStringLiteral("таблица отфильтрована")
                                                 : QStringLiteral("подсказка отфильтровала"))
                             .arg(player.candidates.size());

    switchTurn();
}

void Server::switchTurn()
{
    turn_ = turn_ == 1 ? 2 : 1;
    broadcast(proto::makeTurn(turn_));
    qInfo().noquote() << QStringLiteral("Ход переходит к игроку %1").arg(turn_);
}

void Server::handleGetCandidates(int slot)
{
    if (!ensureInGame(slot)) {
        return;
    }
    const Player& player = players_[slot];
    send(slot, proto::makeCandidates(player.candidates));
    qInfo().noquote() << QStringLiteral("Игрок %1 запросил кандидатов (%2)")
                             .arg(player.id)
                             .arg(player.candidates.size());
}

void Server::handleRemove(int slot, const proto::Message& message)
{
    if (!ensureInGame(slot)) {
        return;
    }

    Player& player = players_[slot];
    gamecore::CandidateSet& candidates = player.candidates;

    int removed = 0;
    for (int id : message.characterIds) {
        const auto it = std::find(candidates.begin(), candidates.end(), id);
        if (it != candidates.end()) {
            candidates.erase(it);
            ++removed;
        }
    }

    send(slot, proto::makeRemoveOk(static_cast<int>(candidates.size())));
    qInfo().noquote() << QStringLiteral("Игрок %1 удалил %2 кандидатов, осталось %3")
                             .arg(player.id)
                             .arg(removed)
                             .arg(candidates.size());
}

void Server::handleGuess(int slot, int characterId)
{
    if (!ensureTurn(slot)) {
        return;
    }

    Player& player = players_[slot];
    if (characterId < 0 || characterId >= gamecore::kCharacterCount) {
        sendError(slot, QStringLiteral("Недопустимый characterId"));
        return;
    }
    if (std::find(player.candidates.begin(), player.candidates.end(), characterId)
        == player.candidates.end()) {
        sendError(slot, QStringLiteral("Этого персонажа нет среди ваших кандидатов"));
        return;
    }

    const Player& opponent = players_[opponentSlot(slot)];
    player.lastTurnWasHint = false; // догадка — не подсказка, запрет снимается
    const int secret = opponent.secret;
    const QString secretName =
        QString::fromStdString(gamecore::characterNames()[static_cast<std::size_t>(secret)]);

    if (characterId == secret) {
        state_ = State::MatchOver;
        for (Player& p : players_) {
            p.wantsRematch = false;
        }
        broadcast(proto::makeMatchOver(player.id, secret, secretName));
        qInfo().noquote() << QStringLiteral("Игрок %1 угадал «%2» и победил")
                                 .arg(player.id)
                                 .arg(secretName);
        return;
    }

    const QString guessedName =
        QString::fromStdString(gamecore::characterNames()[static_cast<std::size_t>(characterId)]);
    send(slot, proto::makeGuessResult(false));
    qInfo().noquote() << QStringLiteral("Игрок %1 ошибся: «%2» вместо «%3»")
                             .arg(player.id)
                             .arg(guessedName)
                             .arg(secretName);
    switchTurn();
}

void Server::handleRematch(int slot)
{
    if (slot < 0 || slot >= kPlayerCount || !players_[slot].socket) {
        return;
    }
    if (state_ != State::MatchOver) {
        sendError(slot, QStringLiteral("Реванш возможен только после окончания партии"));
        return;
    }

    players_[slot].wantsRematch = true;
    qInfo().noquote() << QStringLiteral("Игрок %1 хочет реванш").arg(players_[slot].id);

    if (players_[0].wantsRematch && players_[1].wantsRematch) {
        clearSecrets();
        beginWaitingSecrets();
        qInfo().noquote() << QStringLiteral("Оба игрока готовы к реваншу");
    }
}

void Server::handleLeave(int slot)
{
    if (slot < 0 || slot >= kPlayerCount) {
        return;
    }
    qInfo().noquote() << QStringLiteral("Игрок %1 покинул игру").arg(slot + 1);
    dropLobby(slot);
}
