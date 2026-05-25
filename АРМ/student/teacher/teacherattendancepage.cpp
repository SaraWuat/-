#include "teacherattendancepage.h"
#include "ui_teacherattendancepage.h"
#include "../components/requester/requester.h"

#include <QSettings>
#include <QJsonArray>
#include <QJsonObject>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QComboBox>
#include <QDebug>

TeacherAttendancePage::TeacherAttendancePage(QWidget *parent, int group_id)
    : QMainWindow(parent), ui(new Ui::TeacherAttendancePage), m_groupId(group_id)
{
    ui->setupUi(this);
    setWindowTitle("Посещаемость группы");
    connect(ui->backButton, &QPushButton::clicked, this, [this](){ close(); });

    // При смене предмета — перестраиваем таблицу
    connect(ui->subjectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        int sid = ui->subjectCombo->currentData().toInt();
        if (sid > 0) buildTable(sid);
    });

    loadSubjects();
}

TeacherAttendancePage::~TeacherAttendancePage() { delete ui; }

// ── Шаг 1: список предметов группы ───────────────────────────────────────────
void TeacherAttendancePage::loadSubjects()
{
    QSettings s("NagaevM","studentApp");
    auto* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("subjects/group/%1?token=%2")
            .arg(m_groupId).arg(s.value("auth_token").toString()),
        [this, r](const QJsonObject &data) {
            m_subjects = data["data"].toArray();
            ui->subjectCombo->blockSignals(true);
            ui->subjectCombo->clear();
            for (const QJsonValue &v : m_subjects) {
                QJsonObject subj = v.toObject();
                ui->subjectCombo->addItem(subj["name"].toString(), subj["id"].toInt());
            }
            ui->subjectCombo->blockSignals(false);
            r->deleteLater();
            loadJournal();
        },
        [r](const QJsonObject& e){ qDebug()<<e; r->deleteLater(); },
        Requester::Type::GET
    );
}

// ── Шаг 2: загружаем полный журнал группы ────────────────────────────────────
void TeacherAttendancePage::loadJournal()
{
    QSettings s("NagaevM","studentApp");
    auto* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("journal/group/%1?token=%2").arg(m_groupId).arg(s.value("auth_token").toString()),
        [this, r](const QJsonObject &data) {
            m_journalData = data;
            m_students    = data["students"].toArray();
            r->deleteLater();
            // Строим таблицу для первого предмета
            if (ui->subjectCombo->count() > 0)
                buildTable(ui->subjectCombo->currentData().toInt());
        },
        [r](const QJsonObject& e){ qDebug()<<e; r->deleteLater(); },
        Requester::Type::GET
    );
}

// ── Шаг 3: строим таблицу студенты × занятия для выбранного предмета ─────────
// Строки = студенты, столбцы = дата занятия
// Пусто = был, "Н" красным = отсутствовал
void TeacherAttendancePage::buildTable(int subject_id)
{
    // Находим имя предмета
    QString subjName;
    for (const QJsonValue &v : m_subjects)
        if (v.toObject()["id"].toInt() == subject_id) {
            subjName = v.toObject()["name"].toString(); break;
        }
    ui->subjectLabel->setText(subjName);

    QJsonArray allRecs  = m_journalData["recs"].toArray();
    QJsonArray allData  = m_journalData["data"].toArray();

    // Фильтруем только записи этого предмета
    QJsonArray recs;
    for (const QJsonValue &v : allRecs)
        if (v.toObject()["subject"].toString() == subjName)
            recs.append(v);

    // Строим карту absent: rec_id -> set<student_id>
    QMap<int, QSet<int>> absentMap;
    for (const QJsonValue &v : allData) {
        QJsonObject item = v.toObject();
        if (item["absent"].toInt() == 1)
            absentMap[item["rec_id"].toInt()].insert(item["student_id"].toInt());
    }

    QTableWidget* t = ui->attTable;
    t->clear();
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->verticalHeader()->setVisible(false);

    int nS = m_students.size();
    int nR = recs.size();

    if (nR == 0) {
        t->setRowCount(1); t->setColumnCount(1);
        t->setItem(0,0,new QTableWidgetItem("Нет занятий по этому предмету"));
        return;
    }

    // Колонки: имя | дата1 | дата2 | ... | итог
    t->setRowCount(nS);
    t->setColumnCount(nR + 2); // +1 имя, +1 итог
    t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    t->setColumnWidth(nR+1, 55);
    for (int j = 0; j < nR; j++)
        t->setColumnWidth(j+1, 58);

    QStringList headers; headers << "ФИО";
    for (const QJsonValue &v : recs)
        headers << v.toObject()["date"].toString();
    headers << "Итог";
    t->setHorizontalHeaderLabels(headers);

    auto mk = [](const QString& s, bool absent = false) {
        auto* it = new QTableWidgetItem(s);
        it->setTextAlignment(Qt::AlignCenter);
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        if (absent) {
            it->setBackground(QColor("#f8d7da"));
            it->setForeground(QBrush(Qt::red));
        }
        return it;
    };

    for (int i = 0; i < nS; i++) {
        QJsonObject stu = m_students[i].toObject();
        int sid = stu["id"].toInt();

        auto* ni = new QTableWidgetItem(stu["name"].toString());
        ni->setFlags(ni->flags() & ~Qt::ItemIsEditable);
        t->setItem(i, 0, ni);

        int missedCount = 0;
        for (int j = 0; j < nR; j++) {
            int rid = recs[j].toObject()["rec_id"].toInt();
            bool absent = absentMap.value(rid).contains(sid);
            if (absent) missedCount++;
            t->setItem(i, j+1, mk(absent ? "Н" : "", absent));
        }

        // Итог: пропущено/всего
        int pct = nR > 0 ? qRound((nR - missedCount) * 100.0 / nR) : 0;
        auto* ti = mk(QString("%1/%2").arg(nR - missedCount).arg(nR));
        ti->setBackground(pct>=75 ? QColor("#d4edda") : pct>=50 ? QColor("#fff3cd") : QColor("#f8d7da"));
        t->setItem(i, nR+1, ti);
    }

    t->resizeRowsToContents();
}
