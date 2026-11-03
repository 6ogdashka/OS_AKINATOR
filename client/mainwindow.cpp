// mainwindow.cpp — реализация GUI-клиента (раздел 8 ТЗ).
#include "mainwindow.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <cstddef>
#include <vector>

#include "connection.h"
#include "gamecore/gamecore.h"

namespace {

constexpr int kConnectPage = 0;
constexpr int kSecretPage = 1;
constexpr int kGamePage = 2;

// Скрыть элементы списка, не содержащие подстроку поиска (без учёта регистра).
void filterList(QListWidget* list, const QString& text)
{
    const QString needle = text.trimmed();
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem* item = list->item(row);
        const bool matches = needle.isEmpty() || item->text().contains(needle, Qt::CaseInsensitive);
        item->setHidden(!matches);
    }
}

// Число элементов, видимых с учётом текущего фильтра поиска.
int visibleItemCount(QListWidget* list)
{
    int visible = 0;
    for (int row = 0; row < list->count(); ++row) {
        if (!list->item(row)->isHidden()) {
            ++visible;
        }
    }
    return visible;
}

} // namespace

MainWindow::MainWindow(Connection* connection, const QString& host, int port,
                       const QString& envWarning, QWidget* parent)
    : QMainWindow(parent)
    , connection_(connection)
{
    buildUi();

    hostField_->setText(host);
    portField_->setValue(port);
    if (!envWarning.isEmpty()) {
        connectStatusLabel_->setText(envWarning);
    }

    connect(connection_, &Connection::connected, this, [this]() {
        connectButton_->setEnabled(false);
        hostField_->setEnabled(false);
        portField_->setEnabled(false);
        connectStatusLabel_->setText(QStringLiteral("Подключение установлено, ждём приветствия…"));
    });
    connect(connection_, &Connection::welcome, this, &MainWindow::onWelcome);
    connect(connection_, &Connection::start, this, &MainWindow::onStart);
    connect(connection_, &Connection::turnChanged, this, &MainWindow::onTurnChanged);
    connect(connection_, &Connection::answerReceived, this, &MainWindow::onAnswer);
    connect(connection_, &Connection::hintStatusReceived, this, &MainWindow::onHintStatus);
    connect(connection_, &Connection::opponentAsked, this, &MainWindow::onOpponentAsked);
    connect(connection_, &Connection::candidatesReceived, this, &MainWindow::onCandidates);
    connect(connection_, &Connection::removeOk, this, &MainWindow::onRemoveOk);
    connect(connection_, &Connection::guessResult, this, &MainWindow::onGuessResult);
    connect(connection_, &Connection::matchOver, this, &MainWindow::onMatchOver);
    connect(connection_, &Connection::serverError, this, &MainWindow::onServerError);
    connect(connection_, &Connection::opponentDisconnected, this,
            &MainWindow::onOpponentDisconnected);
    connect(connection_, &Connection::connectionClosed, this, &MainWindow::onConnectionClosed);

    resetGameData();
    applyState(UiState::Disconnected);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Бой Акинаторов"));
    resize(820, 640);

    stack_ = new QStackedWidget(this);
    stack_->addWidget(buildConnectPage());
    stack_->addWidget(buildSecretPage());
    stack_->addWidget(buildGamePage());
    setCentralWidget(stack_);
}

