#include "teacherwindow.h"
#include "ui_teacherwindow.h"
#include "components/requester/requester.h"
#include "login.h"
#include "adminwindow.h"
#include "teacher/teachercalendarpage.h"
#include "teacher/teacherattendancepage.h"
#include "teacher/teachergradespage.h"

#include <QSettings>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QJsonArray>
#include <QJsonObject>
#include <QDate>
#include <QSet>
#include <QDebug>

static QString weekLabel() {
    QDate today = QDate::currentDate();
    int year = today.month() >= 9 ? today.year() : today.year() - 1;
    int w = (QDate(year,9,1).daysTo(today)/7 % 2) + 1;
    return w == 1 ? "1-я неделя" : "2-я неделя";
}

TeacherWindow::TeacherWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::TeacherWindow)
{
    ui->setupUi(this);
    setWindowTitle("Преподаватель");

    QSettings s("NagaevM","studentApp");
    ui->teacherName->setText(s.value("name").toString());
    ui->weekLabel->setText(weekLabel());

    bool isAdmin = s.value("is_admin", 0).toInt() == 1;
    ui->adminButton->setVisible(isAdmin);
    ui->attendanceButton->setVisible(false);
    ui->curatorBadge->setVisible(false);

    connect(ui->logoutButton, &QPushButton::clicked, this, [this]() {
        QSettings s("NagaevM","studentApp");
        s.remove("auth_token"); s.remove("name"); s.remove("role"); s.remove("is_admin");
        (new login())->show(); close();
    });
    connect(ui->adminButton, &QPushButton::clicked, this, [this](){
        (new AdminWindow())->show();
    });
    // Расписание препода
    connect(ui->scheduleButton, &QPushButton::clicked, this, [this](){
        (new TeacherCalendarPage(nullptr))->show();
    });
    // Оценки
    connect(ui->gradesButton, &QPushButton::clicked, this, [this](){
        int gid = ui->groupCombo->currentData().toInt();
        if (gid > 0) (new TeacherGradesPage(nullptr, gid, m_teacherId))->show();
    });
    // Посещаемость — только куратор
    connect(ui->attendanceButton, &QPushButton::clicked, this, [this](){
        int gid = ui->groupCombo->currentData().toInt();
        if (gid > 0) (new TeacherAttendancePage(nullptr, gid))->show();
    });

    loadTeacherInfo();
}

TeacherWindow::~TeacherWindow() { delete ui; }

void TeacherWindow::loadTeacherInfo()
{
    QSettings s("NagaevM","studentApp");
    auto* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("teacher/info/%1").arg(s.value("auth_token").toString()),
        [this, r](const QJsonObject &data) {
            m_teacherId  = data["id"].toInt();
            m_isCurator  = data["is_curator"].toBool();
            m_curGroupId = data["curator_group_id"].toInt();
            m_groups     = data["groups"].toArray();
            if (m_isCurator) {
                ui->curatorBadge->setVisible(true);
                ui->curatorBadge->setText("🎓 Куратор");
                ui->attendanceButton->setVisible(true);
            }
            fillGroupsCombo();
            loadTodayClasses();
            r->deleteLater();
        },
        [r](const QJsonObject &e){ qDebug() << e; r->deleteLater(); },
        Requester::Type::GET
    );
}

void TeacherWindow::fillGroupsCombo()
{
    ui->groupCombo->clear();
    QSet<int> seen;
    for (const QJsonValue &v : m_groups) {
        QJsonObject g = v.toObject();
        int gid = g["id"].toInt();
        seen.insert(gid);
        ui->groupCombo->addItem(g["name"].toString(), gid);
    }
    if (m_isCurator && m_curGroupId > 0 && !seen.contains(m_curGroupId))
        ui->groupCombo->addItem("Моя группа (куратор)", m_curGroupId);
}

void TeacherWindow::loadTodayClasses()
{
    QSettings s("NagaevM","studentApp");
    auto* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("classes/today/teacher/%1").arg(s.value("auth_token").toString()),
        [this, r](const QJsonObject &data) {
            QJsonArray arr = data["data"].toArray();
            if (arr.isEmpty()) {
                auto* m = new QStandardItemModel(1,1,this);
                m->setItem(0,0,new QStandardItem("Занятий нет"));
                ui->classesTable->setModel(m); r->deleteLater(); return;
            }
            auto* m = new QStandardItemModel(arr.size(), 4, this);
            for (int i = 0; i < arr.size(); i++) {
                QJsonObject o = arr[i].toObject();
                m->setItem(i,0,new QStandardItem(o["time"].toString()));
                m->setItem(i,1,new QStandardItem(o["subject"].toString()));
                m->setItem(i,2,new QStandardItem(o["group"].toString()));
                m->setItem(i,3,new QStandardItem(o["place"].toString()));
            }
            m->setHeaderData(0,Qt::Horizontal,"Время");
            m->setHeaderData(1,Qt::Horizontal,"Дисциплина");
            m->setHeaderData(2,Qt::Horizontal,"Группа");
            m->setHeaderData(3,Qt::Horizontal,"Ауд.");
            ui->classesTable->setModel(m);
            ui->classesTable->setColumnWidth(0,70);
            ui->classesTable->setColumnWidth(1,190);
            ui->classesTable->setColumnWidth(2,90);
            ui->classesTable->setColumnWidth(3,70);
            ui->classesTable->verticalHeader()->setDefaultSectionSize(45);
            r->deleteLater();
        },
        [r](const QJsonObject&){ r->deleteLater(); },
        Requester::Type::GET
    );
}
