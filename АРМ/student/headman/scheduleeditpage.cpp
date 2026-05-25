#include "scheduleeditpage.h"
#include "ui_scheduleeditpage.h"
#include "../components/requester/requester.h"

#include <QSettings>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QPushButton>
#include <QHeaderView>
#include <QMessageBox>
#include <QDate>

static const QStringList DAY_NAMES  = {"Понедельник","Вторник","Среда","Четверг","Пятница","Суббота"};
static const QStringList WEEK_NAMES = {"Нечётная","Чётная","Обе"};
static const QList<int>  TYPE_VALUES = {1, 2, 0}; // индекс комбобокса → значение в БД (Лекция=1,Пр=2,Лаб=0)
static const QStringList TYPE_NAMES  = {"лек","пр","лаб"};

// ─────────────────────────────────────────────────────────────────────────────
ScheduleEditPage::ScheduleEditPage(QWidget *parent, int group_id)
    : QMainWindow(parent)
    , ui(new Ui::ScheduleEditPage)
    , m_groupId(group_id)
{
    ui->setupUi(this);
    setWindowTitle("Расписание группы");

    // Статические комбобоксы для добавления пары
    ui->weekDayCombo->clear();
    for (const QString &d : DAY_NAMES)
        ui->weekDayCombo->addItem(d);

    ui->weekCombo->clear();
    for (int i = 0; i < WEEK_NAMES.size(); i++)
        ui->weekCombo->addItem(WEEK_NAMES[i], i + 1);

    // Инициализируем DateEdit для праздника текущей датой
    ui->holidayDateEdit->setDate(QDate::currentDate());
    ui->holidayDateEdit->setDisplayFormat("dd.MM.yyyy");
    ui->holidayDateEdit->setCalendarPopup(true);

    connect(ui->backButton,          &QPushButton::clicked, this, [this]() { this->close(); });
    connect(ui->addClassButton,      &QPushButton::clicked, this, [this]() { addClass(); });
    connect(ui->addEventButton,      &QPushButton::clicked, this, [this]() { addEvent(); });
    connect(ui->addHolidayButton,    &QPushButton::clicked, this, [this]() { addHoliday(); });

    loadDicts();
}

ScheduleEditPage::~ScheduleEditPage()
{
    delete ui;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ЗАГРУЗКА СПРАВОЧНИКОВ
// ═══════════════════════════════════════════════════════════════════════════════

void ScheduleEditPage::loadDicts()
{
    // Предметы
    Requester* r1 = new Requester(this);
    r1->initRequester("127.0.0.1", 8081, nullptr);
    r1->sendRequest("subjects",
        [this, r1](const QJsonObject &data) {
            m_subjects = data["data"].toArray();
            ui->subjectCombo->clear();
            for (const QJsonValue &v : m_subjects) {
                QJsonObject s = v.toObject();
                ui->subjectCombo->addItem(s["name"].toString(), s["id"].toInt());
            }
            r1->deleteLater();
        },
        [r1](const QJsonObject& err) { qDebug() << "subjects error:" << err; r1->deleteLater(); },
        Requester::Type::GET
    );

    // Преподаватели
    Requester* r2 = new Requester(this);
    r2->initRequester("127.0.0.1", 8081, nullptr);
    r2->sendRequest("teachers",
        [this, r2](const QJsonObject &data) {
            m_teachers = data["data"].toArray();
            ui->teacherCombo->clear();
            for (const QJsonValue &v : m_teachers) {
                QJsonObject t = v.toObject();
                ui->teacherCombo->addItem(t["name"].toString(), t["id"].toInt());
            }
            r2->deleteLater();
        },
        [r2](const QJsonObject& err) { qDebug() << "teachers error:" << err; r2->deleteLater(); },
        Requester::Type::GET
    );

    // Времена → после загрузки запускаем всё остальное
    Requester* r3 = new Requester(this);
    r3->initRequester("127.0.0.1", 8081, nullptr);
    r3->sendRequest("times",
        [this, r3](const QJsonObject &data) {
            m_times = data["data"].toArray();
            ui->timeCombo->clear();
            for (const QJsonValue &v : m_times) {
                QJsonObject t = v.toObject();
                ui->timeCombo->addItem(t["time"].toString(), t["id"].toInt());
            }
            loadSchedule();
            loadEvents();
            loadHolidays();
            r3->deleteLater();
        },
        [r3](const QJsonObject& err) { qDebug() << "times error:" << err; r3->deleteLater(); },
        Requester::Type::GET
    );
}

// ═══════════════════════════════════════════════════════════════════════════════
// ЗАГРУЗКА ДАННЫХ
// ═══════════════════════════════════════════════════════════════════════════════

void ScheduleEditPage::loadSchedule()
{
    Requester* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("schedule/%1").arg(m_groupId),
        [this, r](const QJsonObject &data) {
            m_schedule = data["data"].toArray();
            fillScheduleTable();
            r->deleteLater();
        },
        [r](const QJsonObject &err) { qDebug() << "loadSchedule error:" << err; r->deleteLater(); },
        Requester::Type::GET
    );
}

