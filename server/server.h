// server.h — авторитетный сервер «Боя Акинаторов»: лобби на 2 игроков,
// секреты, кандидаты, ходы, три подсказки, угадывание, реванш.
#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

#include <array>
#include <vector>

#include "gamecore/gamecore.h"
#include "protocol.h"

class Server : public QObject
{
    Q_OBJECT

public:
    explicit Server(QObject* parent = nullptr);
    ~Server() override;

    bool start(quint16 port, const QHostAddress& address = QHostAddress::Any);
    quint16 port() const;

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    enum class State { Lobby, WaitingSecrets, InGame, MatchOver };

    struct Player {
        QTcpSocket* socket = nullptr;
        QByteArray buffer;
        int id = 0;                       // 1 или 2
        bool hasSecret = false;
        gamecore::CharacterId secret = -1;
        gamecore::CandidateSet candidates;
        std::vector<int> asked;
        QMap<QString, int> hintsLeft;
        bool wantsRematch = false;
    };

    static constexpr int kPlayerCount = 2;

    int slotOf(const QTcpSocket* socket) const;
    static int opponentSlot(int slot);
    bool bothConnected() const;

    void send(int slot, const QJsonObject& message);
    void broadcast(const QJsonObject& message);
    void sendError(int slot, const QString& text);
    void sendHintStatus(int slot);

    void beginWaitingSecrets();
    void startMatch();
    void clearSecrets();
    void maybeStartMatch();

    void handleMessage(int slot, const proto::Message& message);
    void handleSetSecret(int slot, const proto::Message& message);
    void handleQuestion(int slot, int questionId);
    void handleHint(int slot, proto::HintKind kind);
    void handleGetCandidates(int slot);
    void handleRemove(int slot, const proto::Message& message);
    void handleGuess(int slot, int characterId);
    void handleRematch(int slot);
    void handleLeave(int slot);

    bool ensureInGame(int slot);
    bool ensureTurn(int slot);
    bool isAsked(int slot, int questionId) const;

    void applyQuestion(int slot, int questionId, const QJsonObject& hint);
    void switchTurn();
    void dropLobby(int closingSlot);

    QTcpServer server_;
    std::array<Player, kPlayerCount> players_;
    State state_ = State::Lobby;
    int turn_ = 1;
};
