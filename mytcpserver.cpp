#include "mytcpserver.h"
#include "func2serv.h"
#include "DatabaseManager.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

MyTcpServer::MyTcpServer(QObject *parent) : QObject(parent), currentGameId(-1)
{
    mTcpServer = new QTcpServer(this);
    connect(mTcpServer, &QTcpServer::newConnection, this, &MyTcpServer::slotNewConnection);

    if (!mTcpServer->listen(QHostAddress::Any, 33333)) {
        qDebug() << "Server is NOT started!";
    } else {
        qDebug() << "Server is started!";
    }
}

MyTcpServer::~MyTcpServer()
{
    mTcpServer->close();
}

void MyTcpServer::slotNewConnection()
{
    QTcpSocket *clientSocket = mTcpServer->nextPendingConnection();
    if (clientSocket) {
        if (getPlayerCount() >= 2) {
            QByteArray response = createJsonResponse("error", "error", "Server is full");
            clientSocket->write(response);
            clientSocket->flush();
            clientSocket->disconnectFromHost();
            return;
        }

        connect(clientSocket, &QTcpSocket::readyRead, this, &MyTcpServer::slotServerRead);
        connect(clientSocket, &QTcpSocket::disconnected, this, &MyTcpServer::slotClientDisconnected);
        qDebug() << "New client connected from" << clientSocket->peerAddress().toString();
    }
}

