#include "attendanceinputpage.h"
#include "ui_attendanceinputpage.h"
#include "../components/requester/requester.h"

#include <QSettings>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QCheckBox>
#include <QHeaderView>
#include <QDate>
#include <QMessageBox>

AttendanceInputPage::AttendanceInputPage(QWidget *parent, int group_id)
    : QMainWindow(parent)
    , ui(new Ui::AttendanceInputPage)
    , m_groupId(group_id)
{
    ui->setupUi(this);
    setWindowTitle("Отметка присутствия");

    // Диапазон: с начала семестра до сегодня
    QDate semStart(QDate::currentDate().month() >= 9
                   ? QDate::currentDate().year()
                   : QDate::currentDate().year() - 1,
                   9, 1);
    ui->dateEdit->setMinimumDate(semStart);
    ui->dateEdit->setMaximumDate(QDate::currentDate());
    ui->dateEdit->setDate(QDate::currentDate());
    ui->dateEdit->setDisplayFormat("dd.MM.yyyy");

    connect(ui->backButton, &QPushButton::clicked,
            this, [this](){ close(); });

    connect(ui->submitButton, &QPushButton::clicked, this, [this](){
        if (m_editMode) submitEdit();
        else            submit();
    });

    connect(ui->editButton, &QPushButton::clicked,
            this, [this](){ enableEditMode(); });

    connect(ui->classCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){
        int cid = ui->classCombo->currentData().toInt();
        if (cid > 0) loadStudents(cid);
    });

    connect(ui->dateEdit, &QDateEdit::dateChanged,
            this, [this](const QDate &date){
        loadClassesForDate(date);
    });

    loadClassesForDate(QDate::currentDate());
}

AttendanceInputPage::~AttendanceInputPage()
{
    delete ui;
}

// ─────────────────────────────────────────────────────────────────────────────
void AttendanceInputPage::loadClassesForDate(const QDate &date)
{
    QSettings s("NagaevM", "studentApp");

    Requester* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("classes/date/%1/%2")
            .arg(s.value("auth_token").toString())
            .arg(date.toString("yyyy-MM-dd")),
        [this, r](const QJsonObject &data) {
            QJsonArray arr = data["data"].toArray();
            ui->classCombo->blockSignals(true);
            ui->classCombo->clear();
            for (const QJsonValue &v : arr) {
                QJsonObject obj = v.toObject();
                QString typeStr = obj["type"].toString();
                QString label = obj["time"].toString() + "  "
                    + obj["subject"].toString();
                if (!typeStr.isEmpty())
                    label += " (" + typeStr + ")";
                ui->classCombo->addItem(label, obj["id"].toInt());
            }
            ui->classCombo->blockSignals(false);

            if (arr.isEmpty()) {
                ui->studentsTable->clearContents();
                ui->studentsTable->setRowCount(0);
                ui->statusLabel->setText("На эту дату пар нет");
                ui->statusLabel->setStyleSheet("color: gray;");
                ui->submitButton->setVisible(false);
                ui->editButton->setVisible(false);
            } else {
                loadStudents(ui->classCombo->currentData().toInt());
            }
            r->deleteLater();
        },
        [this, r](const QJsonObject &) {
            ui->statusLabel->setText("Ошибка загрузки пар");
            ui->statusLabel->setStyleSheet("color: red;");
            r->deleteLater();
        },
        Requester::Type::GET
    );
}

// ─────────────────────────────────────────────────────────────────────────────
void AttendanceInputPage::loadStudents(int class_id)
{
    m_classId  = class_id;
    m_editMode = false;
    QSettings s("NagaevM", "studentApp");

    Requester* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("student/journalrec/teacher/%1?token=%2")
            .arg(class_id)
            .arg(s.value("auth_token").toString()),
        [this, r](const QJsonObject &data) {
            QJsonArray arr  = data["data"].toArray();
            m_students      = QJsonArray();
            m_alreadySubmitted = data["submitted"].toBool(false);

            if (m_alreadySubmitted && !data["theme"].toString().isEmpty())
                ui->themeEdit->setPlainText(data["theme"].toString());
            else if (!m_alreadySubmitted)
                ui->themeEdit->clear();

            QTableWidget* t = ui->studentsTable;
            t->clear();
            t->setColumnCount(2);
            t->setHorizontalHeaderLabels({"ФИО", "Отсутствует"});
            t->setRowCount(arr.size());
            t->setEditTriggers(QAbstractItemView::NoEditTriggers);
            t->verticalHeader()->setVisible(false);
            t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
            t->setColumnWidth(1, 100);

            for (int i = 0; i < arr.size(); i++) {
                QJsonObject obj = arr[i].toObject();
                m_students.append(obj);

                auto* ni = new QTableWidgetItem(obj["name"].toString());
                ni->setFlags(ni->flags() & ~Qt::ItemIsEditable);
                t->setItem(i, 0, ni);

                QCheckBox* cb = new QCheckBox();
                cb->setStyleSheet("margin-left: 30px;");
                cb->setChecked(obj["absent"].toInt() == 1);
                cb->setEnabled(!m_alreadySubmitted);

                if (!m_alreadySubmitted) {
                    int sid = obj["id"].toInt();
                    connect(cb, &QCheckBox::checkStateChanged,
                            [this, sid, cb](int){
                        for (int j = 0; j < m_students.size(); j++) {
                            QJsonObject st = m_students[j].toObject();
                            if (st["id"].toInt() == sid) {
                                st["absent"]   = cb->isChecked() ? 1 : 0;
                                m_students[j]  = st;
                                break;
                            }
                        }
                    });
                }
                t->setCellWidget(i, 1, cb);
            }
            t->resizeRowsToContents();
            setReadOnlyMode(m_alreadySubmitted);
            r->deleteLater();
        },
        [r](const QJsonObject &err){
            qDebug() << "loadStudents error:" << err;
            r->deleteLater();
        },
        Requester::Type::GET
    );
}

