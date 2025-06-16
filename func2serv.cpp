#include "func2serv.h"
#include "DatabaseManager.h"
#include "mytcpserver.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

// Функция формирования JSON-ответа
QByteArray createJsonResponse(const QString &type, const QString &status, const QString &message) {
    QJsonObject jsonObj;
    jsonObj["type"] = type;
    jsonObj["status"] = status;
    jsonObj["message"] = message;
    QJsonDocument doc(jsonObj);
    return doc.toJson(QJsonDocument::Compact) + "\r\n";
}

// Функция парсинга команд
QByteArray parse(const QString &input, MyTcpServer *server) {
    qDebug() << "Received input:" << input;
    QJsonDocument doc = QJsonDocument::fromJson(input.toUtf8());
    if (!doc.isObject()) {
        qDebug() << "Invalid JSON format, input:" << input;
        return createJsonResponse("error", "error", "Invalid JSON format");
    }

    QJsonObject jsonObj = doc.object();
    if (!jsonObj.contains("type")) {
        qDebug() << "Missing type field in JSON, input:" << input;
        return createJsonResponse("error", "error", "Missing type field");
    }

    QString type = jsonObj["type"].toString();
    qDebug() << "Parsed type:" << type;
    if (type == "register") {
        QByteArray response = handleRegister(input);
        QJsonDocument doc = QJsonDocument::fromJson(response);
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj["status"].toString() == "success") {
                QString nickname = jsonObj["nickname"].toString();
                obj["nickname"] = nickname;
                response = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\r\n";
            }
        }
        return response;
    } else if (type == "login") {
        QByteArray response = slotLogin(input);
        QJsonDocument doc = QJsonDocument::fromJson(response);
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj["status"].toString() == "success") {
                QString nickname = jsonObj["nickname"].toString();
                obj["nickname"] = nickname;
                response = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\r\n";
            }
        }
        return response;
    } else if (type == "start_game") {
        return handleStartGame(input, server);
    } else if (type == "place_ship") {
        return handlePlaceShip(input, server);
    } else if (type == "make_move") {
        return handleMakeMove(input, server);
    } else if (type == "ready_to_battle") {
        return createJsonResponse("ready_to_battle", "success", "Ready status received");
    }

    qDebug() << "Unknown command type:" << type;
    return createJsonResponse("error", "error", "Unknown command");
}

bool parseRegisterData(const QString &data, QString &nickname, QString &email, QString &password) {
    QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
    if (!doc.isObject()) return false;

    QJsonObject jsonObj = doc.object();
    if (!jsonObj.contains("nickname") || !jsonObj.contains("email") || !jsonObj.contains("password")) return false;

    nickname = jsonObj["nickname"].toString();
    email = jsonObj["email"].toString();
    password = jsonObj["password"].toString();

    if (nickname.isEmpty() || email.isEmpty() || password.isEmpty()) return false;

    qDebug() << "Parsed register data - Nickname:" << nickname << "Email:" << email << "Password:" << password;
    return true;
}

bool parseLoginData(const QString &data, QString &nickname, QString &password) {
    QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
    if (!doc.isObject()) return false;

    QJsonObject jsonObj = doc.object();
    if (!jsonObj.contains("nickname") || !jsonObj.contains("password")) return false;

    nickname = jsonObj["nickname"].toString();
    password = jsonObj["password"].toString();

    if (nickname.isEmpty() || password.isEmpty()) return false;

    qDebug() << "Parsed login data - Nickname:" << nickname << "Password:" << password;
    return true;
}

