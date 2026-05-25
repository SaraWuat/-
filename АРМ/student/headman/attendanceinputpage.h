#ifndef ATTENDANCEINPUTPAGE_H
#define ATTENDANCEINPUTPAGE_H

#include <QMainWindow>
#include <QJsonArray>
#include <QDate>

namespace Ui { class AttendanceInputPage; }

class AttendanceInputPage : public QMainWindow
{
    Q_OBJECT
public:
    explicit AttendanceInputPage(QWidget *parent = nullptr, int group_id = 0);
    ~AttendanceInputPage();

private:
    Ui::AttendanceInputPage *ui;
    int        m_groupId          = 0;
    QJsonArray m_students;
    int        m_classId          = 0;
    bool       m_alreadySubmitted = false;
    bool       m_editMode         = false;

    void loadClassesForDate(const QDate &date);
    void loadStudents(int class_id);
    void submit();
    void submitEdit();
    void setReadOnlyMode(bool readOnly);
    void enableEditMode();
};

#endif
