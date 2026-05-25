#ifndef STUDENTCONTROLLER_H
#define STUDENTCONTROLLER_H

#include <QtHttpServer/QHttpServer>
#include <QHttpServerRequest>
#include <QSqlRecord>
#include <QString>

class studentController
{
public:
    studentController(QHttpServer& server);

private:
    static bool validatePassword(const QSqlRecord& studentRec, const QString& password);
    static QString generateAuthToken();
};

#endif // STUDENTCONTROLLER_H