// ─────────────────────────────────────────────────────────────────────────────
void AttendanceInputPage::setReadOnlyMode(bool readOnly)
{
    ui->submitButton->setVisible(!readOnly);
    ui->editButton->setVisible(readOnly);
    ui->themeEdit->setReadOnly(readOnly);

    if (readOnly) {
        ui->statusLabel->setText("✓ Посещаемость отмечена. Нажмите «Редактировать» для изменения.");
        ui->statusLabel->setStyleSheet("color: steelblue; font-weight: bold;");
    } else {
        ui->statusLabel->clear();
        ui->statusLabel->setStyleSheet("");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void AttendanceInputPage::enableEditMode()
{
    m_editMode = true;
    ui->editButton->setVisible(false);
    ui->submitButton->setVisible(true);
    ui->submitButton->setText("💾  Сохранить изменения");
    ui->themeEdit->setReadOnly(false);
    ui->statusLabel->setText("⚠ Режим редактирования — внесите изменения и сохраните");
    ui->statusLabel->setStyleSheet("color: darkorange; font-weight: bold;");

    // Разблокируем чекбоксы и подключаем сигналы
    QTableWidget* t = ui->studentsTable;
    for (int i = 0; i < t->rowCount(); i++) {
        QCheckBox* cb = qobject_cast<QCheckBox*>(t->cellWidget(i, 1));
        if (!cb) continue;
        cb->setEnabled(true);

        int sid = m_students[i].toObject()["id"].toInt();
        connect(cb, &QCheckBox::checkStateChanged,
                [this, sid, cb](int){
            for (int j = 0; j < m_students.size(); j++) {
                QJsonObject st = m_students[j].toObject();
                if (st["id"].toInt() == sid) {
                    st["absent"]   = cb->isChecked() ? 1 : 0;
                    m_students[j]  = st;
                    break;
                }
            }
        });
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void AttendanceInputPage::submit()
{
    if (m_classId == 0 || m_alreadySubmitted) return;

    QSettings s("NagaevM", "studentApp");
    QJsonObject body;
    body["token"]    = s.value("auth_token").toString();
    body["class_id"] = m_classId;
    body["theme"]    = ui->themeEdit->toPlainText();
    body["data"]     = m_students;

    ui->submitButton->setEnabled(false);
    Requester* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        "journal/fill",
        [this, r](const QJsonObject &){
            m_alreadySubmitted = true;
            m_editMode         = false;
            ui->submitButton->setText("✅  Подтвердить");
            setReadOnlyMode(true);
            r->deleteLater();
        },
        [this, r](const QJsonObject &err){
            ui->statusLabel->setText("✗ " + err["error"].toString());
            ui->statusLabel->setStyleSheet("color: red; font-weight: bold;");
            ui->submitButton->setEnabled(true);
            r->deleteLater();
        },
        Requester::Type::POST,
        body.toVariantMap()
    );
}

// ─────────────────────────────────────────────────────────────────────────────
void AttendanceInputPage::submitEdit()
{
    if (m_classId == 0) return;

    QSettings s("NagaevM", "studentApp");
    QJsonObject body;
    body["token"]    = s.value("auth_token").toString();
    body["class_id"] = m_classId;
    body["theme"]    = ui->themeEdit->toPlainText();
    body["data"]     = m_students;

    ui->submitButton->setEnabled(false);
    Requester* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        "journal/edit",
        [this, r](const QJsonObject &){
            m_alreadySubmitted = true;
            m_editMode         = false;
            ui->submitButton->setText("✅  Подтвердить");
            setReadOnlyMode(true);
            r->deleteLater();
        },
        [this, r](const QJsonObject &err){
            ui->statusLabel->setText("✗ " + err["error"].toString());
            ui->statusLabel->setStyleSheet("color: red; font-weight: bold;");
            ui->submitButton->setEnabled(true);
            r->deleteLater();
        },
        Requester::Type::POST,
        body.toVariantMap()
    );
}
