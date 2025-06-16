#include "DatabaseManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QMutex>
#include <QSqlRecord>

DatabaseManager* DatabaseManager::instance = nullptr;
QMutex mutex;

DatabaseManager::DatabaseManager()
{
    if (!QSqlDatabase::drivers().contains("QSQLITE")) {
        qDebug() << "Error: SQLite driver not available!";
    } else {
        qDebug() << "SQLite driver is available.";
    }

    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName("server_db.sqlite");
    db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=1000"); // Устанавливаем тайм-аут 1 сек
    qDebug() << "Attempting to open database at:" << db.databaseName();

    if (!db.open()) {
        qDebug() << "Error opening DB:" << db.lastError().text();
    } else {
        qDebug() << "Database connected successfully!";
        QSqlQuery query(db);

        bool success = query.exec("CREATE TABLE IF NOT EXISTS User ("
                                  "nickname TEXT PRIMARY KEY, "
                                  "email TEXT NOT NULL UNIQUE, "
                                  "password TEXT NOT NULL, "
                                  "connection_info TEXT)");
        if (!success) {
            qDebug() << "Error creating table User:" << query.lastError().text();
        } else {
            qDebug() << "Table User created or already exists.";
        }

        success = query.exec("CREATE TABLE IF NOT EXISTS Game ("
                             "game_id INTEGER PRIMARY KEY AUTOINCREMENT, "
                             "player1 TEXT NOT NULL, "
                             "player2 TEXT NOT NULL, "
                             "current_turn TEXT NOT NULL, "
                             "FOREIGN KEY(player1) REFERENCES User(nickname), "
                             "FOREIGN KEY(player2) REFERENCES User(nickname))");
        if (!success) {
            qDebug() << "Error creating table Game:" << query.lastError().text();
        } else {
            qDebug() << "Table Game created or already exists.";
        }

        success = query.exec("CREATE TABLE IF NOT EXISTS Ship ("
                             "ship_id INTEGER PRIMARY KEY AUTOINCREMENT, "
                             "game_id INTEGER NOT NULL, "
                             "player TEXT NOT NULL, "
                             "x INTEGER NOT NULL, "
                             "y INTEGER NOT NULL, "
                             "size INTEGER NOT NULL, "
                             "is_horizontal INTEGER NOT NULL, "
                             "FOREIGN KEY(game_id) REFERENCES Game(game_id), "
                             "FOREIGN KEY(player) REFERENCES User(nickname))");
        if (!success) {
            qDebug() << "Error creating table Ship:" << query.lastError().text();
        } else {
            qDebug() << "Table Ship created or already exists.";
        }

        success = query.exec("CREATE TABLE IF NOT EXISTS Move ("
                             "move_id INTEGER PRIMARY KEY AUTOINCREMENT, "
                             "game_id INTEGER NOT NULL, "
                             "player TEXT NOT NULL, "
                             "x INTEGER NOT NULL, "
                             "y INTEGER NOT NULL, "
                             "result TEXT NOT NULL, "
                             "FOREIGN KEY(game_id) REFERENCES Game(game_id), "
                             "FOREIGN KEY(player) REFERENCES User(nickname))");
        if (!success) {
            qDebug() << "Error creating table Move:" << query.lastError().text();
        } else {
            qDebug() << "Table Move created or already exists.";
        }
    }
}

DatabaseManager::~DatabaseManager()
{
    if (db.isOpen()) {
        db.close();
    }
    instance = nullptr;
}

DatabaseManager* DatabaseManager::getInstance()
{
    if (!instance) {
        instance = new DatabaseManager();
    }
    return instance;
}

QSqlDatabase DatabaseManager::getDatabase()
{
    return db;
}

