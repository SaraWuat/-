#ifndef ATTENDANCEPAGE_H
#define ATTENDANCEPAGE_H

#include <QMainWindow>
#include <QJsonArray>

namespace Ui { class AttendancePage; }

class AttendancePage : public QMainWindow
{
    Q_OBJECT
public:
    explicit AttendancePage(QWidget *parent = nullptr);
    ~AttendancePage();

private:
    Ui::AttendancePage *ui;
    QJsonArray m_data; // все записи

    void loadData();
    void buildTree();
};

#endif // ATTENDANCEPAGE_H
