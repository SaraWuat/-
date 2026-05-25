#ifndef TEACHERGRADESPAGE_H
#define TEACHERGRADESPAGE_H

#include <QMainWindow>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>

namespace Ui { class TeacherGradesPage; }

class TeacherGradesPage : public QMainWindow
{
    Q_OBJECT
public:
    explicit TeacherGradesPage(QWidget *parent = nullptr,
                                int group_id   = 0,
                                int teacher_id = 0);
    ~TeacherGradesPage();

private:
    Ui::TeacherGradesPage *ui;
    int         m_groupId   = 0;
    int         m_teacherId = 0;
    QJsonArray  m_students;
    QJsonArray  m_subjects;
    QMap<int, QJsonArray> m_marksBySubject;

    void loadSubjects();
    void loadStudents();
    void loadMarks();
    void buildTable();
    void addMarkDialog(int student_id, const QString& student_name);
};

#endif