QWidget* MainWindow::buildConnectPage()
{
    auto* page = new QWidget(this);

    hostField_ = new QLineEdit(page);
    hostField_->setPlaceholderText(QStringLiteral("127.0.0.1"));

    portField_ = new QSpinBox(page);
    portField_->setRange(1, 65535);
    portField_->setValue(9999);

    connectButton_ = new QPushButton(QStringLiteral("Подключиться"), page);

    connectStatusLabel_ = new QLabel(page);
    connectStatusLabel_->setWordWrap(true);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Адрес сервера:"), hostField_);
    form->addRow(QStringLiteral("Порт:"), portField_);

    auto* box = new QGroupBox(QStringLiteral("Подключение к серверу"), page);
    auto* boxLayout = new QVBoxLayout(box);
    boxLayout->addLayout(form);
    boxLayout->addWidget(connectButton_);
    boxLayout->addWidget(connectStatusLabel_);

    auto* layout = new QVBoxLayout(page);
    layout->addStretch();
    layout->addWidget(box);
    layout->addStretch();

    connect(connectButton_, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    return page;
}

QWidget* MainWindow::buildSecretPage()
{
    auto* page = new QWidget(this);

    auto* title = new QLabel(QStringLiteral("Загадайте персонажа для соперника"), page);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setBold(true);
    title->setFont(titleFont);

    searchField_ = new QLineEdit(page);
    searchField_->setPlaceholderText(QStringLiteral("Поиск персонажа…"));

    characterList_ = new QListWidget(page);
    for (int id = 0; id < gamecore::kCharacterCount; ++id) {
        auto* item = new QListWidgetItem(characterName(id), characterList_);
        item->setData(Qt::UserRole, id);
    }

    confirmSecretButton_ = new QPushButton(QStringLiteral("Загадать выбранного"), page);
    secretStatusLabel_ = new QLabel(page);
    secretStatusLabel_->setWordWrap(true);

    auto* layout = new QVBoxLayout(page);
    layout->addWidget(title);
    layout->addWidget(searchField_);
    layout->addWidget(characterList_, 1);
    layout->addWidget(confirmSecretButton_);
    layout->addWidget(secretStatusLabel_);

    connect(searchField_, &QLineEdit::textChanged, this, &MainWindow::onSecretSearchChanged);
    connect(characterList_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { onConfirmSecretClicked(); });
    connect(confirmSecretButton_, &QPushButton::clicked, this,
            &MainWindow::onConfirmSecretClicked);
    return page;
}

QWidget* MainWindow::buildGamePage()
{
    auto* page = new QWidget(this);

    statusLabel_ = new QLabel(page);
    QFont statusFont = statusLabel_->font();
    statusFont.setBold(true);
    statusFont.setPointSize(statusFont.pointSize() + 1);
    statusLabel_->setFont(statusFont);

    candidatesLabel_ = new QLabel(page);
    hintsLabel_ = new QLabel(page);

    askButton_ = new QPushButton(QStringLiteral("Задать вопрос"), page);
    hintVarianceButton_ = new QPushButton(QStringLiteral("Подсказка: дисперсия"), page);
    hintRandomForestButton_ = new QPushButton(QStringLiteral("Подсказка: Random Forest"), page);
    hintCatBoostButton_ = new QPushButton(QStringLiteral("Подсказка: CatBoost"), page);
    tableButton_ = new QPushButton(QStringLiteral("Открыть таблицу"), page);
    guessButton_ = new QPushButton(QStringLiteral("Угадать"), page);
    rematchButton_ = new QPushButton(QStringLiteral("Реванш"), page);
    leaveButton_ = new QPushButton(QStringLiteral("Выход"), page);

    logText_ = new QPlainTextEdit(page);
    logText_->setReadOnly(true);

    auto* infoLayout = new QHBoxLayout;
    infoLayout->addWidget(candidatesLabel_);
    infoLayout->addStretch();
    infoLayout->addWidget(hintsLabel_);

    auto* buttons = new QGridLayout;
    buttons->addWidget(askButton_, 0, 0);
    buttons->addWidget(hintVarianceButton_, 0, 1);
    buttons->addWidget(hintRandomForestButton_, 0, 2);
    buttons->addWidget(hintCatBoostButton_, 0, 3);
    buttons->addWidget(tableButton_, 1, 0);
    buttons->addWidget(guessButton_, 1, 1);
    buttons->addWidget(rematchButton_, 1, 2);
    buttons->addWidget(leaveButton_, 1, 3);

    auto* layout = new QVBoxLayout(page);
    layout->addWidget(statusLabel_);
    layout->addLayout(infoLayout);
    layout->addLayout(buttons);
    layout->addWidget(logText_, 1);

    connect(askButton_, &QPushButton::clicked, this, &MainWindow::onAskClicked);
    connect(hintVarianceButton_, &QPushButton::clicked, this, &MainWindow::onVarianceClicked);
    connect(hintRandomForestButton_, &QPushButton::clicked, this,
            &MainWindow::onRandomForestClicked);
    connect(hintCatBoostButton_, &QPushButton::clicked, this, &MainWindow::onCatBoostClicked);
    connect(tableButton_, &QPushButton::clicked, this, &MainWindow::onTableClicked);
    connect(guessButton_, &QPushButton::clicked, this, &MainWindow::onGuessClicked);
    connect(rematchButton_, &QPushButton::clicked, this, &MainWindow::onRematchClicked);
    connect(leaveButton_, &QPushButton::clicked, this, &MainWindow::onLeaveClicked);
    return page;
}

// --- Состояния ------------------------------------------------------------

void MainWindow::applyState(UiState state)
{
    state_ = state;

    const bool canConnect = state == UiState::Disconnected;
    hostField_->setEnabled(canConnect);
    portField_->setEnabled(canConnect);
    connectButton_->setEnabled(canConnect);

    switch (state) {
    case UiState::Disconnected:
    case UiState::Connecting:
        stack_->setCurrentIndex(kConnectPage);
        break;
    case UiState::WaitingOpponent:
        stack_->setCurrentIndex(kSecretPage);
        break;
    default:
        stack_->setCurrentIndex(kGamePage);
        break;
    }

    const bool myTurn = state == UiState::MyTurn;
    const bool inGame = myTurn || state == UiState::WaitResult
                        || state == UiState::OpponentTurn;
    const bool matchOver = state == UiState::MatchOver;
    // Раздел 8 ТЗ: «таблица/угадать» видны в свой ход и во время ожидания ответа
    // (заблокированы), а в ход соперника — скрыты.
    const bool candidatesActionsVisible = myTurn || state == UiState::WaitResult;

    askButton_->setVisible(inGame);
    hintVarianceButton_->setVisible(inGame);
    hintRandomForestButton_->setVisible(inGame);
    hintCatBoostButton_->setVisible(inGame);
    tableButton_->setVisible(candidatesActionsVisible);
    guessButton_->setVisible(candidatesActionsVisible);
    rematchButton_->setVisible(matchOver);
    leaveButton_->setVisible(inGame || matchOver);

    askButton_->setEnabled(myTurn);
    hintVarianceButton_->setEnabled(myTurn && hintsLeftVariance_ > 0);
    hintRandomForestButton_->setEnabled(myTurn && hintsLeftRandomForest_ > 0);
    hintCatBoostButton_->setEnabled(myTurn && hintsLeftCatBoost_ > 0);
    tableButton_->setEnabled(myTurn);
    guessButton_->setEnabled(myTurn);

    updateTurnStatus();
    updateCandidatesLabel();
    updateHintsLabel();
}

void MainWindow::updateTurnStatus()
{
    switch (state_) {
    case UiState::MyTurn:
        statusLabel_->setText(QStringLiteral("Ваш ход"));
        break;
    case UiState::WaitResult:
        statusLabel_->setText(QStringLiteral("Ожидание ответа…"));
        break;
    case UiState::OpponentTurn:
        statusLabel_->setText(QStringLiteral("Ход соперника"));
        break;
    case UiState::MatchOver:
        statusLabel_->setText(QStringLiteral("Партия завершена"));
        break;
    default:
        statusLabel_->clear();
        break;
    }
}

void MainWindow::resetGameData()
{
    askedQuestions_.clear();
    pending_ = PendingRequest::None;
    currentTurn_ = 0;
    waitingAnswer_ = false;
    candidatesCount_ = gamecore::kCharacterCount;
    hintsLeftVariance_ = 1;
    hintsLeftRandomForest_ = 1;
    hintsLeftCatBoost_ = 1;
    logText_->clear();
}

void MainWindow::appendLog(const QString& text)
{
    logText_->appendPlainText(text);
}

void MainWindow::updateCandidatesLabel()
{
    candidatesLabel_->setText(QStringLiteral("Кандидатов: %1").arg(candidatesCount_));
}

void MainWindow::updateHintsLabel()
{
    hintsLabel_->setText(QStringLiteral("Подсказки — дисперсия: %1, Random Forest: %2, CatBoost: %3")
                             .arg(hintsLeftVariance_)
                             .arg(hintsLeftRandomForest_)
                             .arg(hintsLeftCatBoost_));
}

// --- Подключение ----------------------------------------------------------

void MainWindow::onConnectClicked()
{
    QString host = hostField_->text().trimmed();
    if (host.isEmpty()) {
        host = QStringLiteral("127.0.0.1");
        hostField_->setText(host);
    }
    const int port = portField_->value();
    connectStatusLabel_->setText(QStringLiteral("Подключение к %1:%2…").arg(host).arg(port));
    applyState(UiState::Connecting);
    connection_->connectToServer(host, static_cast<quint16>(port));
}

void MainWindow::onConnectionClosed()
{
    // При неудачном подключении текст ошибки уже показан в onServerError.
    if (state_ != UiState::Connecting && state_ != UiState::Disconnected) {
        connectStatusLabel_->setText(QStringLiteral("Соединение с сервером закрыто"));
    }
    secretStatusLabel_->clear();
    applyState(UiState::Disconnected);
}

void MainWindow::onWelcome(int player)
{
    localPlayer_ = player;
    resetGameData();

    searchField_->clear();
    searchField_->setEnabled(true);
    characterList_->setEnabled(true);
    confirmSecretButton_->setEnabled(true);
    if (characterList_->count() > 0) {
        characterList_->setCurrentRow(0);
    }
    secretStatusLabel_->setText(
        QStringLiteral("Вы — игрок %1. Загадайте персонажа для соперника.").arg(player));
    connectStatusLabel_->setText(QStringLiteral("Вы — игрок %1").arg(player));
    applyState(UiState::WaitingOpponent);
}

void MainWindow::onSecretSearchChanged(const QString& text)
{
    filterList(characterList_, text);
}

void MainWindow::onConfirmSecretClicked()
{
    if (state_ != UiState::WaitingOpponent) {
        return;
    }
    QListWidgetItem* item = characterList_->currentItem();
    if (!item || item->isHidden()) {
        secretStatusLabel_->setText(QStringLiteral("Выберите персонажа из списка"));
        return;
    }

    const int id = item->data(Qt::UserRole).toInt();
    connection_->send(proto::makeSetSecret(id));

    confirmSecretButton_->setEnabled(false);
    characterList_->setEnabled(false);
    searchField_->setEnabled(false);
    secretStatusLabel_->setText(
        QStringLiteral("Загадан «%1». Ожидание второго игрока…").arg(item->text()));
}

// --- Игровые события ------------------------------------------------------

void MainWindow::onStart(const QStringList& players)
{
    currentTurn_ = 0;
    waitingAnswer_ = false;
    candidatesCount_ = gamecore::kCharacterCount;
    hintsLeftVariance_ = 1;
    hintsLeftRandomForest_ = 1;
    hintsLeftCatBoost_ = 1;

    logText_->clear();
    appendLog(QStringLiteral("Партия начинается: %1 против %2.")
                  .arg(players.value(0, QStringLiteral("Player 1")),
                       players.value(1, QStringLiteral("Player 2"))));
    appendLog(QStringLiteral("Вы — игрок %1.").arg(localPlayer_));
    applyState(UiState::OpponentTurn);
}

void MainWindow::onTurnChanged(int player)
{
    currentTurn_ = player;
    waitingAnswer_ = false;
    appendLog(player == localPlayer_ ? QStringLiteral("— Ваш ход —")
                                     : QStringLiteral("— Ход соперника —"));
    applyState(player == localPlayer_ ? UiState::MyTurn : UiState::OpponentTurn);
}

void MainWindow::onAnswer(const proto::Message& message)
{
    waitingAnswer_ = false;
    askedQuestions_.insert(message.questionId);
    candidatesCount_ = message.remaining;

    const QString question = questionText(message.questionId);
    const QString answer = answerText(message.answer);

    if (message.hasHint) {
        const QJsonObject hint = message.raw.value(QStringLiteral("hint")).toObject();
        appendLog(QStringLiteral("Подсказка «%1» (metric %2): «%3» → %4 (осталось %5)")
                      .arg(hint.value(QStringLiteral("kind")).toString())
                      .arg(hint.value(QStringLiteral("metric")).toDouble(), 0, 'f', 3)
                      .arg(question, answer)
                      .arg(message.remaining));
    } else {
        appendLog(QStringLiteral("Вы спросили «%1» → %2 (осталось %3)")
                      .arg(question, answer)
                      .arg(message.remaining));
    }

    applyState(UiState::OpponentTurn);
}

void MainWindow::onHintStatus(const QJsonObject& hintsLeft)
{
    hintsLeftVariance_ = hintsLeft.value(QStringLiteral("variance")).toInt();
    hintsLeftRandomForest_ = hintsLeft.value(QStringLiteral("randomforest")).toInt();
    hintsLeftCatBoost_ = hintsLeft.value(QStringLiteral("catboost")).toInt();
    applyState(state_);
}

void MainWindow::onOpponentAsked(int questionId, const QString& answer)
{
    appendLog(QStringLiteral("Соперник спросил «%1» → %2")
                  .arg(questionText(questionId), answerText(answer)));
}

void MainWindow::onCandidates(const QList<int>& characterIds)
{
    candidatesCount_ = characterIds.size();
    updateCandidatesLabel();

    const PendingRequest pending = pending_;
    pending_ = PendingRequest::None;

    if (pending == PendingRequest::Table) {
        openTableDialog(characterIds);
    } else if (pending == PendingRequest::Guess) {
        const int chosen = guessDialog(characterIds);
        if (chosen >= 0) {
            connection_->send(proto::makeGuess(chosen));
            waitingAnswer_ = true;
            applyState(UiState::WaitResult);
        }
    }
}

void MainWindow::onRemoveOk(int remaining)
{
    candidatesCount_ = remaining;
    updateCandidatesLabel();
    appendLog(QStringLiteral("Список кандидатов обновлён, осталось %1").arg(remaining));
}

void MainWindow::onGuessResult(bool correct)
{
    waitingAnswer_ = false;
    if (correct) {
        return; // при верной догадке сервер сразу шлёт matchover
    }
    appendLog(QStringLiteral("Неверная догадка — ход переходит к сопернику."));
    applyState(UiState::OpponentTurn);
}

void MainWindow::onMatchOver(int winner, int characterId, const QString& character)
{
    waitingAnswer_ = false;
    const bool won = winner == localPlayer_;

    if (won) {
        appendLog(QStringLiteral("Победа! Соперник загадал «%1» (#%2).")
                      .arg(character)
                      .arg(characterId));
    } else {
        appendLog(QStringLiteral("Поражение: соперник угадал «%1» (#%2).")
                      .arg(character)
                      .arg(characterId));
    }

    applyState(UiState::MatchOver);
    statusLabel_->setText(won
                              ? QStringLiteral("Вы победили! Соперник загадал «%1»").arg(character)
                              : QStringLiteral("Вы проиграли. Соперник угадал «%1»").arg(character));
}

void MainWindow::onServerError(const QString& text)
{
    appendLog(QStringLiteral("Ошибка: %1").arg(text));

    if (state_ == UiState::Disconnected || state_ == UiState::Connecting) {
        connectStatusLabel_->setText(text);
        return;
    }

    // Сервер отклоняет запрос, не меняя ход (раздел 7 ТЗ): возвращаем себе
    // управление, иначе клиент навсегда остался бы в «Ожидание ответа».
    pending_ = PendingRequest::None;
    waitingAnswer_ = false;
    if (state_ == UiState::WaitResult) {
        applyState(UiState::MyTurn);
    }
    statusLabel_->setText(QStringLiteral("Ошибка: %1").arg(text));
}

void MainWindow::onOpponentDisconnected(const QString& text)
{
    appendLog(QStringLiteral("Соперник отключился: %1").arg(text));
    connectStatusLabel_->setText(text);
    // Сервер закрывает и наше соединение: блокируем игровые действия, чтобы
    // не отправлять запросы в уже мёртвый сокет.
    applyState(UiState::WaitResult);
    statusLabel_->setText(QStringLiteral("Противник отключился"));
}

// --- Диалоги --------------------------------------------------------------

int MainWindow::askQuestionDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Задать вопрос"));
    dialog.resize(560, 520);

    auto* search = new QLineEdit(&dialog);
    search->setPlaceholderText(QStringLiteral("Поиск вопроса…"));

    auto* list = new QListWidget(&dialog);
    for (int id = 0; id < gamecore::kQuestionCount; ++id) {
        if (askedQuestions_.contains(id)) {
            continue;
        }
        auto* item = new QListWidgetItem(questionText(id), list);
        item->setData(Qt::UserRole, id);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Спросить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));

    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(search);
    layout->addWidget(list, 1);
    layout->addWidget(buttons);

    connect(search, &QLineEdit::textChanged, &dialog,
            [list](const QString& text) { filterList(list, text); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dialog,
            [&dialog](QListWidgetItem*) { dialog.accept(); });

    if (list->count() > 0) {
        list->setCurrentRow(0);
    }

    if (dialog.exec() != QDialog::Accepted) {
        return -1;
    }
    QListWidgetItem* item = list->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : -1;
}

