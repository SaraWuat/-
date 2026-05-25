#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "components/requester/requester.h"
#include "login.h"
#include "calendarpage.h"
#include "attendancepage.h"
#include "gradespage.h"
#include "headman/attendanceinputpage.h"
#include "headman/scheduleeditpage.h"

#include <QSettings>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QDate>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>

static QString weekLabel() {
    QDate today = QDate::currentDate();
    int year = today.month() >= 9 ? today.year() : today.year() - 1;
    int w = (QDate(year, 9, 1).daysTo(today) / 7 % 2) + 1;
    return w == 1 ? "Нечётная неделя" : "Чётная неделя";
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle("Студент");

    QSettings s("NagaevM", "studentApp");
    ui->studentName->setText(s.value("name").toString());
    ui->weekLabel->setText(weekLabel());

    ui->headmanAttendanceButton->setVisible(false);
    ui->headmanScheduleButton->setVisible(false);

    connect(ui->logoutButton, &QPushButton::clicked, this, [this]() {
        QSettings s("NagaevM","studentApp");
        s.remove("auth_token"); s.remove("name"); s.remove("role"); s.remove("is_admin");
        (new login())->show(); close();
    });
    connect(ui->calendarButton,    &QPushButton::clicked, this, [this](){
        (new CalendarPage(nullptr))->show();
    });
    connect(ui->attendanceButton,  &QPushButton::clicked, this, [this](){
        (new AttendancePage(nullptr))->show();
    });
    connect(ui->gradesButton,      &QPushButton::clicked, this, [this](){
        (new GradesPage(nullptr))->show();
    });
    connect(ui->headmanAttendanceButton, &QPushButton::clicked, this, [this](){
        (new AttendanceInputPage(nullptr, m_groupId))->show();
    });
    connect(ui->headmanScheduleButton, &QPushButton::clicked, this, [this](){
        (new ScheduleEditPage(nullptr, m_groupId))->show();
    });

    loadStudentInfo();
    loadTodayClasses();
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::loadStudentInfo()
{
    QSettings s("NagaevM","studentApp");
    auto* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("student/info/%1").arg(s.value("auth_token").toString()),
        [this, r](const QJsonObject &data) {
            m_studentId = data["id"].toInt();
            m_groupId   = data["group_id"].toInt();
            m_isHeadman = data["is_headman"].toBool();
            if (m_isHeadman) {
                ui->headmanAttendanceButton->setVisible(true);
                ui->headmanScheduleButton->setVisible(true);
            }
            r->deleteLater();
        },
        [r](const QJsonObject &e){ qDebug() << e; r->deleteLater(); },
        Requester::Type::GET
    );
}

void MainWindow::loadTodayClasses()
{
    QSettings s("NagaevM","studentApp");
    auto* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("classes/today/token/%1").arg(s.value("auth_token").toString()),
        [this, r](const QJsonObject &data) {
            QJsonArray classes = data["data"].toArray();
            QJsonArray events  = data["events"].toArray();

            // Объединяем пары и события в одну модель
            int totalRows = classes.size() + events.size();
            if (totalRows == 0) {
                auto* m = new QStandardItemModel(1,1,this);
                m->setItem(0,0,new QStandardItem("Занятий нет"));
                ui->classesTable->setModel(m);
                r->deleteLater(); return;
            }

            auto* m = new QStandardItemModel(totalRows, 3, this);
            int row = 0;

            // Пары
            for (int i = 0; i < classes.size(); i++, row++) {
                QJsonObject o = classes[i].toObject();
                m->setItem(row,0,new QStandardItem(o["time"].toString()));
                QString typeStr = o["type"].toString();
                QString subjectLine = o["subject"].toString();
                if (!typeStr.isEmpty()) subjectLine += " (" + typeStr + ")";
                m->setItem(row,1,new QStandardItem(
                    subjectLine + "\n" + o["teacher"].toString()));
                m->setItem(row,2,new QStandardItem(o["place"].toString()));
            }

            // События — выделяем цветом
            for (int i = 0; i < events.size(); i++, row++) {
                QJsonObject e = events[i].toObject();
                auto mkE = [](const QString& t) {
                    auto* it = new QStandardItem(t);
                    it->setBackground(QBrush(QColor("#fff3cd")));
                    it->setForeground(QBrush(QColor("#856404")));
                    return it;
                };
                m->setItem(row,0,mkE(e["time"].toString().isEmpty() ? "—" : e["time"].toString()));
                m->setItem(row,1,mkE("📌 " + e["title"].toString()));
                m->setItem(row,2,mkE(""));
            }

            m->setHeaderData(0,Qt::Horizontal,"Время");
            m->setHeaderData(1,Qt::Horizontal,"Дисциплина / Преподаватель");
            m->setHeaderData(2,Qt::Horizontal,"Ауд.");
            ui->classesTable->setModel(m);
            ui->classesTable->setColumnWidth(0,80);
            ui->classesTable->setColumnWidth(1,260);
            ui->classesTable->setColumnWidth(2,90);
            ui->classesTable->verticalHeader()->setDefaultSectionSize(50);
            r->deleteLater();
        },
        [this, r](const QJsonObject&) {
            auto* m = new QStandardItemModel(1,1,this);
            m->setItem(0,0,new QStandardItem("Ошибка соединения"));
            ui->classesTable->setModel(m);
            r->deleteLater();
        },
        Requester::Type::GET
    );
}