void ScheduleEditPage::loadEvents()
{
    Requester* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("events/%1").arg(m_groupId),
        [this, r](const QJsonObject &data) {
            m_events = data["data"].toArray();
            fillEventsTable();
            r->deleteLater();
        },
        [r](const QJsonObject &err) { qDebug() << "loadEvents error:" << err; r->deleteLater(); },
        Requester::Type::GET
    );
}

void ScheduleEditPage::loadHolidays()
{
    Requester* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("holidays/%1").arg(m_groupId),
        [this, r](const QJsonObject &data) {
            m_holidays = data["data"].toArray();
            fillHolidaysTable();
            r->deleteLater();
        },
        [r](const QJsonObject &err) { qDebug() << "loadHolidays error:" << err; r->deleteLater(); },
        Requester::Type::GET
    );
}

// ═══════════════════════════════════════════════════════════════════════════════
// ОТОБРАЖЕНИЕ ТАБЛИЦ
// ═══════════════════════════════════════════════════════════════════════════════

void ScheduleEditPage::fillScheduleTable()
{
    QTableWidget* table = ui->scheduleTable;
    table->clear();
    table->setColumnCount(7);
    table->setHorizontalHeaderLabels({"День","Нед.","Время","Тип","Предмет","Ауд.","Del"});
    table->setRowCount(m_schedule.size());
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(false);

    auto mkItem = [](const QString &text) {
        auto* it = new QTableWidgetItem(text);
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        return it;
    };

    for (int i = 0; i < m_schedule.size(); i++) {
        QJsonObject c = m_schedule[i].toObject();
        int wd   = c["week_day"].toInt();
        int week = c["week"].toInt();
        int type = c["type"].toInt();

        table->setItem(i, 0, mkItem(wd >= 1 && wd <= 6 ? DAY_NAMES[wd-1] : "?"));
        table->setItem(i, 1, mkItem(week >= 1 && week <= 3 ? WEEK_NAMES[week-1] : "?"));
        table->setItem(i, 2, mkItem(c["time"].toString()));
        table->setItem(i, 3, mkItem(type >= 0 && type <= 2 ? TYPE_NAMES[type] : "?"));
        table->setItem(i, 4, mkItem(c["subject"].toString()));
        table->setItem(i, 5, mkItem(c["place"].toString()));

        QPushButton* delBtn = new QPushButton("✕");
        delBtn->setFixedWidth(32);
        int classId = c["id"].toInt();
        connect(delBtn, &QPushButton::clicked, [this, classId]() { deleteClass(classId); });
        table->setCellWidget(i, 6, delBtn);
    }
    table->resizeColumnsToContents();
    table->resizeRowsToContents();
}

void ScheduleEditPage::fillEventsTable()
{
    QTableWidget* table = ui->eventsTable;
    table->clear();
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({"Дата","День","Время","Название","Del"});
    table->setRowCount(m_events.size());
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(false);

    auto mkItem = [](const QString &text) {
        auto* it = new QTableWidgetItem(text);
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        return it;
    };

    for (int i = 0; i < m_events.size(); i++) {
        QJsonObject e = m_events[i].toObject();
        QDate date = QDate::fromString(e["date"].toString(), "yyyy-MM-dd");
        int wd = date.isValid() ? date.dayOfWeek() : 0;

        table->setItem(i, 0, mkItem(date.isValid() ? date.toString("dd.MM.yyyy") : e["date"].toString()));
        table->setItem(i, 1, mkItem(wd >= 1 && wd <= 7 ? (wd <= 6 ? DAY_NAMES[wd-1] : "Воскр.") : "?"));
        table->setItem(i, 2, mkItem(e["time"].toString()));
        table->setItem(i, 3, mkItem(e["title"].toString()));

        QPushButton* delBtn = new QPushButton("✕");
        delBtn->setFixedWidth(32);
        int eventId = e["id"].toInt();
        connect(delBtn, &QPushButton::clicked, [this, eventId]() { deleteEvent(eventId); });
        table->setCellWidget(i, 4, delBtn);
    }
    table->resizeColumnsToContents();
    table->resizeRowsToContents();
}

