#include <QCoreApplication>
#include <QtHttpServer/QHttpServer>
#include <QTcpServer>
#include <QTimer>
#include <QObject>
#include <QDebug>
#include <QNetworkProxy>

#include "config/config.h"
#include "controllers/classescontroller.h"
#include "controllers/journalcontroller.h"
#include "controllers/studentcontroller.h"
#include "controllers/attendancecontroller.h"
#include "controllers/gradescontroller.h"
#include "controllers/eventscontroller.h"
#include "controllers/admincontroller.h"
#include "controllers/teachercontroller.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

    config conf;
    QHttpServer server;

    auto *tcpServer = new QTcpServer(&a);
    if (tcpServer->listen(QHostAddress::Any, 8081)) {
        qDebug() << "Server listening on port 8081";
        server.bind(tcpServer);
    } else {
        qCritical() << "Failed:" << tcpServer->errorString();
        return -1;
    }

    // Порядок важен: фиксированные маршруты должны регистрироваться раньше <arg>
    studentController    students(server);   // /student/login, /employee/login,
                                             // /student/info/<arg>,
                                             // /student/journalrec/teacher/<arg>,
                                             // /student/grades/<arg>
    classesController    classes(server);    // /schedule/*, /classes/*, /subjects, /teachers, /times
    journalController    journal(server);    // /journal/*
    attendanceController attendance(server); // /attendance/*
    gradesController     grades(server);     // /grades/*
    eventsController     events(server);     // /events/*, /holidays/*
    adminController      admin(server);      // /admin/*
    teacherController    teacher(server);    // /teacher/info/<arg>, /teacher/attendance

    journal.addJournalRecsExec();
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout,
                     &journal, &journalController::addJournalRecsExec);
    timer.start(1000 * 60 * 60);

    server.route("/", []() { return "Server is running!"; });
    return QCoreApplication::exec();
}
