#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

class DatabaseManager : public QObject
{
    Q_OBJECT

public:
    static DatabaseManager* getInstance();
    QSqlDatabase getDatabase();
    bool addUser(const QString &nickname, const QString &email, const QString &password);
    void printUsers();

    // Методы для работы с игрой
    int createGame(const QString &player1, const QString &player2); // Создание новой игры с инициализацией первого хода
    bool saveShip(int gameId, const QString &player, int x, int y, int size, bool isHorizontal); // Сохранение корабля
    bool saveMove(int gameId, const QString &player, int x, int y, const QString &result); // Сохранение хода
    QString checkMove(int gameId, const QString &player, int x, int y); // Проверка результата выстрела
    QString getCurrentTurn(int gameId); // Получение текущего хода
    bool updateTurn(int gameId, const QString &nextPlayer); // Обновление текущего хода

private:
    DatabaseManager();
    virtual ~DatabaseManager();
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    static DatabaseManager* instance;
    QSqlDatabase db;
};

#endif // DATABASEMANAGER_H
