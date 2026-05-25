#ifndef TEACHERJOURNALPAGE_H
#define TEACHERJOURNALPAGE_H

#include <QMainWindow>
#include <QJsonArray>
#include <QJsonObject>
#include <QDialog>
#include <QSpinBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>

namespace Ui { class TeacherJournalPage; }

class TeacherJournalPage : public QMainWindow
{
    Q_OBJECT
public:
    explicit TeacherJournalPage(QWidget *parent = nullptr,
                                 int group_id   = 0,
                                 int teacher_id = 0);
    ~TeacherJournalPage();

private:
    Ui::TeacherJournalPage *ui;
    int         m_groupId   = 0;
    int         m_teacherId = 0;
    QJsonArray  m_students;
    QJsonArray  m_recs;      // только пары этого препода
    QJsonObject m_dataMap;   // rec_id:student_id -> {absent, grade}

    void loadJournal();
    void buildTable();
    void saveGrade(int rec_id, int student_id, int grade);
    void saveAttendance(int rec_id, int student_id, int absent);
};

#endif
