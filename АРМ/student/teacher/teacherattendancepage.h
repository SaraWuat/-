#ifndef TEACHERATTENDANCEPAGE_H
#define TEACHERATTENDANCEPAGE_H

#include <QMainWindow>
#include <QJsonArray>
#include <QJsonObject>

namespace Ui { class TeacherAttendancePage; }

class TeacherAttendancePage : public QMainWindow
{
    Q_OBJECT
public:
    explicit TeacherAttendancePage(QWidget *parent = nullptr, int group_id = 0);
    ~TeacherAttendancePage();

private:
    Ui::TeacherAttendancePage *ui;
    int        m_groupId = 0;
    QJsonArray m_students;
    QJsonArray m_subjects;   // предметы группы
    // Данные журнала: { students, recs, data }
    QJsonObject m_journalData;

    void loadSubjects();
    void loadJournal();
    void buildTable(int subject_id);  // строим таблицу для выбранного предмета
};

#endif
