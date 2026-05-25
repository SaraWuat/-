#include "teachergradespage.h"
#include "ui_teachergradespage.h"
#include "../components/requester/requester.h"

#include <QSettings>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QDialog>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QInputDialog>
#include <QMap>
#include <QDebug>

TeacherGradesPage::TeacherGradesPage(QWidget *parent, int group_id, int teacher_id)
    : QMainWindow(parent)
    , ui(new Ui::TeacherGradesPage)
    , m_groupId(group_id)
    , m_teacherId(teacher_id)
{
    ui->setupUi(this);
    setWindowTitle("Оценки группы");
    connect(ui->backButton,    &QPushButton::clicked, this, [this](){ close(); });
    connect(ui->refreshButton, &QPushButton::clicked, this, [this](){ loadSubjects(); });
    loadSubjects();
}

TeacherGradesPage::~TeacherGradesPage() { delete ui; }

// ── Шаг 1: предметы этого препода ────────────────────────────────────────────
void TeacherGradesPage::loadSubjects()
{
    QSettings s("NagaevM","studentApp");
    auto* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("teacher/subjects/%1").arg(s.value("auth_token").toString()),
        [this, r](const QJsonObject &data) {
            m_subjects = data["data"].toArray();
            r->deleteLater();
            loadStudents();
        },
        [r](const QJsonObject& e){ qDebug()<<e; r->deleteLater(); },
        Requester::Type::GET
    );
}

// ── Шаг 2: студенты группы ───────────────────────────────────────────────────
void TeacherGradesPage::loadStudents()
{
    QSettings s("NagaevM","studentApp");
    auto* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("admin/students?token=%1&group_id=%2")
            .arg(s.value("auth_token").toString()).arg(m_groupId),
        [this, r](const QJsonObject &data) {
            m_students = data["data"].toArray();
            r->deleteLater();
            loadMarks();
        },
        [r](const QJsonObject& e){ qDebug()<<e; r->deleteLater(); },
        Requester::Type::GET
    );
}

// ── Шаг 3: teacher_marks для каждого студента ─────────────────────────────────
void TeacherGradesPage::loadMarks()
{
    if (m_students.isEmpty()) { buildTable(); return; }

    QSettings s("NagaevM","studentApp");
    QString tok = s.value("auth_token").toString();
    m_marksBySubject.clear();

    int* pending = new int(m_students.size());
    for (const QJsonValue &sv : m_students) {
        int sid = sv.toObject()["id"].toInt();
        auto* r = new Requester(this);
        r->initRequester("127.0.0.1", 8081, nullptr);
        r->sendRequest(
            QString("teacher_marks/%1?token=%2").arg(sid).arg(tok),
            [this, r, sid, pending](const QJsonObject &mdata) {
                for (const QJsonValue &mv : mdata["data"].toArray()) {
                    QJsonObject mo = mv.toObject();
                    mo["student_id"] = sid;
                    // ищем subject_id по названию предмета
                    int subjId = 0;
                    for (const QJsonValue &sv2 : m_subjects) {
                        if (sv2.toObject()["name"].toString() == mo["subject"].toString()) {
                            subjId = sv2.toObject()["id"].toInt();
                            break;
                        }
                    }
                    if (subjId > 0)
                        m_marksBySubject[subjId].append(mo);
                }
                (*pending)--;
                if (*pending == 0) { delete pending; buildTable(); }
                r->deleteLater();
            },
            [r, pending, this](const QJsonObject& e) {
                qDebug()<<e;
                (*pending)--;
                if (*pending == 0) { delete pending; buildTable(); }
                r->deleteLater();
            },
            Requester::Type::GET
        );
    }
}