void MyTcpServer::slotServerRead()
{
    QTcpSocket *clientSocket = qobject_cast<QTcpSocket*>(sender());
    if (!clientSocket) {
        qDebug() << "Invalid client socket in slotServerRead";
        return;
    }

    QByteArray requestData = clientSocket->readAll();
    QString request = QString::fromUtf8(requestData).trimmed();

    qDebug() << "Received raw request:" << requestData.toHex();
    qDebug() << "Received input (parsed):" << request;
    QByteArray response;

    QJsonDocument doc = QJsonDocument::fromJson(request.toUtf8());
    if (doc.isObject()) {
        QJsonObject jsonObj = doc.object();
        QString type = jsonObj["type"].toString();
        QString nickname = jsonObj["nickname"].toString();

        if (type == "register" || type == "login") {
            if (!nickname.isEmpty()) {
                registerClient(nickname, clientSocket);
                response = parse(request, this);
                qDebug() << "Clients registered:" << mClients.keys();
            } else {
                response = createJsonResponse("error", "error", "Nickname is empty");
            }
        } else if (type == "ready_to_battle") {
            if (!nickname.isEmpty() && players.contains(nickname)) {
                QMutexLocker locker(&mutex);
                qDebug() << "Processing ready_to_battle for" << nickname << "- currentGameId:" << currentGameId << "- Socket state:" << clientSocket->state();
                readyPlayers.insert(nickname);
                qDebug() << "Player" << nickname << "is ready. Ready players:" << readyPlayers;
                response = createJsonResponse("ready_to_battle", "success", "Ready status received");
                if (readyPlayers.size() == 2 && currentGameId != -1) {
                    qDebug() << "Both players ready, starting game with gameId:" << currentGameId;
                    DatabaseManager *db = DatabaseManager::getInstance();
                    QString player1 = players[0];
                    db->updateTurn(currentGameId, player1);
                    QJsonObject startMsg;
                    startMsg["type"] = "game_start";
                    startMsg["status"] = "success";
                    startMsg["message"] = "Game started";
                    startMsg["current_turn"] = player1;
                    QByteArray startResponse = QJsonDocument(startMsg).toJson(QJsonDocument::Compact) + "\r\n";
                    qDebug() << "Prepared game_start message:" << startResponse;
                    for (const QString &player : mClients.keys()) {
                        QTcpSocket *targetSocket = mClients[player];
                        qDebug() << "Attempting to send to" << player << "- Socket state:" << targetSocket->state();
                        if (targetSocket->state() == QAbstractSocket::ConnectedState) {
                            bool success = targetSocket->write(startResponse);
                            targetSocket->flush();
                            if (success) {
                                qDebug() << "Successfully sent game_start to" << player;
                            } else {
                                qDebug() << "Failed to send game_start to" << player << "- Error:" << targetSocket->errorString();
                            }
                        } else {
                            qDebug() << "Cannot send to" << player << "- Socket not connected, state:" << targetSocket->state();
                        }
                    }
                }
            } else {
                response = createJsonResponse("error", "error", "Player not registered");
            }
        } else if (type == "make_move") {
            int gameId = jsonObj["game_id"].toInt();
            int x = jsonObj["x"].toInt();
            int y = jsonObj["y"].toInt();
            qDebug() << "Processing make_move for" << nickname << "in game" << gameId << "at (" << x << "," << y << ")";

            DatabaseManager *db = DatabaseManager::getInstance();
            QString currentTurn = db->getCurrentTurn(gameId);
            qDebug() << "Current turn for game" << gameId << "is" << currentTurn;
            if (currentTurn != nickname) {
                response = createJsonResponse("error", "error", "Not your turn");
                qDebug() << "Move rejected: not" << nickname << "'s turn, current turn is" << currentTurn;
            } else {
                QString result = db->checkMove(gameId, nickname, x, y);
                qDebug() << "Move result for" << nickname << ":" << result;

                if (result == "error") {
                    response = createJsonResponse("error", "error", "Failed to process move");
                    qDebug() << "Move processing failed for" << nickname;
                } else if (result == "already_shot") {
                    response = createJsonResponse("error", "error", "Cell already shot");
                    qDebug() << "Move rejected: cell (" << x << "," << y << ") already shot by" << nickname;
                } else {
                    QJsonObject moveResponse;
                    moveResponse["type"] = "make_move";
                    moveResponse["status"] = result;
                    moveResponse["message"] = "Move processed";
                    moveResponse["x"] = x;
                    moveResponse["y"] = y;

                    QJsonObject opponentResponse;
                    opponentResponse["type"] = "move_result";
                    opponentResponse["status"] = result;
                    opponentResponse["x"] = x;
                    opponentResponse["y"] = y;
                    opponentResponse["message"] = "Opponent made a move";

                    // Обновляем счётчик потопленных кораблей
                    //QMutexLocker locker(&mutex);
                    if (result == "sunk") {
                        sunkShips[nickname] = sunkShips.value(nickname, 0) + 1;
                        qDebug() << nickname << "has sunk" << sunkShips[nickname] << "ships";
                    }
                    if (sunkShips[nickname] >= 10) {
                        QJsonObject gameOverMsg;
                        gameOverMsg["type"] = "game_over";
                        gameOverMsg["status"] = "success";
                        gameOverMsg["message"] = QString("%1 победил! Игра окончена.").arg(nickname);
                        gameOverMsg["winner"] = nickname;
                        QByteArray gameOverResponse = QJsonDocument(gameOverMsg).toJson(QJsonDocument::Compact) + "\r\n";

                        // Отправляем сообщение game_over обоим игрокам
                        QString opponent = getOpponent(nickname);
                        if (!opponent.isEmpty()) {
                            sendMessageToUser(nickname, gameOverResponse);
                            sendMessageToUser(opponent, gameOverResponse);
                            qDebug() << "Game over: " << nickname << " has sunk 10 ships. Sent game_over to both players.";
                        } else {
                            qDebug() << "Opponent not found for " << nickname << ", sending game_over only to " << nickname;
                            sendMessageToUser(nickname, gameOverResponse);
                        }

                        // Сбрасываем игру
                        resetGame();
                        response = gameOverResponse;
                    }

                    // Обновляем current_turn только один раз
                    QString opponent = getOpponent(nickname);
                    if (!opponent.isEmpty()) {
                        if (result != "hit" && result != "sunk") {
                            db->updateTurn(gameId, opponent);
                            qDebug() << "Turn updated to" << opponent << "for game" << gameId;
                        }
                        moveResponse["current_turn"] = (result != "hit" && result != "sunk") ? opponent : currentTurn;
                        opponentResponse["current_turn"] = (result != "hit" && result != "sunk") ? opponent : currentTurn;
                    } else {
                        qDebug() << "Opponent not found for" << nickname;
                        moveResponse["current_turn"] = currentTurn;
                        opponentResponse["current_turn"] = currentTurn;
                    }

                    // Отправляем ответы
                    response = QJsonDocument(moveResponse).toJson(QJsonDocument::Compact) + "\r\n";
                    qDebug() << "Prepared response for" << nickname << ":" << response;

                    if (!opponent.isEmpty()) {
                        QByteArray opponentMessage = QJsonDocument(opponentResponse).toJson(QJsonDocument::Compact) + "\r\n";
                        qDebug() << "Sending move_result to" << opponent << ":" << opponentMessage;
                        sendMessageToUser(opponent, opponentMessage);
                    } else {
                        qDebug() << "Opponent not found for" << nickname << "in game" << gameId;
                    }
                }
            }
        } else {
            response = parse(request, this);
            qDebug() << "Processed other request type:" << type << ", response:" << response;
        }
    } else {
        qDebug() << "Failed to parse JSON for request:" << request;
        response = createJsonResponse("error", "error", "Invalid JSON format");
    }

    if (clientSocket->state() == QAbstractSocket::ConnectedState) {
        qDebug() << "Sending response to" << getNicknameBySocket(clientSocket) << ". Response:" << response;
        clientSocket->write(response);
        clientSocket->flush();
    } else {
        qDebug() << "Cannot send response to" << getNicknameBySocket(clientSocket) << ", socket state:" << clientSocket->state();
    }
}

