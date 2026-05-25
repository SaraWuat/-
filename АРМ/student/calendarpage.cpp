#include "calendarpage.h"
#include "ui_calendarpage.h"
#include "components/requester/requester.h"

#include <QSettings>
#include <QDate>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardItemModel>
#include <QStandardItem>

// Расчёт чётности для произвольной даты
static int weekForDate(const QDate &date) {
    int year = date.month() >= 9 ? date.year() : date.year() - 1;
    int weeksSince = QDate(year, 9, 1).daysTo(date) / 7;
    return (weeksSince % 2) + 1; // 1=нечётная, 2=чётная
}

CalendarPage::CalendarPage(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::CalendarPage)
{
    ui->setupUi(this);
    setWindowTitle("Календарь");

    // При выборе даты — загружаем пары
    connect(ui->calendarWidget, &QCalendarWidget::clicked,
            this, &CalendarPage::loadClassesForDate);

    connect(ui->backButton, &QPushButton::clicked, this, [this]() {
        this->close();
    });

    // Показываем пары на сегодня сразу
    loadClassesForDate(QDate::currentDate());
}

CalendarPage::~CalendarPage()
{
    delete ui;
}

void CalendarPage::loadClassesForDate(const QDate &date)
{
    int weekDay = date.dayOfWeek(); // 1=Пн, 7=Вс
    int week    = weekForDate(date);

    QString weekStr = (week == 1) ? "Нечётная" : "Чётная";
    static const QStringList dayNames = {
        "", "Понедельник", "Вторник", "Среда",
        "Четверг", "Пятница", "Суббота", "Воскресенье"
    };
    ui->dateLabel->setText(
        QString("%1, %2  [%3 неделя]")
            .arg(dayNames[weekDay])
            .arg(date.toString("dd.MM.yyyy"))
            .arg(weekStr)
    );

    // Воскресенье — пар нет
    if (weekDay == 7) {
        QStandardItemModel* model = new QStandardItemModel(1, 1, this);
        model->setItem(0, 0, new QStandardItem("Выходной"));
        ui->classesTable->setModel(model);
        return;
    }

    QSettings settings("NagaevM", "studentApp");
    QString token = settings.value("auth_token").toString();

    // GET /classes/date/<token>/<date_iso>
    // date_iso = yyyy-MM-dd
    Requester* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("classes/date/%1/%2").arg(token).arg(date.toString("yyyy-MM-dd")),
        [this, r](const QJsonObject &data) {
            QJsonArray arr = data["data"].toArray();

            if (arr.isEmpty()) {
                QStandardItemModel* model = new QStandardItemModel(1, 1, this);
                model->setItem(0, 0, new QStandardItem("Занятий нет"));
                ui->classesTable->setModel(model);
                r->deleteLater();
                return;
            }

            QStandardItemModel* model = new QStandardItemModel(arr.size(), 3, this);
            for (int i = 0; i < arr.size(); i++) {
                QJsonObject obj = arr[i].toObject();
                model->setItem(i, 0, new QStandardItem(obj["time"].toString()));
                QString typeStr2 = obj["type"].toString();
                QString subjLine = obj["subject"].toString();
                if (!typeStr2.isEmpty()) subjLine += " (" + typeStr2 + ")";
                model->setItem(i, 1, new QStandardItem(
                    subjLine + "\n" + obj["teacher"].toString()));
                model->setItem(i, 2, new QStandardItem(obj["place"].toString()));
            }
            model->setHeaderData(0, Qt::Horizontal, "Время");
            model->setHeaderData(1, Qt::Horizontal, "Дисциплина\nПреподаватель");
            model->setHeaderData(2, Qt::Horizontal, "Ауд.");

            ui->classesTable->setModel(model);
            ui->classesTable->setColumnWidth(0, 100);
            ui->classesTable->setColumnWidth(1, 230);
            ui->classesTable->setColumnWidth(2, 80);
            ui->classesTable->verticalHeader()->setDefaultSectionSize(50);
            r->deleteLater();
        },
        [this, r](const QJsonObject &err) {
            qDebug() << "CalendarPage error:" << err;
            QStandardItemModel* model = new QStandardItemModel(1, 1, this);
            model->setItem(0, 0, new QStandardItem("Ошибка соединения"));
            ui->classesTable->setModel(model);
            r->deleteLater();
        },
        Requester::Type::GET
    );
}
