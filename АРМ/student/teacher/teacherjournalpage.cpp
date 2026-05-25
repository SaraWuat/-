#include "teacherjournalpage.h"
#include "ui_teacherjournalpage.h"
#include "../components/requester/requester.h"

#include <QSettings>
#include <QJsonDocument>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QDialog>
#include <QSpinBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QDebug>

TeacherJournalPage::TeacherJournalPage(QWidget *parent, int group_id, int teacher_id)
    : QMainWindow(parent)
    , ui(new Ui::TeacherJournalPage)
    , m_groupId(group_id)
    , m_teacherId(teacher_id)
{
    ui->setupUi(this);
    setWindowTitle("Журнал группы");
    connect(ui->backButton,    &QPushButton::clicked, this, [this](){ close(); });
    connect(ui->refreshButton, &QPushButton::clicked, this, [this](){ loadJournal(); });
    loadJournal();
}

TeacherJournalPage::~TeacherJournalPage() { delete ui; }

void TeacherJournalPage::loadJournal()
{
    QSettings s("NagaevM","studentApp");
    QString tok = s.value("auth_token").toString();

    // Запрашиваем журнал с фильтром по teacher (сервер вернёт только пары этого препода)
    auto* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("journal/group/%1?token=%2").arg(m_groupId).arg(tok),
        [this, r](const QJsonObject &data) {
            m_students = data["students"].toArray();
            m_recs     = data["recs"].toArray();
            m_dataMap  = QJsonObject();
            for (const QJsonValue &v : data["data"].toArray()) {
                QJsonObject item = v.toObject();
                QString key = QString("%1:%2")
                    .arg(item["rec_id"].toInt())
                    .arg(item["student_id"].toInt());
                m_dataMap[key] = item;
            }
            buildTable();
            r->deleteLater();
        },
        [r](const QJsonObject &e){ qDebug() << e; r->deleteLater(); },
        Requester::Type::GET
    );
}

void TeacherJournalPage::buildTable()
{
    QTableWidget* t = ui->journalTable;
    t->clear();
    int nS = m_students.size(), nR = m_recs.size();
    t->setRowCount(nS);
    t->setColumnCount(nR + 1);
    t->horizontalHeader()->setDefaultSectionSize(65);
    t->setColumnWidth(0, 190);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->verticalHeader()->setVisible(false);
    t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    QStringList headers; headers << "ФИО";
    for (const QJsonValue &v : m_recs) {
        QJsonObject rec = v.toObject();
        headers << rec["date"].toString() + "\n" + rec["subject"].toString().left(6);
    }
    t->setHorizontalHeaderLabels(headers);

    for (int i = 0; i < nS; i++) {
        QJsonObject stu = m_students[i].toObject();
        int sid = stu["id"].toInt();

        auto* ni = new QTableWidgetItem(stu["name"].toString());
        ni->setFlags(ni->flags() & ~Qt::ItemIsEditable);
        t->setItem(i, 0, ni);

        for (int j = 0; j < nR; j++) {
            int rid = m_recs[j].toObject()["rec_id"].toInt();
            QString key = QString("%1:%2").arg(rid).arg(sid);
            int absent = 0, grade = 0;
            if (m_dataMap.contains(key)) {
                absent = m_dataMap[key].toObject()["absent"].toInt();
                grade  = m_dataMap[key].toObject()["grade"].toInt();
            }
            QString text = absent ? "Н" : (grade > 0 ? QString::number(grade) : "");
            auto* ci = new QTableWidgetItem(text);
            ci->setTextAlignment(Qt::AlignCenter);
            if (absent)       ci->setBackground(QColor("#f8d7da"));
            else if (grade>0) ci->setBackground(grade>=4 ? QColor("#d4edda") : QColor("#fff3cd"));
            ci->setData(Qt::UserRole,   rid);
            ci->setData(Qt::UserRole+1, sid);
            ci->setData(Qt::UserRole+2, absent);
            ci->setData(Qt::UserRole+3, grade);
            t->setItem(i, j+1, ci);
        }
    }

    disconnect(t, &QTableWidget::cellDoubleClicked, nullptr, nullptr);
    connect(t, &QTableWidget::cellDoubleClicked, this, [this](int row, int col){
        if (col == 0) return;
        auto* ci = ui->journalTable->item(row, col);
        if (!ci) return;
        int rid    = ci->data(Qt::UserRole).toInt();
        int sid    = ci->data(Qt::UserRole+1).toInt();
        int absent = ci->data(Qt::UserRole+2).toInt();
        int grade  = ci->data(Qt::UserRole+3).toInt();

        QDialog dlg(this);
        dlg.setWindowTitle("Посещаемость / оценка");
        auto* vl = new QVBoxLayout(&dlg);
        auto* absentCb = new QCheckBox("Отсутствовал (Н)", &dlg);
        absentCb->setChecked(absent == 1);
        vl->addWidget(absentCb);
        auto* gradeSpin = new QSpinBox(&dlg);
        gradeSpin->setRange(0,5); gradeSpin->setSpecialValueText("—"); gradeSpin->setValue(grade);
        auto* gl = new QHBoxLayout;
        gl->addWidget(new QLabel("Оценка:", &dlg)); gl->addWidget(gradeSpin);
        vl->addLayout(gl);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel, &dlg);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        vl->addWidget(bb);

        if (dlg.exec() == QDialog::Accepted) {
            int na = absentCb->isChecked() ? 1 : 0;
            int ng = gradeSpin->value();
            if (na != absent)          saveAttendance(rid, sid, na);
            if (ng != grade && ng > 0) saveGrade(rid, sid, ng);
            if (ng == 0 && grade > 0)  saveGrade(rid, sid, 0);
            QTimer::singleShot(400, this, [this](){ loadJournal(); });
        }
    });
}

void TeacherJournalPage::saveGrade(int rec_id, int student_id, int grade)
{
    QSettings s("NagaevM","studentApp");
    QJsonObject body;
    body["token"]      = s.value("auth_token").toString();
    body["rec_id"]     = rec_id;
    body["student_id"] = student_id;
    body["grade"]      = grade;
    auto* r = new Requester(this);
    r->initRequester("127.0.0.1",8081,nullptr);
    r->sendRequest("grades",
        [r](const QJsonObject&){ r->deleteLater(); },
        [r](const QJsonObject& e){ qDebug()<<e; r->deleteLater(); },
        Requester::Type::POST, body.toVariantMap());
}

void TeacherJournalPage::saveAttendance(int rec_id, int student_id, int absent)
{
    QSettings s("NagaevM","studentApp");
    QJsonObject body;
    body["token"]      = s.value("auth_token").toString();
    body["rec_id"]     = rec_id;
    body["student_id"] = student_id;
    body["absent"]     = absent;
    auto* r = new Requester(this);
    r->initRequester("127.0.0.1",8081,nullptr);
    r->sendRequest("teacher/attendance",
        [r](const QJsonObject&){ r->deleteLater(); },
        [r](const QJsonObject& e){ qDebug()<<e; r->deleteLater(); },
        Requester::Type::POST, body.toVariantMap());
}