bool DatabaseManager::addUser(const QString &nickname, const QString &email, const QString &password)
{
    QMutexLocker locker(&mutex);
    if (!db.isOpen()) {
        qDebug() << "Database is not open!";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("INSERT INTO User (nickname, email, password, connection_info) VALUES (:nickname, :email, :password, :connection_info)");
    query.bindValue(":nickname", nickname);
    query.bindValue(":email", email);
    query.bindValue(":password", password);
    query.bindValue(":connection_info", "");

    qDebug() << "Adding user - Nickname:" << nickname << "Email:" << email;

    if (!query.exec()) {
        qDebug() << "Error adding user:" << query.lastError().text();
        return false;
    }
    qDebug() << "User added successfully.";
    return true;
}

void DatabaseManager::printUsers()
{
    QMutexLocker locker(&mutex);
    QSqlQuery query(db);
    if (!query.exec("SELECT * FROM User")) {
        qDebug() << "Error fetching users:" << query.lastError().text();
        return;
    }
    while (query.next()) {
        qDebug() << "Nickname:" << query.value("nickname").toString()
        << "Email:" << query.value("email").toString()
        << "Password:" << query.value("password").toString()
        << "Connection:" << query.value("connection_info").toString();
    }
}

int DatabaseManager::createGame(const QString &player1, const QString &player2)
{
    QMutexLocker locker(&mutex);
    if (!db.isOpen()) {
        qDebug() << "Database is not open!";
        return -1;
    }

    QSqlQuery query(db);
    query.prepare("INSERT INTO Game (player1, player2, current_turn) VALUES (:player1, :player2, :current_turn)");
    query.bindValue(":player1", player1);
    query.bindValue(":player2", player2);
    query.bindValue(":current_turn", player1);

    if (!query.exec()) {
        qDebug() << "Error creating game:" << query.lastError().text();
        return -1;
    }

    query.exec("SELECT last_insert_rowid()");
    if (query.next()) {
        int gameId = query.value(0).toInt();
        qDebug() << "Game created with ID:" << gameId << "between" << player1 << "and" << player2;
        return gameId;
    }
    return -1;
}

bool DatabaseManager::saveShip(int gameId, const QString &player, int x, int y, int size, bool isHorizontal)
{
    QMutexLocker locker(&mutex);
    if (!db.isOpen()) {
        qDebug() << "Database is not open!";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("INSERT INTO Ship (game_id, player, x, y, size, is_horizontal) VALUES (:game_id, :player, :x, :y, :size, :is_horizontal)");
    query.bindValue(":game_id", gameId);
    query.bindValue(":player", player);
    query.bindValue(":x", x);
    query.bindValue(":y", y);
    query.bindValue(":size", size);
    query.bindValue(":is_horizontal", isHorizontal ? 1 : 0);

    if (!query.exec()) {
        qDebug() << "Error saving ship:" << query.lastError().text();
        return false;
    }
    qDebug() << "Ship saved for player" << player << "in game" << gameId;
    return true;
}

bool DatabaseManager::saveMove(int gameId, const QString &player, int x, int y, const QString &result)
{
    // Мьютекс уже заблокирован в вызывающей функции (checkMove), поэтому здесь не блокируем
    if (!db.isOpen()) {
        qDebug() << "Database is not open in saveMove!";
        return false;
    }

    qDebug() << "Starting saveMove for player" << player << "in game" << gameId << "at (" << x << "," << y << ") with result:" << result;

    QSqlQuery query(db);
    query.prepare("INSERT INTO Move (game_id, player, x, y, result) VALUES (:game_id, :player, :x, :y, :result)");
    query.bindValue(":game_id", gameId);
    query.bindValue(":player", player);
    query.bindValue(":x", x);
    query.bindValue(":y", y);
    query.bindValue(":result", result);

    qDebug() << "SQL query:" << query.lastQuery();
    qDebug() << "Bound values:" << query.boundValues();

    try {
        if (!query.exec()) {
            qDebug() << "Error saving move:" << query.lastError().text();
            return false;
        }
    } catch (const std::exception &e) {
        qDebug() << "Exception in saveMove:" << e.what();
        return false;
    }

    qDebug() << "Move saved: Player" << player << "in game" << gameId << "at (" << x << "," << y << ") - Result:" << result;
    return true;
}

QString DatabaseManager::checkMove(int gameId, const QString &player, int x, int y)
{
    QMutexLocker locker(&mutex);
    if (!db.isOpen()) {
        qDebug() << "Database is not open!";
        return "error";
    }

    qDebug() << "Starting checkMove for player" << player << "in game" << gameId << "at (" << x << "," << y << ")";

    if (!db.transaction()) {
        qDebug() << "Failed to start transaction in checkMove:" << db.lastError().text();
        return "error";
    }

    // Получаем оппонента
    QSqlQuery gameQuery(db);
    gameQuery.prepare("SELECT player1, player2 FROM Game WHERE game_id = :game_id");
    gameQuery.bindValue(":game_id", gameId);
    if (!gameQuery.exec() || !gameQuery.next()) {
        qDebug() << "Error fetching game:" << gameQuery.lastError().text();
        db.rollback();
        return "error";
    }

    QString player1 = gameQuery.value("player1").toString();
    QString player2 = gameQuery.value("player2").toString();
    QString opponent = (player == player1) ? player2 : player1;
    qDebug() << "Opponent for" << player << "is" << opponent;

    // Проверяем, не стреляли ли уже в эту клетку
    QSqlQuery moveQuery(db);
    moveQuery.prepare("SELECT result FROM Move WHERE game_id = :game_id AND player = :player AND x = :x AND y = :y");
    moveQuery.bindValue(":game_id", gameId);
    moveQuery.bindValue(":player", player);
    moveQuery.bindValue(":x", x);
    moveQuery.bindValue(":y", y);
    if (moveQuery.exec() && moveQuery.next()) {
        qDebug() << "Cell (" << x << "," << y << ") already shot by" << player;
        db.commit();
        return "already_shot";
    }

    // Проверяем, есть ли корабль оппонента в этой клетке
    QSqlQuery shipQuery(db);
    shipQuery.prepare("SELECT ship_id, x, y, size, is_horizontal FROM Ship WHERE game_id = :game_id AND player = :player");
    shipQuery.bindValue(":game_id", gameId);
    shipQuery.bindValue(":player", opponent);
    if (!shipQuery.exec()) {
        qDebug() << "Error fetching ships:" << shipQuery.lastError().text();
        db.rollback();
        return "error";
    }

    bool hit = false;
    int shipId = -1;
    int shipSize = 0;
    int shipX = 0, shipY = 0;
    bool isHorizontal = false;
    while (shipQuery.next()) {
        shipX = shipQuery.value(1).toInt();
        shipY = shipQuery.value(2).toInt();
        shipSize = shipQuery.value(3).toInt();
        isHorizontal = shipQuery.value(4).toBool();
        shipId = shipQuery.value(0).toInt();
        qDebug() << "Checking ship: id=" << shipId << ", x=" << shipX << ", y=" << shipY << ", size=" << shipSize << ", is_horizontal=" << isHorizontal;

        if (isHorizontal) {
            if (y == shipY && x >= shipX && x < shipX + shipSize) {
                hit = true;
                break;
            }
        } else {
            if (x == shipX && y >= shipY && y < shipY + shipSize) {
                hit = true;
                break;
            }
        }
    }

    QString result;
    if (hit) {
        QSqlQuery hitQuery(db);
        hitQuery.prepare("SELECT COUNT(*) FROM Move WHERE game_id = :game_id AND player = :player AND result IN ('hit', 'sunk') AND "
                         "(x >= :ship_x AND x < :ship_x + :size AND y = :ship_y AND :is_horizontal = 1 OR "
                         "y >= :ship_y AND y < :ship_y + :size AND x = :ship_x AND :is_horizontal = 0)");
        hitQuery.bindValue(":game_id", gameId);
        hitQuery.bindValue(":player", player);
        hitQuery.bindValue(":ship_x", shipX);
        hitQuery.bindValue(":ship_y", shipY);
        hitQuery.bindValue(":size", shipSize);
        hitQuery.bindValue(":is_horizontal", isHorizontal ? 1 : 0);
        if (!hitQuery.exec() || !hitQuery.next()) {
            qDebug() << "Error counting hits:" << hitQuery.lastError().text();
            db.rollback();
            return "error";
        }

        int hitCount = hitQuery.value(0).toInt() + 1;
        qDebug() << "Ship id=" << shipId << ", hits=" << hitCount << ", size=" << shipSize;
        if (hitCount >= shipSize) {
            result = "sunk";
            qDebug() << "Ship id=" << shipId << "sunk!";
        } else {
            result = "hit";
        }
    } else {
        result = "miss";
    }

    // Сохраняем ход в той же транзакции
    QSqlQuery moveInsertQuery(db);
    moveInsertQuery.prepare("INSERT INTO Move (game_id, player, x, y, result) VALUES (:game_id, :player, :x, :y, :result)");
    moveInsertQuery.bindValue(":game_id", gameId);
    moveInsertQuery.bindValue(":player", player);
    moveInsertQuery.bindValue(":x", x);
    moveInsertQuery.bindValue(":y", y);
    moveInsertQuery.bindValue(":result", result);

    qDebug() << "Saving move in checkMove: SQL query:" << moveInsertQuery.lastQuery();
    qDebug() << "Bound values:" << moveInsertQuery.boundValues();

    try {
        if (!moveInsertQuery.exec()) {
            qDebug() << "Error saving move in checkMove:" << moveInsertQuery.lastError().text();
            db.rollback();
            return "error";
        }
    } catch (const std::exception &e) {
        qDebug() << "Exception in checkMove while saving move:" << e.what();
        db.rollback();
        return "error";
    }

    if (!db.commit()) {
        qDebug() << "Failed to commit transaction in checkMove:" << db.lastError().text();
        db.rollback();
        return "error";
    }

    qDebug() << "checkMove completed for" << player << "with result:" << result;
    return result;
}

QString DatabaseManager::getCurrentTurn(int gameId)
{
    QMutexLocker locker(&mutex);
    if (!db.isOpen()) {
        qDebug() << "Database is not open!";
        return "";
    }

    QSqlQuery query(db);
    query.prepare("SELECT current_turn FROM Game WHERE game_id = :game_id");
    query.bindValue(":game_id", gameId);
    if (!query.exec() || !query.next()) {
        qDebug() << "Error fetching current turn:" << query.lastError().text();
        return "";
    }
    return query.value("current_turn").toString();
}

bool DatabaseManager::updateTurn(int gameId, const QString &nextPlayer)
{
    QMutexLocker locker(&mutex);
    if (!db.isOpen()) {
        qDebug() << "Database is not open!";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("UPDATE Game SET current_turn = :current_turn WHERE game_id = :game_id");
    query.bindValue(":current_turn", nextPlayer);
    query.bindValue(":game_id", gameId);
    if (!query.exec()) {
        qDebug() << "Error updating turn:" << query.lastError().text();
        return false;
    }
    qDebug() << "Turn updated to" << nextPlayer << "for game" << gameId;
    return true;
}
