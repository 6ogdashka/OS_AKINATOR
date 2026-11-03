// main.cpp — точка входа сервера. Порт берётся из GAME_SERVER_PORT (по умолчанию 9999).
#include <QCoreApplication>
#include <QDebug>
#include <QString>

#include "server.h"

namespace {

constexpr int kDefaultPort = 9999;

quint16 resolvePort()
{
    const QString raw = qEnvironmentVariable("GAME_SERVER_PORT", QString::number(kDefaultPort));
    bool ok = false;
    const int port = raw.toInt(&ok);
    if (!ok || port < 1 || port > 65535) {
        qWarning().noquote() << QStringLiteral("Некорректный GAME_SERVER_PORT='%1', "
                                               "используется порт %2")
                                    .arg(raw)
                                    .arg(kDefaultPort);
        return static_cast<quint16>(kDefaultPort);
    }
    return static_cast<quint16>(port);
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("akinator-server"));

    const quint16 port = resolvePort();

    Server server;
    if (!server.start(port)) {
        qCritical().noquote() << QStringLiteral("Не удалось запустить сервер на порту %1")
                                     .arg(port);
        return 1;
    }

    return app.exec();
}