QByteArray handleRegister(const QString &data) {
    QString nickname, email, password;
    if (!parseRegisterData(data, nickname, email, password)) {
        return createJsonResponse("register", "error", "Invalid registration data");
    }

    DatabaseManager *db = DatabaseManager::getInstance();
    QSqlDatabase database = db->getDatabase();
    if (!database.isOpen()) {
        qDebug() << "Database is not open in handleRegister";
        return createJsonResponse("register", "error", "Database is not open");
    }

    QSqlQuery query(database);
    query.prepare("SELECT COUNT(*) FROM User WHERE nickname = :nickname OR email = :email");
    query.bindValue(":nickname", nickname);
    query.bindValue(":email", email);

    qDebug() << "Executing query in handleRegister: SELECT COUNT(*) FROM User WHERE nickname =" << nickname << "OR email =" << email;

    if (!query.exec()) {
        qDebug() << "Database query failed (SELECT) in handleRegister:" << query.lastError().text();
        return createJsonResponse("register", "error", "Database query failed");
    }

    query.next();
    if (query.value(0).toInt() > 0) {
        return createJsonResponse("register", "error", "User already exists");
    }

    bool success = db->addUser(nickname, email, password);
    if (success) {
        return createJsonResponse("register", "success", "User registered successfully");
    } else {
        return createJsonResponse("register", "error", "Registration failed");
    }
}

QByteArray slotLogin(const QString &data) {
    QString nickname, password;
    if (!parseLoginData(data, nickname, password)) {
        return createJsonResponse("login", "error", "Invalid login data");
    }

    DatabaseManager *db = DatabaseManager::getInstance();
    QSqlDatabase database = db->getDatabase();
    if (!database.isOpen()) {
        qDebug() << "Database is not open in slotLogin";
        return createJsonResponse("login", "error", "Database is not open");
    }

    QSqlQuery query(database);
    query.prepare("SELECT * FROM User WHERE nickname = :nickname AND password = :password");
    query.bindValue(":nickname", nickname);
    query.bindValue(":password", password);

    qDebug() << "Executing query in slotLogin: SELECT * FROM User WHERE nickname =" << nickname << "AND password =" << password;

    if (!query.exec()) {
        qDebug() << "Database query failed (SELECT) in slotLogin:" << query.lastError().text();
        return createJsonResponse("login", "error", "Database query failed");
    }

    if (query.next()) {
        qDebug() << "Login successful";
        return createJsonResponse("login", "success", "Login successful");
    } else {
        qDebug() << "Login error";
        return createJsonResponse("login", "error", "Invalid nickname or password");
    }
}

QByteArray handleStartGame(const QString &data, MyTcpServer *server) {
    QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
    if (!doc.isObject()) {
        return createJsonResponse("start_game", "error", "Invalid JSON format");
    }

    QJsonObject jsonObj = doc.object();
    QString nickname = jsonObj["nickname"].toString();
    if (nickname.isEmpty()) {
        return createJsonResponse("start_game", "error", "Missing nickname");
    }

    if (!server) {
        return createJsonResponse("start_game", "error", "Server error");
    }

    server->addPlayerToGame(nickname);
    if (server->getPlayerCount() == 2) {
        QString opponent = server->getOpponent(nickname);
        DatabaseManager *db = DatabaseManager::getInstance();
        int gameId = db->createGame(nickname, opponent);
        if (gameId != -1) {
            server->currentGameId = gameId;
            QJsonObject responseObj;
            responseObj["type"] = "game_ready";
            responseObj["status"] = "success";
            responseObj["message"] = "Please place your ships and confirm readiness";
            responseObj["game_id"] = gameId;
            responseObj["opponent"] = opponent;
            QByteArray response = QJsonDocument(responseObj).toJson(QJsonDocument::Compact) + "\r\n";
            server->sendMessageToUser(nickname, response);

            responseObj["opponent"] = nickname;
            response = QJsonDocument(responseObj).toJson(QJsonDocument::Compact) + "\r\n";
            server->sendMessageToUser(opponent, response);
        } else {
            return createJsonResponse("start_game", "error", "Failed to create game");
        }
    }

    return createJsonResponse("start_game", "waiting", "Waiting for opponent");
}