// ── Шаг 4: строим таблицу ─────────────────────────────────────────────────────
void TeacherGradesPage::buildTable()
{
    QTableWidget* t = ui->gradesTable;
    t->clear();
    t->verticalHeader()->setVisible(false);

    int nS    = m_students.size();
    int nSubj = m_subjects.size();

    if (nS == 0 || nSubj == 0) {
        t->setRowCount(1);
        t->setColumnCount(1);
        t->setItem(0, 0, new QTableWidgetItem(nSubj == 0
            ? "У вас нет предметов в этой группе" : "Нет студентов"));
        return;
    }

    // Колонки: Имя | ★ | предмет1 | предмет2 | ...
    t->setRowCount(nS);
    t->setColumnCount(nSubj + 2);
    t->setColumnWidth(0, 185);
    t->setColumnWidth(1, 32);
    t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    t->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    for (int j = 0; j < nSubj; j++)
        t->horizontalHeader()->setSectionResizeMode(j+2, QHeaderView::ResizeToContents);

    QStringList headers;
    headers << "ФИО" << "★";
    for (const QJsonValue &sv : m_subjects)
        headers << sv.toObject()["name"].toString();
    t->setHorizontalHeaderLabels(headers);

    // Двойной клик по заголовку → переименование (удобно для «к/р», «экзамен» и т.д.)
    connect(t->horizontalHeader(), &QHeaderView::sectionDoubleClicked,
            this, [t](int col) {
        if (col < 2) return;
        QString cur = t->horizontalHeaderItem(col)
                        ? t->horizontalHeaderItem(col)->text() : "";
        bool ok;
        QString newName = QInputDialog::getText(
            t, "Переименовать столбец", "Название:", QLineEdit::Normal, cur, &ok);
        if (ok && !newName.isEmpty()) {
            if (!t->horizontalHeaderItem(col))
                t->setHorizontalHeaderItem(col, new QTableWidgetItem(newName));
            else
                t->horizontalHeaderItem(col)->setText(newName);
        }
    });

    for (int i = 0; i < nS; i++) {
        QJsonObject stu = m_students[i].toObject();
        int sid = stu["id"].toInt();

        auto* ni = new QTableWidgetItem(stu["name"].toString());
        ni->setFlags(ni->flags() & ~Qt::ItemIsEditable);
        t->setItem(i, 0, ni);

        // Кнопка ★
        auto* btn = new QPushButton("★");
        btn->setFixedWidth(28);
        btn->setStyleSheet("color: #c47a00; font-weight: bold; padding:0;");
        btn->setToolTip("Добавить оценку / плюс");
        connect(btn, &QPushButton::clicked, this, [this, sid, stu](){
            addMarkDialog(sid, stu["name"].toString());
        });
        t->setCellWidget(i, 1, btn);

        // Ячейки по предметам
        for (int j = 0; j < nSubj; j++) {
            int subjId = m_subjects[j].toObject()["id"].toInt();
            const QJsonArray& marks = m_marksBySubject.value(subjId);

            QStringList parts;
            for (const QJsonValue &mv : marks) {
                QJsonObject mo = mv.toObject();
                if (mo["student_id"].toInt() != sid) continue;
                int val = mo["mark"].toInt();
                QString item = val == 1 ? "+" : QString::number(val);
                if (!mo["comment"].toString().isEmpty())
                    item += "(" + mo["comment"].toString().left(8) + ")";
                parts << item;
            }

            auto* ci = new QTableWidgetItem(parts.join(", "));
            ci->setTextAlignment(Qt::AlignCenter);
            ci->setFlags(ci->flags() & ~Qt::ItemIsEditable);
            if (!parts.isEmpty()) {
                // Цвет по последней оценке
                int lastVal = m_marksBySubject.value(subjId).last().toObject()["mark"].toInt();
                if (lastVal == 1) ci->setBackground(QColor("#cce5ff"));
                else if (lastVal >= 4) ci->setBackground(QColor("#d4edda"));
                else if (lastVal == 3) ci->setBackground(QColor("#fff3cd"));
                else if (lastVal == 2) ci->setBackground(QColor("#f8d7da"));
            }
            t->setItem(i, j+2, ci);
        }
    }
}

// ── Диалог добавления оценки / плюса ─────────────────────────────────────────
void TeacherGradesPage::addMarkDialog(int student_id, const QString& student_name)
{
    QSettings s("NagaevM","studentApp");
    QString tok = s.value("auth_token").toString();

    QDialog dlg(this);
    dlg.setWindowTitle(QString("Оценка: %1").arg(student_name));
    auto* form = new QFormLayout(&dlg);

    auto* subjCombo = new QComboBox(&dlg);
    for (const QJsonValue &sv : m_subjects)
        subjCombo->addItem(sv.toObject()["name"].toString(),
                           sv.toObject()["id"].toInt());

    auto* markCombo = new QComboBox(&dlg);
    markCombo->addItem("+ (плюс)", 1);
    markCombo->addItem("2", 2);
    markCombo->addItem("3", 3);
    markCombo->addItem("4", 4);
    markCombo->addItem("5", 5);

    auto* dateEdit = new QDateEdit(QDate::currentDate(), &dlg);
    dateEdit->setDisplayFormat("dd.MM.yyyy");
    dateEdit->setCalendarPopup(true);
    dateEdit->setMaximumDate(QDate::currentDate());

    auto* commentEdit = new QLineEdit(&dlg);
    commentEdit->setPlaceholderText("Комментарий (необязательно)");

    form->addRow("Предмет:", subjCombo);
    form->addRow("Оценка:",  markCombo);
    form->addRow("Дата:",    dateEdit);
    form->addRow("Заметка:", commentEdit);

    auto* bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);

    if (dlg.exec() != QDialog::Accepted) return;

    QJsonObject body;
    body["token"]      = tok;
    body["student_id"] = student_id;
    body["subject_id"] = subjCombo->currentData().toInt();
    body["mark"]       = markCombo->currentData().toInt();
    body["comment"]    = commentEdit->text();
    body["mark_date"]  = dateEdit->date().toString("yyyy-MM-dd");

    auto* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest("teacher_marks",
        [r, this](const QJsonObject&){ r->deleteLater(); loadSubjects(); },
        [r](const QJsonObject& e){ qDebug()<<e; r->deleteLater(); },
        Requester::Type::POST, body.toVariantMap());
}