void MyTcpServer::slotClientDisconnected()
{
    QTcpSocket *clientSocket = qobject_cast<QTcpSocket*>(sender());
    if (clientSocket) {
        QString nickname = getNicknameBySocket(clientSocket);
        if (!nickname.isEmpty()) {
            QMutexLocker locker(&mutex);
            readyPlayers.remove(nickname);
            sunkShips.remove(nickname); // Удаляем счётчик при отключении
            unregisterClient(clientSocket);
            qDebug() << "Client" << nickname << "disconnected! Socket state:" << clientSocket->state();
        }
        clientSocket->deleteLater();
    }
}

void MyTcpServer::sendMessageToUser(const QString &nickname, const QByteArray &message)
{
    QMutexLocker locker(&mutex);
    if (mClients.contains(nickname)) {
        QTcpSocket *socket = mClients[nickname];
        if (socket && socket->state() == QAbstractSocket::ConnectedState && socket->isValid()) {
            qint64 bytesWritten = socket->write(message);
            if (bytesWritten == -1) {
                qDebug() << "Failed to write to socket for" << nickname << "- Error:" << socket->errorString();
            } else {
                // Не используем flush, чтобы избежать блокировки
                qDebug() << "Message queued for" << nickname << ":" << message;
            }
        } else {
            qDebug() << "Socket for" << nickname << "is invalid or not connected. State:" << (socket ? socket->state() : -1);
        }
    } else {
        qDebug() << "User" << nickname << "not found or not connected. Current clients:" << mClients.keys();
    }
}

void MyTcpServer::registerClient(const QString &nickname, QTcpSocket *socket)
{
    QMutexLocker locker(&mutex);
    mClients.insert(nickname, socket);
    mSocketToNickname.insert(socket, nickname);
    sunkShips.insert(nickname, 0); // Инициализируем счётчик потопленных кораблей
    qDebug() << "Registered client:" << nickname << "Socket state:" << socket->state();
}

void MyTcpServer::unregisterClient(QTcpSocket *socket)
{
    QMutexLocker locker(&mutex);
    QString nickname = mSocketToNickname.value(socket, "");
    if (!nickname.isEmpty()) {
        mClients.remove(nickname);
        mSocketToNickname.remove(socket);
        players.removeAll(nickname);
        readyPlayers.remove(nickname);
        sunkShips.remove(nickname); // Удаляем счётчик при отключении
        QString opponent = getOpponentInternal(nickname);
        if (!opponent.isEmpty()) {
            sendMessageToUser(opponent, createJsonResponse("gameover", "opponent_disconnected", "Opponent disconnected"));
            resetGame();
        }
    }
}

QString MyTcpServer::getNicknameBySocket(QTcpSocket *socket)
{
    QMutexLocker locker(&mutex);
    return mSocketToNickname.value(socket, "");
}

void MyTcpServer::addPlayerToGame(const QString &nickname)
{
    QMutexLocker locker(&mutex);
    if (!players.contains(nickname)) {
        players.append(nickname);
        sunkShips.insert(nickname, 0); // Инициализируем счётчик при добавлении игрока
        qDebug() << "Added player to game:" << nickname;
    }
}

QString MyTcpServer::getOpponent(const QString &nickname) const
{
    return const_cast<MyTcpServer*>(this)->getOpponentInternal(nickname);
}

QString MyTcpServer::getOpponentInternal(const QString &nickname)
{
    QMutexLocker locker(&mutex);
    for (const QString &player : players) {
        if (player != nickname) {
            return player;
        }
    }
    return "";
}

int MyTcpServer::getPlayerCount() const
{
    return const_cast<MyTcpServer*>(this)->getPlayerCountInternal();
}

int MyTcpServer::getPlayerCountInternal()
{
    QMutexLocker locker(&mutex);
    return players.size();
}

void MyTcpServer::resetGame()
{
    QMutexLocker locker(&mutex);
    players.clear();
    readyPlayers.clear();
    sunkShips.clear(); // Очищаем счётчики потопленных кораблей
    currentGameId = -1;
    qDebug() << "Game reset.";
}

int MyTcpServer::getGameId() const
{
    return currentGameId;
}

int MyTcpServer::getSunkShips(const QString &nickname) const
{
    QMutexLocker locker(&mutex);
    return sunkShips.value(nickname, 0);
}