QByteArray handlePlaceShip(const QString &data, MyTcpServer *server) {
    QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
    if (!doc.isObject()) {
        return createJsonResponse("place_ship", "error", "Invalid JSON format");
    }

    QJsonObject jsonObj = doc.object();
    if (!jsonObj.contains("nickname") || !jsonObj.contains("game_id") || !jsonObj.contains("x") ||
        !jsonObj.contains("y") || !jsonObj.contains("size") || !jsonObj.contains("is_horizontal")) {
        return createJsonResponse("place_ship", "error", "Missing required fields");
    }

    QString nickname = jsonObj["nickname"].toString();
    int gameId = jsonObj["game_id"].toInt();
    int x = jsonObj["x"].toInt();
    int y = jsonObj["y"].toInt();
    int size = jsonObj["size"].toInt();
    bool isHorizontal = jsonObj["is_horizontal"].toBool();

    if (nickname.isEmpty()) {
        return createJsonResponse("place_ship", "error", "Invalid nickname");
    }

    if (gameId != server->getGameId()) {
        return createJsonResponse("place_ship", "error", "Invalid game ID");
    }

    // Проверка корректности координат и размера
    if (x < 0 || y < 0 || size < 1 || size > 4 || x >= 10 || y >= 10) {
        return createJsonResponse("place_ship", "error", "Invalid ship coordinates or size");
    }
    if (isHorizontal && x + size > 10) {
        return createJsonResponse("place_ship", "error", "Ship exceeds horizontal board limits");
    }
    if (!isHorizontal && y + size > 10) {
        return createJsonResponse("place_ship", "error", "Ship exceeds vertical board limits");
    }

    DatabaseManager *db = DatabaseManager::getInstance();
    if (db->saveShip(gameId, nickname, x, y, size, isHorizontal)) {
        qDebug() << "Ship placed successfully for" << nickname << ": game_id=" << gameId
                 << ", x=" << x << ", y=" << y << ", size=" << size << ", is_horizontal=" << isHorizontal;
        return createJsonResponse("place_ship", "success", "Ship placed successfully");
    } else {
        qDebug() << "Failed to place ship for" << nickname << ": game_id=" << gameId;
        return createJsonResponse("place_ship", "error", "Failed to place ship");
    }
}

QByteArray handleMakeMove(const QString &data, MyTcpServer *server) {
    QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
    if (!doc.isObject()) {
        return createJsonResponse("make_move", "error", "Invalid JSON format");
    }

    QJsonObject jsonObj = doc.object();
    if (!jsonObj.contains("nickname") || !jsonObj.contains("game_id") || !jsonObj.contains("x") || !jsonObj.contains("y")) {
        return createJsonResponse("make_move", "error", "Missing required fields");
    }

    QString nickname = jsonObj["nickname"].toString();
    int gameId = jsonObj["game_id"].toInt();
    int x = jsonObj["x"].toInt();
    int y = jsonObj["y"].toInt();

    if (gameId != server->getGameId()) {
        return createJsonResponse("make_move", "error", "Invalid game ID");
    }

    DatabaseManager *db = DatabaseManager::getInstance();
    QString currentTurn = db->getCurrentTurn(gameId);
    if (currentTurn != nickname) {
        return createJsonResponse("make_move", "error", "Not your turn");
    }

    QString result = db->checkMove(gameId, nickname, x, y);
    if (result == "error") {
        return createJsonResponse("make_move", "error", "Failed to process move");
    }

    if (result == "already_shot") {
        return createJsonResponse("make_move", "error", "Cell already shot");
    }

    db->saveMove(gameId, nickname, x, y, result);

    QString opponent = server->getOpponent(nickname);
    QString nextTurn = (result != "hit" && result != "sunk") ? opponent : nickname;
    if (result != "hit" && result != "sunk") {
        db->updateTurn(gameId, opponent);
    }

    QJsonObject response;
    response["type"] = "make_move";
    response["status"] = result;
    response["message"] = result == "sunk" ? "Ship sunk!" : result == "hit" ? "Hit!" : "Miss!";
    response["x"] = x;
    response["y"] = y;
    response["current_turn"] = nextTurn;

    QJsonObject opponentResponse;
    opponentResponse["type"] = "move_result";
    opponentResponse["status"] = result;
    opponentResponse["x"] = x;
    opponentResponse["y"] = y;
    opponentResponse["message"] = result == "sunk" ? "Your ship was sunk!" : result == "hit" ? "Your ship was hit!" : "Opponent missed!";
    opponentResponse["current_turn"] = nextTurn;

    server->sendMessageToUser(opponent, QJsonDocument(opponentResponse).toJson(QJsonDocument::Compact) + "\r\n");

    return QJsonDocument(response).toJson(QJsonDocument::Compact) + "\r\n";
}
