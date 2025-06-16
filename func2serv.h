#ifndef FUNC2SERV_H
#define FUNC2SERV_H

#include <QByteArray>
#include <QString>

// Функция обработки запросов
QByteArray parse(const QString &input, class MyTcpServer *server);

// Функции работы с БД и игрой
bool parseRegisterData(const QString &data, QString &nickname, QString &email, QString &password);
bool parseLoginData(const QString &data, QString &nickname, QString &password);
QByteArray handleRegister(const QString &data);
QByteArray slotLogin(const QString &data);
QByteArray handleStartGame(const QString &data, MyTcpServer *server);
QByteArray handlePlaceShip(const QString &data, MyTcpServer *server);
QByteArray handleMakeMove(const QString &data, MyTcpServer *server);
QByteArray createJsonResponse(const QString &type, const QString &status, const QString &message);

#endif // FUNC2SERV_H
