#ifndef GRADESPAGE_H
#define GRADESPAGE_H

#include <QMainWindow>
#include <QJsonArray>

namespace Ui { class GradesPage; }

class GradesPage : public QMainWindow
{
    Q_OBJECT
public:
    explicit GradesPage(QWidget *parent = nullptr);
    ~GradesPage();
private:
    Ui::GradesPage *ui;
    void loadGrades();
};

#endif // GRADESPAGE_H
