#include "teachercalendarpage.h"
#include "ui_teachercalendarpage.h"
#include "../components/requester/requester.h"

#include <QSettings>
#include <QDate>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardItemModel>
#include <QStandardItem>

static int weekForDate(const QDate &date) {
    int year = date.month() >= 9 ? date.year() : date.year() - 1;
    return (QDate(year, 9, 1).daysTo(date) / 7 % 2) + 1;
}

static const QStringList DAY_NAMES = {
    "", "Понедельник", "Вторник", "Среда",
    "Четверг", "Пятница", "Суббота", "Воскресенье"
};

TeacherCalendarPage::TeacherCalendarPage(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::TeacherCalendarPage)
{
    ui->setupUi(this);
    setWindowTitle("Моё расписание");

    connect(ui->calendarWidget, &QCalendarWidget::clicked,
            this, &TeacherCalendarPage::loadClassesForDate);
    connect(ui->backButton, &QPushButton::clicked,
            this, [this](){ close(); });

    loadClassesForDate(QDate::currentDate());
}

TeacherCalendarPage::~TeacherCalendarPage() { delete ui; }

void TeacherCalendarPage::loadClassesForDate(const QDate &date)
{
    int wd   = date.dayOfWeek();
    int week = weekForDate(date);
    QString weekStr = (week == 1) ? "1-я (нечётная)" : "2-я (чётная)";

    ui->dateLabel->setText(
        QString("%1, %2  [%3 неделя]")
            .arg(DAY_NAMES[wd])
            .arg(date.toString("dd.MM.yyyy"))
            .arg(weekStr));

    if (wd == 7) {
        auto* m = new QStandardItemModel(1,1,this);
        m->setItem(0,0,new QStandardItem("Выходной"));
        ui->classesTable->setModel(m);
        return;
    }

    QSettings s("NagaevM","studentApp");
    auto* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    // Используем роут для преподавателя по дате
    r->sendRequest(
        QString("classes/date/teacher/%1/%2")
            .arg(s.value("auth_token").toString())
            .arg(date.toString("yyyy-MM-dd")),
        [this, r](const QJsonObject &data) {
            QJsonArray arr = data["data"].toArray();
            if (arr.isEmpty()) {
                auto* m = new QStandardItemModel(1,1,this);
                m->setItem(0,0,new QStandardItem("Занятий нет"));
                ui->classesTable->setModel(m);
                r->deleteLater(); return;
            }
            auto* m = new QStandardItemModel(arr.size(), 4, this);
            for (int i = 0; i < arr.size(); i++) {
                QJsonObject o = arr[i].toObject();
                m->setItem(i,0,new QStandardItem(o["time"].toString()));
                m->setItem(i,1,new QStandardItem(
                    o["subject"].toString() + "\n" + o["type"].toString()));
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
            ui->classesTable->setColumnWidth(2,100);
            ui->classesTable->setColumnWidth(3,70);
            ui->classesTable->verticalHeader()->setDefaultSectionSize(48);
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