void ScheduleEditPage::fillHolidaysTable()
{
    QTableWidget* table = ui->holidaysTable;
    table->clear();
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({"Дата","Del"});
    table->setRowCount(m_holidays.size());
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);

    auto mkItem = [](const QString &text) {
        auto* it = new QTableWidgetItem(text);
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        it->setTextAlignment(Qt::AlignCenter);
        return it;
    };

    for (int i = 0; i < m_holidays.size(); i++) {
        QJsonObject h = m_holidays[i].toObject();
        QDate date = QDate::fromString(h["date"].toString(), "yyyy-MM-dd");
        table->setItem(i, 0, mkItem(date.isValid() ? date.toString("dd.MM.yyyy") : h["date"].toString()));

        QPushButton* delBtn = new QPushButton("✕");
        delBtn->setFixedWidth(32);
        int hid = h["id"].toInt();
        connect(delBtn, &QPushButton::clicked, [this, hid]() { deleteHoliday(hid); });
        table->setCellWidget(i, 1, delBtn);
    }
    table->resizeColumnsToContents();
    table->resizeRowsToContents();
}

// ═══════════════════════════════════════════════════════════════════════════════
// ПРОВЕРКА КОНФЛИКТОВ
// ═══════════════════════════════════════════════════════════════════════════════

bool ScheduleEditPage::hasConflict(int week_day, int week, int time_id) const
{
    // week==3 (Обе) конфликтует с нечётной(1), чётной(2) и обеими(3)
    for (const QJsonValue &v : m_schedule) {
        QJsonObject c = v.toObject();
        if (c["week_day"].toInt() != week_day) continue;
        if (c["time_id"].toInt()  != time_id)  continue;

        int existWeek = c["week"].toInt();
        // Конфликт если оба "Обе", или хотя бы один "Обе", или совпадают
        bool conflict = (week == 3 || existWeek == 3 || week == existWeek);
        if (conflict) return true;
    }
    return false;
}

bool ScheduleEditPage::hasHolidayConflict(const QDate &date) const
{
    QString ds = date.toString("yyyy-MM-dd");
    for (const QJsonValue &v : m_holidays) {
        if (v.toObject()["date"].toString() == ds) return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ДЕЙСТВИЯ: ПАРЫ
// ═══════════════════════════════════════════════════════════════════════════════

void ScheduleEditPage::addClass()
{
    if (ui->placeEdit->text().trimmed().isEmpty()) {
        ui->classStatusLabel->setText("Укажите аудиторию");
        ui->classStatusLabel->setStyleSheet("color: red;");
        return;
    }

    int weekDay = ui->weekDayCombo->currentIndex() + 1;
    int week    = ui->weekCombo->currentData().toInt();
    int timeId  = ui->timeCombo->currentData().toInt();

    if (hasConflict(weekDay, week, timeId)) {
        QMessageBox::warning(this, "Конфликт в расписании",
            QString("На %1 (%2 неделя) в %3 уже стоит пара.\n"
                    "Удалите существующую или выберите другое время.")
                .arg(DAY_NAMES[weekDay - 1])
                .arg(WEEK_NAMES[week - 1])
                .arg(ui->timeCombo->currentText()));
        return;
    }

    QSettings settings("NagaevM", "studentApp");
    int typeIndex = ui->typeCombo->currentIndex();
    int typeValue = TYPE_VALUES.value(typeIndex, 1);

    QJsonObject body;
    body["token"]      = settings.value("auth_token").toString();
    body["group_id"]   = m_groupId;
    body["subject_id"] = ui->subjectCombo->currentData().toInt();
    body["teacher_id"] = ui->teacherCombo->currentData().toInt();
    body["time_id"]    = timeId;
    body["week_day"]   = weekDay;
    body["week"]       = week;
    body["place"]      = ui->placeEdit->text();
    body["type"]       = typeValue;

    Requester* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        "schedule",
        [this, r](const QJsonObject &) {
            ui->classStatusLabel->setText("✓ Пара добавлена");
            ui->classStatusLabel->setStyleSheet("color: green; font-weight: bold;");
            ui->placeEdit->clear();
            loadSchedule();
            r->deleteLater();
        },
        [this, r](const QJsonObject &err) {
            ui->classStatusLabel->setText("Ошибка: " + err["error"].toString());
            ui->classStatusLabel->setStyleSheet("color: red;");
            r->deleteLater();
        },
        Requester::Type::POST,
        body.toVariantMap()
    );
}

void ScheduleEditPage::deleteClass(int class_id)
{
    QSettings settings("NagaevM", "studentApp");
    Requester* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("schedule/%1?token=%2").arg(class_id).arg(settings.value("auth_token").toString()),
        [this, r](const QJsonObject &) { loadSchedule(); r->deleteLater(); },
        [r](const QJsonObject &err)    { qDebug() << "deleteClass:" << err; r->deleteLater(); },
        Requester::Type::DELET
    );
}

