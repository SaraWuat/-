#include "login.h"
#include "mainwindow.h"
#include "teacherwindow.h"

#include <QApplication>
#include <QSettings>
#include "config.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    config conf;

    QSettings s("NagaevM", "studentApp");
    QString token = s.value("auth_token").toString();
    QString role  = s.value("role").toString();
    QString name  = s.value("name").toString();

    if (!token.isEmpty() && !name.isEmpty()) {
        if (role == "teacher")
            (new TeacherWindow())->show();
        else
            (new MainWindow())->show();
    } else {
        (new login())->show();
    }

    return QApplication::exec();
}
