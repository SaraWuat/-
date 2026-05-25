#ifndef SCHEDULEEDITPAGE_H
#define SCHEDULEEDITPAGE_H

#include <QMainWindow>
#include <QJsonArray>
#include <QDate>

namespace Ui { class ScheduleEditPage; }

class ScheduleEditPage : public QMainWindow
{
    Q_OBJECT
public:
    explicit ScheduleEditPage(QWidget *parent = nullptr, int group_id = 0);
    ~ScheduleEditPage();

private:
    Ui::ScheduleEditPage *ui;
    int        m_groupId  = 0;
    QJsonArray m_subjects;
    QJsonArray m_teachers;
    QJsonArray m_times;
    QJsonArray m_schedule;   // пары
    QJsonArray m_events;     // события
    QJsonArray m_holidays;   // выходные дни

    // ── Загрузка ─────────────────────────────────────────────────
    void loadDicts();
    void loadSchedule();
    void loadEvents();
    void loadHolidays();

    // ── Отображение ──────────────────────────────────────────────
    void fillScheduleTable();
    void fillEventsTable();
    void fillHolidaysTable();

    // ── Действия ─────────────────────────────────────────────────
    void addClass();
    void deleteClass(int class_id);
    void addEvent();
    void deleteEvent(int event_id);
    void addHoliday();
    void deleteHoliday(int holiday_id);

    // ── Проверка конфликтов ──────────────────────────────────────
    // Возвращает true если слот (week_day, week, time_id) уже занят в m_schedule
    bool hasConflict(int week_day, int week, int time_id) const;
    // Возвращает true если дата уже есть в m_holidays
    bool hasHolidayConflict(const QDate &date) const;
};

#endif // SCHEDULEEDITPAGE_H