// ═══════════════════════════════════════════════════════════════════════════════
// ДЕЙСТВИЯ: СОБЫТИЯ
// ═══════════════════════════════════════════════════════════════════════════════

void ScheduleEditPage::addEvent()
{
    QString title = ui->eventTitleEdit->text().trimmed();
    if (title.isEmpty()) {
        ui->eventStatusLabel->setText("Введите название события");
        ui->eventStatusLabel->setStyleSheet("color: red;");
        return;
    }

    QDate  date    = ui->eventDateEdit->date();
    QString timeStr = ui->eventTimeEdit->text().trimmed(); // формат "08:00"

    // Предупреждение если в этот день выходной
    if (hasHolidayConflict(date)) {
        QMessageBox::warning(this, "Выходной день",
            QString("Дата %1 отмечена как выходной день.\n"
                    "Всё равно добавить событие?").arg(date.toString("dd.MM.yyyy")));
        // продолжаем — это не блокирующий запрет
    }

    QSettings settings("NagaevM", "studentApp");
    QJsonObject body;
    body["token"]    = settings.value("auth_token").toString();
    body["group_id"] = m_groupId;
    body["date"]     = date.toString("yyyy-MM-dd");
    body["time"]     = timeStr;
    body["title"]    = title;

    Requester* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        "events",
        [this, r](const QJsonObject &) {
            ui->eventStatusLabel->setText("✓ Событие добавлено");
            ui->eventStatusLabel->setStyleSheet("color: green; font-weight: bold;");
            ui->eventTitleEdit->clear();
            ui->eventTimeEdit->clear();
            loadEvents();
            r->deleteLater();
        },
        [this, r](const QJsonObject &err) {
            ui->eventStatusLabel->setText("Ошибка: " + err["error"].toString());
            ui->eventStatusLabel->setStyleSheet("color: red;");
            r->deleteLater();
        },
        Requester::Type::POST,
        body.toVariantMap()
    );
}

void ScheduleEditPage::deleteEvent(int event_id)
{
    QSettings settings("NagaevM", "studentApp");
    Requester* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("events/%1?token=%2").arg(event_id).arg(settings.value("auth_token").toString()),
        [this, r](const QJsonObject &) { loadEvents(); r->deleteLater(); },
        [r](const QJsonObject &err)    { qDebug() << "deleteEvent:" << err; r->deleteLater(); },
        Requester::Type::DELET
    );
}

// ═══════════════════════════════════════════════════════════════════════════════
// ДЕЙСТВИЯ: ВЫХОДНЫЕ ДНИ
// ═══════════════════════════════════════════════════════════════════════════════

void ScheduleEditPage::addHoliday()
{
    QDate date = ui->holidayDateEdit->date();

    if (hasHolidayConflict(date)) {
        ui->holidayStatusLabel->setText("Эта дата уже отмечена как выходной");
        ui->holidayStatusLabel->setStyleSheet("color: red;");
        return;
    }

    QSettings settings("NagaevM", "studentApp");
    QJsonObject body;
    body["token"]    = settings.value("auth_token").toString();
    body["group_id"] = m_groupId;
    body["date"]     = date.toString("yyyy-MM-dd");

    Requester* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        "holidays",
        [this, r](const QJsonObject &) {
            ui->holidayStatusLabel->setText("✓ Выходной добавлен");
            ui->holidayStatusLabel->setStyleSheet("color: green; font-weight: bold;");
            loadHolidays();
            r->deleteLater();
        },
        [this, r](const QJsonObject &err) {
            ui->holidayStatusLabel->setText("Ошибка: " + err["error"].toString());
            ui->holidayStatusLabel->setStyleSheet("color: red;");
            r->deleteLater();
        },
        Requester::Type::POST,
        body.toVariantMap()
    );
}

void ScheduleEditPage::deleteHoliday(int holiday_id)
{
    QSettings settings("NagaevM", "studentApp");
    Requester* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("holidays/%1?token=%2").arg(holiday_id).arg(settings.value("auth_token").toString()),
        [this, r](const QJsonObject &) { loadHolidays(); r->deleteLater(); },
        [r](const QJsonObject &err)    { qDebug() << "deleteHoliday:" << err; r->deleteLater(); },
        Requester::Type::DELET
    );
}
