#ifndef CALENDARPAGE_H
#define CALENDARPAGE_H

#include <QMainWindow>
#include <QDate>

namespace Ui { class CalendarPage; }

class CalendarPage : public QMainWindow
{
    Q_OBJECT
public:
    explicit CalendarPage(QWidget *parent = nullptr);
    ~CalendarPage();

private:
    Ui::CalendarPage *ui;
    void loadClassesForDate(const QDate &date);
};

#endif // CALENDARPAGE_H
