// main.cpp — точка входа GUI-клиента. Host/port предзаполняются из env
// (GAME_SERVER_HOST / GAME_SERVER_PORT), но приоритет у полей в окне.
#include <QApplication>
#include <QDebug>
#include <QString>

#include "connection.h"
#include "mainwindow.h"

namespace {

constexpr int kDefaultPort = 9999;

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("akinator-client"));

    QString host = qEnvironmentVariable("GAME_SERVER_HOST").trimmed();
    if (host.isEmpty()) {
        host = QStringLiteral("127.0.0.1");
    }

    QString warning;
    const QString rawPort =
        qEnvironmentVariable("GAME_SERVER_PORT", QString::number(kDefaultPort));
    bool ok = false;
    int port = rawPort.toInt(&ok);
    if (!ok || port < 1 || port > 65535) {
        warning = QStringLiteral("Некорректный GAME_SERVER_PORT='%1', используется порт %2")
                      .arg(rawPort)
                      .arg(kDefaultPort);
        qWarning().noquote() << warning;
        port = kDefaultPort;
    }

    Connection connection;
    MainWindow window(&connection, host, port, warning);
    window.show();

    return app.exec();
}