void MainWindow::openTableDialog(const QList<int>& characterIds)
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Таблица кандидатов"));
    dialog.resize(440, 540);

    auto* countLabel = new QLabel(&dialog);
    countLabel->setText(QStringLiteral("Кандидатов: %1").arg(characterIds.size()));

    auto* search = new QLineEdit(&dialog);
    search->setPlaceholderText(QStringLiteral("Поиск…"));

    auto* list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (int id : characterIds) {
        auto* item = new QListWidgetItem(characterName(id), list);
        item->setData(Qt::UserRole, id);
    }

    auto* removeButton = new QPushButton(QStringLiteral("Удалить выбранных"), &dialog);
    auto* closeButton = new QPushButton(QStringLiteral("Закрыть"), &dialog);

    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(countLabel);
    layout->addWidget(search);
    layout->addWidget(list, 1);
    auto* row = new QHBoxLayout;
    row->addWidget(removeButton);
    row->addStretch();
    row->addWidget(closeButton);
    layout->addLayout(row);

    connect(search, &QLineEdit::textChanged, &dialog, [list, countLabel](const QString& text) {
        filterList(list, text);
        countLabel->setText(QStringLiteral("Кандидатов: %1").arg(visibleItemCount(list)));
    });
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(removeButton, &QPushButton::clicked, this, [this, list, countLabel]() {
        const QList<QListWidgetItem*> selected = list->selectedItems();
        if (selected.isEmpty()) {
            return;
        }
        std::vector<int> ids;
        ids.reserve(static_cast<std::size_t>(selected.size()));
        for (const QListWidgetItem* item : selected) {
            ids.push_back(item->data(Qt::UserRole).toInt());
        }
        connection_->send(proto::makeRemove(ids));
        for (QListWidgetItem* item : selected) {
            delete list->takeItem(list->row(item));
        }
        countLabel->setText(QStringLiteral("Кандидатов: %1").arg(visibleItemCount(list)));
    });

    dialog.exec();
}

