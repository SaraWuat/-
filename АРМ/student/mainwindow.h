#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QJsonObject>

namespace Ui { class MainWindow; }

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    Ui::MainWindow *ui;
    bool m_isHeadman = false;
    int  m_groupId   = 0;
    int  m_studentId = 0;

    void loadTodayClasses();
    void loadStudentInfo();
};

#endif // MAINWINDOW_H
