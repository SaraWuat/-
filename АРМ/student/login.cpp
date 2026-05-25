#include "login.h"
#include "ui_login.h"

#include <QJsonObject>
#include <QSettings>
#include <QDebug>

#include "components/requester/requester.h"
#include "mainwindow.h"
#include "teacherwindow.h"

login::login(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::login)
{
    ui->setupUi(this);
    setWindowTitle("Вход");
    ui->errorLabel->setVisible(false);

    connect(ui->loginButton, &QPushButton::clicked, this, &login::loginButtonPress);
    connect(ui->passwordInput, &QLineEdit::returnPressed, this, &login::loginButtonPress);
}

login::~login() { delete ui; }

void login::loginButtonPress()
{
    QString email    = ui->emailInput->text().trimmed();
    QString password = ui->passwordInput->text();

    if (email.isEmpty() || password.isEmpty()) {
        showError("Введите email и пароль");
        return;
    }

    ui->loginButton->setEnabled(false);
    ui->errorLabel->setVisible(false);

    QJsonObject json;
    json["email"]    = email;
    json["password"] = password;

    // Сначала пробуем как студент
    Requester* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        "student/login",
        [this, r](const QJsonObject &data) {
            QSettings s("NagaevM", "studentApp");
            s.setValue("auth_token", data["token"].toString());
            s.setValue("name",       data["name"].toString());
            s.setValue("role",       "student");
            s.setValue("is_admin",   0);
            (new MainWindow())->show();
            hide();
            r->deleteLater();
        },
        [this, r, json](const QJsonObject &) {
            r->deleteLater();
            // Не студент — пробуем как employee
            Requester* r2 = new Requester(this);
            r2->initRequester("127.0.0.1", 8081, nullptr);
            r2->sendRequest(
                "employee/login",
                [this, r2](const QJsonObject &data) {
                    QSettings s("NagaevM", "studentApp");
                    s.setValue("auth_token", data["token"].toString());
                    s.setValue("name",       data["name"].toString());
                    s.setValue("role",       "teacher");
                    s.setValue("is_admin",   data["is_admin"].toInt(0));
                    (new TeacherWindow())->show();
                    hide();
                    r2->deleteLater();
                },
                [this, r2](const QJsonObject &err) {
                    showError(err["error"].toString().isEmpty()
                              ? "Неверный email или пароль"
                              : err["error"].toString());
                    ui->loginButton->setEnabled(true);
                    r2->deleteLater();
                },
                Requester::Type::POST, json.toVariantMap()
            );
        },
        Requester::Type::POST, json.toVariantMap()
    );
}

void login::showError(const QString& msg)
{
    ui->errorLabel->setText(msg);
    ui->errorLabel->setVisible(true);
}
