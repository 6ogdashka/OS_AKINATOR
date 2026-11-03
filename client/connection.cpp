// connection.cpp — реализация клиентского сетевого канала.
#include "connection.h"

#include <QDebug>

Connection::Connection(QObject* parent)
    : QObject(parent)
{
    connect(&socket_, &QTcpSocket::connected, this, &Connection::onConnected);
    connect(&socket_, &QTcpSocket::readyRead, this, &Connection::onReadyRead);
    connect(&socket_, &QTcpSocket::disconnected, this, &Connection::onDisconnected);
    connect(&socket_, &QTcpSocket::errorOccurred, this, &Connection::onSocketError);
}

Connection::~Connection() = default;

void Connection::connectToServer(const QString& host, quint16 port)
{
    buffer_.clear();
    qInfo().noquote() << QStringLiteral("Подключение к %1:%2").arg(host).arg(port);
    socket_.abort();
    socket_.connectToHost(host, port);
}

void Connection::disconnectFromServer()
{
    if (socket_.state() == QAbstractSocket::UnconnectedState) {
        return;
    }
    socket_.disconnectFromHost();
}

bool Connection::isConnected() const
{
    return socket_.state() == QAbstractSocket::ConnectedState;
}

void Connection::send(const QJsonObject& message)
{
    if (!isConnected()) {
        qWarning().noquote() << QStringLiteral("Сообщение не отправлено: нет соединения");
        return;
    }
    socket_.write(proto::toLine(message));
    socket_.flush();
}

void Connection::onConnected()
{
    qInfo().noquote() << QStringLiteral("Соединение установлено");
    emit connected();
}

void Connection::onDisconnected()
{
    qInfo().noquote() << QStringLiteral("Соединение закрыто");
    buffer_.clear();
    emit connectionClosed();
}

void Connection::onSocketError()
{
    if (socket_.state() == QAbstractSocket::ConnectedState) {
        qWarning().noquote() << QStringLiteral("Сетевая ошибка: %1").arg(socket_.errorString());
        return;
    }
    qWarning().noquote() << QStringLiteral("Ошибка подключения: %1").arg(socket_.errorString());
    emit serverError(socket_.errorString());
    emit connectionClosed();
}

void Connection::onReadyRead()
{
    buffer_.append(socket_.readAll());

    if (buffer_.size() > proto::kMaxLineBytes) {
        // '\n' так и не пришёл: не держим бесконечно растущий буфер.
        qWarning().noquote() << QStringLiteral("Слишком длинное сообщение от сервера");
        buffer_.clear();
        emit serverError(QStringLiteral("Сервер прислал слишком длинное сообщение"));
        socket_.abort();
        return;
    }

    const QStringList lines = proto::splitFramedLines(&buffer_);
    for (const QString& line : lines) {
        proto::Message message;
        QString error;
        if (!proto::parseMessage(line.toUtf8(), &message, &error)) {
            qWarning().noquote() << QStringLiteral("Некорректное сообщение: %1").arg(error);
            emit serverError(error);
            continue;
        }
        handleMessage(message);
    }
}

void Connection::handleMessage(const proto::Message& message)
{
    switch (message.type) {
    case proto::Type::Welcome:
        emit welcome(message.player);
        break;
    case proto::Type::Start:
        emit start(message.players);
        break;
    case proto::Type::Turn:
        emit turnChanged(message.player);
        break;
    case proto::Type::Answer:
        emit answerReceived(message);
        break;
    case proto::Type::HintStatus:
        emit hintStatusReceived(message.hintsLeft);
        break;
    case proto::Type::OpponentAsked:
        emit opponentAsked(message.questionId, message.answer);
        break;
    case proto::Type::Candidates: {
        QList<int> ids;
        ids.reserve(static_cast<int>(message.characterIds.size()));
        for (int id : message.characterIds) {
            ids.append(id);
        }
        emit candidatesReceived(ids);
        break;
    }
    case proto::Type::RemoveOk:
        emit removeOk(message.remaining);
        break;
    case proto::Type::GuessResult:
        emit guessResult(message.correct);
        break;
    case proto::Type::MatchOver:
        emit matchOver(message.winner, message.characterId, message.character);
        break;
    case proto::Type::Error:
        emit serverError(message.text);
        break;
    case proto::Type::OpponentDisconnected:
        emit opponentDisconnected(message.text);
        break;
    default:
        qWarning().noquote() << QStringLiteral("Неожиданный тип сообщения от сервера");
        break;
    }
}
