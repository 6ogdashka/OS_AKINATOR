// connection.h — клиентская обёртка над QTcpSocket: NDJSON-канал до сервера.
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTcpSocket>

#include "protocol.h"

class Connection : public QObject
{
    Q_OBJECT

public:
    explicit Connection(QObject* parent = nullptr);
    ~Connection() override;

    void connectToServer(const QString& host, quint16 port);
    void disconnectFromServer();
    bool isConnected() const;
    void send(const QJsonObject& message);

signals:
    void connected();
    void welcome(int player);
    void start(const QStringList& players);
    void turnChanged(int player);
    void answerReceived(const proto::Message& message);
    void hintStatusReceived(const QJsonObject& hintsLeft);
    void opponentAsked(int questionId, const QString& answer);
    void candidatesReceived(const QList<int>& characterIds);
    void removeOk(int remaining);
    void guessResult(bool correct);
    void matchOver(int winner, int characterId, const QString& character);
    void serverError(const QString& text);
    void opponentDisconnected(const QString& text);
    void connectionClosed();

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onSocketError();

private:
    void handleMessage(const proto::Message& message);

    QTcpSocket socket_;
    QByteArray buffer_;
};
