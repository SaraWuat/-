#ifndef TEACHERCALENDARPAGE_H
#define TEACHERCALENDARPAGE_H

#include <QMainWindow>
#include <QDate>

namespace Ui { class TeacherCalendarPage; }

class TeacherCalendarPage : public QMainWindow
{
    Q_OBJECT
public:
    explicit TeacherCalendarPage(QWidget *parent = nullptr);
    ~TeacherCalendarPage();

private:
    Ui::TeacherCalendarPage *ui;
    void loadClassesForDate(const QDate &date);
};

#endif
