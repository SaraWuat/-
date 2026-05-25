#ifndef TEACHERWINDOW_H
#define TEACHERWINDOW_H

#include <QMainWindow>
#include <QJsonArray>

namespace Ui { class TeacherWindow; }

class TeacherWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit TeacherWindow(QWidget *parent = nullptr);
    ~TeacherWindow();

private:
    Ui::TeacherWindow *ui;
    int        m_teacherId  = 0;
    bool       m_isCurator  = false;
    int        m_curGroupId = 0;
    QJsonArray m_groups;

    void loadTeacherInfo();
    void loadTodayClasses();
    void fillGroupsCombo();
};

#endif