int MainWindow::guessDialog(const QList<int>& characterIds)
{
    if (characterIds.isEmpty()) {
        appendLog(QStringLiteral("Список кандидатов пуст — угадывать некого."));
        return -1;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Угадать персонажа"));
    dialog.resize(440, 520);

    auto* search = new QLineEdit(&dialog);
    search->setPlaceholderText(QStringLiteral("Поиск…"));

    auto* list = new QListWidget(&dialog);
    for (int id : characterIds) {
        auto* item = new QListWidgetItem(characterName(id), list);
        item->setData(Qt::UserRole, id);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Угадать"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));

    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(search);
    layout->addWidget(list, 1);
    layout->addWidget(buttons);

    connect(search, &QLineEdit::textChanged, &dialog,
            [list](const QString& text) { filterList(list, text); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dialog,
            [&dialog](QListWidgetItem*) { dialog.accept(); });

    if (list->count() > 0) {
        list->setCurrentRow(0);
    }

    if (dialog.exec() != QDialog::Accepted) {
        return -1;
    }
    QListWidgetItem* item = list->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : -1;
}

void MainWindow::onAskClicked()
{
    if (state_ != UiState::MyTurn || waitingAnswer_) {
        return;
    }
    const int questionId = askQuestionDialog();
    if (questionId < 0) {
        return;
    }
    connection_->send(proto::makeQuestion(questionId));
    waitingAnswer_ = true;
    applyState(UiState::WaitResult);
}

void MainWindow::onVarianceClicked()
{
    requestHint(proto::HintKind::Variance);
}

void MainWindow::onRandomForestClicked()
{
    requestHint(proto::HintKind::RandomForest);
}

void MainWindow::onCatBoostClicked()
{
    requestHint(proto::HintKind::CatBoost);
}

void MainWindow::requestHint(proto::HintKind kind)
{
    if (state_ != UiState::MyTurn || waitingAnswer_) {
        return;
    }
    connection_->send(proto::makeHint(kind));
    waitingAnswer_ = true;
    applyState(UiState::WaitResult);
}

void MainWindow::onTableClicked()
{
    if (state_ != UiState::MyTurn) {
        return;
    }
    pending_ = PendingRequest::Table;
    connection_->send(proto::makeGetCandidates());
}

void MainWindow::onGuessClicked()
{
    if (state_ != UiState::MyTurn) {
        return;
    }
    pending_ = PendingRequest::Guess;
    connection_->send(proto::makeGetCandidates());
}

void MainWindow::onRematchClicked()
{
    connection_->send(proto::makeRematch());
    resetGameData();

    searchField_->clear();
    searchField_->setEnabled(true);
    characterList_->setEnabled(true);
    confirmSecretButton_->setEnabled(true);
    secretStatusLabel_->setText(QStringLiteral("Загадайте персонажа для реванша."));
    applyState(UiState::WaitingOpponent);
}

void MainWindow::onLeaveClicked()
{
    connection_->send(proto::makeLeave());
    connection_->disconnectFromServer();
    connectStatusLabel_->setText(QStringLiteral("Вы вышли из игры"));
    applyState(UiState::Disconnected);
}

// --- Вспомогательные функции ---------------------------------------------

QString MainWindow::characterName(int id)
{
    if (id < 0 || id >= gamecore::kCharacterCount) {
        return QStringLiteral("#%1").arg(id);
    }
    return QString::fromStdString(gamecore::characterNames()[static_cast<std::size_t>(id)]);
}

QString MainWindow::questionText(int id)
{
    if (id < 0 || id >= gamecore::kQuestionCount) {
        return QStringLiteral("#%1").arg(id);
    }
    return QString::fromStdString(gamecore::questionTexts()[static_cast<std::size_t>(id)]);
}

QString MainWindow::answerText(const QString& answer)
{
    return answer == QStringLiteral("yes") ? QStringLiteral("Да") : QStringLiteral("Нет");
}

