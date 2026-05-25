#ifndef ADMINWINDOW_H
#define ADMINWINDOW_H

#include <QMainWindow>
#include <QJsonArray>

namespace Ui { class AdminWindow; }

class AdminWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit AdminWindow(QWidget *parent = nullptr);
    ~AdminWindow();

private:
    Ui::AdminWindow *ui;

    QJsonArray m_groups;
    QJsonArray m_teachers;
    QJsonArray m_students;  // студенты выбранной группы

    // --- загрузка ---
    void loadGroups();
    void loadTeachers();
    void loadStudents(int group_id);

    // --- заполнение списков ---
    void fillGroupsList();
    void fillTeachersList();
    void fillStudentsList();

    // --- действия: группы ---
    void addGroup();
    void deleteGroup();
    void renameGroup();
    void assignHeadman();
    void assignCurator();

    // --- действия: студенты ---
    void addStudent();
    void deleteStudent();
    void editStudent();

    // --- действия: преподаватели ---
    void addTeacher();
    void deleteTeacher();
    void editTeacher();

    // --- утилиты ---
    int  currentGroupId() const;
    int  currentStudentId() const;
    int  currentTeacherId() const;
    void showStatus(const QString& msg, bool ok = true);
};

#endif // ADMINWINDOW_H
