// mainwindow.h — GUI-клиент «Боя Акинаторов»: подключение, выбор секрета, игра.
#pragma once

#include <QList>
#include <QMainWindow>
#include <QSet>
#include <QString>
#include <QStringList>

#include "protocol.h"

class Connection;

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(Connection* connection, const QString& host, int port,
                        const QString& envWarning, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onConnectClicked();
    void onConnectionClosed();
    void onWelcome(int player);
    void onStart(const QStringList& players);
    void onTurnChanged(int player);
    void onAnswer(const proto::Message& message);
    void onHintStatus(const QJsonObject& hintsLeft);
    void onOpponentAsked(int questionId, const QString& answer);
    void onCandidates(const QList<int>& characterIds);
    void onRemoveOk(int remaining);
    void onGuessResult(bool correct);
    void onMatchOver(int winner, int characterId, const QString& character);
    void onServerError(const QString& text);
    void onOpponentDisconnected(const QString& text);

    void onSecretSearchChanged(const QString& text);
    void onConfirmSecretClicked();
    void onAskClicked();
    void onVarianceClicked();
    void onRandomForestClicked();
    void onCatBoostClicked();
    void onTableClicked();
    void onGuessClicked();
    void onRematchClicked();
    void onLeaveClicked();

private:
    enum class UiState {
        Disconnected,
        Connecting,
        WaitingOpponent,
        MyTurn,
        WaitResult,
        OpponentTurn,
        MatchOver
    };

    enum class PendingRequest { None, Table, Guess };

    void buildUi();
    QWidget* buildConnectPage();
    QWidget* buildSecretPage();
    QWidget* buildGamePage();

    void applyState(UiState state);
    void resetGameData();
    void appendLog(const QString& text);
    void updateCandidatesLabel();
    void updateHintsLabel();
    void updateTurnStatus();
    // Картинка состояния (idle/think/happy/hard) в игровой панели.
    void updateStateImage();

    void requestHint(proto::HintKind kind);
    int askQuestionDialog();
    void openTableDialog(const QList<int>& characterIds);
    int guessDialog(const QList<int>& characterIds);

    static QString characterName(int id);
    static QString questionText(int id);
    static QString answerText(const QString& answer);

    Connection* connection_ = nullptr;

    QStackedWidget* stack_ = nullptr;

    // Экран подключения.
    QLineEdit* hostField_ = nullptr;
    QSpinBox* portField_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QLabel* connectStatusLabel_ = nullptr;
    QLabel* connectImage_ = nullptr;

    // Экран выбора персонажа.
    QLineEdit* searchField_ = nullptr;
    QListWidget* characterList_ = nullptr;
    QPushButton* confirmSecretButton_ = nullptr;
    QLabel* secretStatusLabel_ = nullptr;
    QLabel* secretImage_ = nullptr;

    // Экран игры.
    QLabel* statusLabel_ = nullptr;
    QLabel* candidatesLabel_ = nullptr;
    QLabel* hintsLabel_ = nullptr;
    QLabel* stateImage_ = nullptr;
    QPlainTextEdit* logText_ = nullptr;
    QPushButton* askButton_ = nullptr;
    QPushButton* hintVarianceButton_ = nullptr;
    QPushButton* hintRandomForestButton_ = nullptr;
    QPushButton* hintCatBoostButton_ = nullptr;
    QPushButton* tableButton_ = nullptr;
    QPushButton* guessButton_ = nullptr;
    QPushButton* rematchButton_ = nullptr;
    QPushButton* leaveButton_ = nullptr;

    UiState state_ = UiState::Disconnected;
    PendingRequest pending_ = PendingRequest::None;
    int localPlayer_ = 0;
    int currentTurn_ = 0;
    bool waitingAnswer_ = false;
    // true, если прошлым ходом была подсказка: вторую подряд сервер отклонит,
    // поэтому кнопки подсказок гаснут до обычного вопроса/догадки.
    bool hintUsedLastTurn_ = false;
    bool lastMatchWon_ = false;
    int candidatesCount_ = 0;
    QSet<int> askedQuestions_;
    int hintsLeftVariance_ = 0;
    int hintsLeftRandomForest_ = 0;
    int hintsLeftCatBoost_ = 0;
};
