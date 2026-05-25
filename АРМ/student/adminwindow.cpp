#include "adminwindow.h"
#include "ui_adminwindow.h"
#include "components/requester/requester.h"
#include "login.h"

#include <QSettings>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QListWidget>
#include <QListWidgetItem>
#include <QInputDialog>
#include <QMessageBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QDebug>

// ── helpers ───────────────────────────────────────────────────────────────────
static QString token() {
    return QSettings("NagaevM","studentApp").value("auth_token").toString();
}

static Requester* req(QObject* parent) {
    auto* r = new Requester(parent);
    r->initRequester("127.0.0.1", 8081, nullptr);
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
AdminWindow::AdminWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::AdminWindow)
{
    ui->setupUi(this);
    setWindowTitle("Администрирование");

    // ── выход ──────────────────────────────────────────────────────────────
    connect(ui->logoutButton, &QPushButton::clicked, this, [this]() {
        QSettings s("NagaevM","studentApp");
        s.remove("auth_token"); s.remove("name"); s.remove("role");
        (new login())->show(); close();
    });

    // ── группы ─────────────────────────────────────────────────────────────
    connect(ui->addGroupButton,    &QPushButton::clicked, this, &AdminWindow::addGroup);
    connect(ui->deleteGroupButton, &QPushButton::clicked, this, &AdminWindow::deleteGroup);
    connect(ui->renameGroupButton, &QPushButton::clicked, this, &AdminWindow::renameGroup);
    connect(ui->headmanButton,     &QPushButton::clicked, this, &AdminWindow::assignHeadman);
    connect(ui->curatorButton,     &QPushButton::clicked, this, &AdminWindow::assignCurator);

    connect(ui->groupsList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= m_groups.size()) return;
        loadStudents(m_groups[row].toObject()["id"].toInt());
    });

    // ── студенты ───────────────────────────────────────────────────────────
    connect(ui->addStudentButton,    &QPushButton::clicked, this, &AdminWindow::addStudent);
    connect(ui->deleteStudentButton, &QPushButton::clicked, this, &AdminWindow::deleteStudent);
    connect(ui->editStudentButton,   &QPushButton::clicked, this, &AdminWindow::editStudent);

    // ── преподаватели ───────────────────────────────────────────────────────
    connect(ui->addTeacherButton,    &QPushButton::clicked, this, &AdminWindow::addTeacher);
    connect(ui->deleteTeacherButton, &QPushButton::clicked, this, &AdminWindow::deleteTeacher);
    connect(ui->editTeacherButton,   &QPushButton::clicked, this, &AdminWindow::editTeacher);

    loadGroups();
    loadTeachers();
}

AdminWindow::~AdminWindow() { delete ui; }

// ═══════════════════════════════════════════════════════════════════════════════
// ЗАГРУЗКА
// ═══════════════════════════════════════════════════════════════════════════════
void AdminWindow::loadGroups()
{
    auto* r = req(this);
    r->sendRequest(
        QString("admin/groups?token=%1").arg(token()),
        [this, r](const QJsonObject &data) {
            m_groups = data["data"].toArray();
            fillGroupsList();
            r->deleteLater();
        },
        [this, r](const QJsonObject &e) {
            showStatus("Ошибка загрузки групп: " + e["error"].toString(), false);
            r->deleteLater();
        },
        Requester::Type::GET
    );
}

void AdminWindow::loadTeachers()
{
    auto* r = req(this);
    r->sendRequest(
        QString("admin/teachers?token=%1").arg(token()),
        [this, r](const QJsonObject &data) {
            m_teachers = data["data"].toArray();
            fillTeachersList();
            r->deleteLater();
        },
        [this, r](const QJsonObject &e) {
            showStatus("Ошибка загрузки преподавателей: " + e["error"].toString(), false);
            r->deleteLater();
        },
        Requester::Type::GET
    );
}

void AdminWindow::loadStudents(int group_id)
{
    auto* r = req(this);
    r->sendRequest(
        QString("admin/students?token=%1&group_id=%2").arg(token()).arg(group_id),
        [this, r](const QJsonObject &data) {
            m_students = data["data"].toArray();
            fillStudentsList();
            r->deleteLater();
        },
        [this, r](const QJsonObject &e) {
            showStatus("Ошибка загрузки студентов: " + e["error"].toString(), false);
            r->deleteLater();
        },
        Requester::Type::GET
    );
}

// ═══════════════════════════════════════════════════════════════════════════════
// ЗАПОЛНЕНИЕ СПИСКОВ
// ═══════════════════════════════════════════════════════════════════════════════
void AdminWindow::fillGroupsList()
{
    ui->groupsList->clear();
    for (const QJsonValue &v : m_groups) {
        QJsonObject g = v.toObject();
        QString label = g["name"].toString();
        if (!g["headman_name"].toString().isEmpty())
            label += "  [ст: " + g["headman_name"].toString().split(" ").first() + "]";
        if (!g["curator_name"].toString().isEmpty())
            label += "  [кур: " + g["curator_name"].toString().split(" ").first() + "]";
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, g["id"].toInt());
        ui->groupsList->addItem(item);
    }
}

void AdminWindow::fillStudentsList()
{
    ui->studentsList->clear();
    for (const QJsonValue &v : m_students) {
        QJsonObject s = v.toObject();
        auto* item = new QListWidgetItem(s["name"].toString() + "  <" + s["email"].toString() + ">");
        item->setData(Qt::UserRole, s["id"].toInt());
        ui->studentsList->addItem(item);
    }
    // Заголовок: кол-во студентов
    ui->studentsGroupLabel->setText(
        QString("Студенты (%1):").arg(m_students.size()));
}

void AdminWindow::fillTeachersList()
{
    ui->teachersList->clear();
    for (const QJsonValue &v : m_teachers) {
        QJsonObject t = v.toObject();
        QString label = t["name"].toString();
        if (t["is_curator"].toInt()) label += "  🎓";
        label += "  <" + t["emp_email"].toString() + ">";
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, t["id"].toInt());
        ui->teachersList->addItem(item);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ДЕЙСТВИЯ: ГРУППЫ
// ═══════════════════════════════════════════════════════════════════════════════
void AdminWindow::addGroup()
{
    bool ok;
    QString name = QInputDialog::getText(this, "Новая группа", "Название:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    QJsonObject body; body["token"] = token(); body["name"] = name.trimmed();
    auto* r = req(this);
    r->sendRequest("admin/groups",
        [this, r](const QJsonObject&) { loadGroups(); showStatus("Группа добавлена"); r->deleteLater(); },
        [this, r](const QJsonObject& e) { showStatus(e["error"].toString(), false); r->deleteLater(); },
        Requester::Type::POST, body.toVariantMap());
}

void AdminWindow::deleteGroup()
{
    int gid = currentGroupId();
    if (gid <= 0) return;
    if (QMessageBox::question(this, "Удалить группу",
        "Удалить группу? Студенты останутся в БД без группы.") != QMessageBox::Yes) return;

    auto* r = req(this);
    r->sendRequest(QString("admin/groups/%1?token=%2").arg(gid).arg(token()),
        [this, r](const QJsonObject&) { loadGroups(); showStatus("Группа удалена"); r->deleteLater(); },
        [this, r](const QJsonObject& e) { showStatus(e["error"].toString(), false); r->deleteLater(); },
        Requester::Type::DELET);
}

void AdminWindow::renameGroup()
{
    int gid = currentGroupId();
    if (gid <= 0) return;
    bool ok;
    QString name = QInputDialog::getText(this, "Переименовать", "Новое название:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    QJsonObject body; body["token"] = token(); body["name"] = name.trimmed();
    auto* r = req(this);
    r->sendRequest(QString("admin/groups/%1").arg(gid),
        [this, r](const QJsonObject&) { loadGroups(); showStatus("Группа переименована"); r->deleteLater(); },
        [this, r](const QJsonObject& e) { showStatus(e["error"].toString(), false); r->deleteLater(); },
        Requester::Type::PATCH, body.toVariantMap());
}

void AdminWindow::assignHeadman()
{
    int gid = currentGroupId();
    if (gid <= 0 || m_students.isEmpty()) { showStatus("Выберите группу со студентами", false); return; }

    QDialog dlg(this); dlg.setWindowTitle("Назначить старосту");
    auto* vl = new QVBoxLayout(&dlg);
    auto* combo = new QComboBox(&dlg);
    combo->addItem("— нет старосты —", 0);
    for (const QJsonValue &v : m_students) {
        QJsonObject s = v.toObject();
        combo->addItem(s["name"].toString(), s["id"].toInt());
    }
    vl->addWidget(new QLabel("Студент:"));
    vl->addWidget(combo);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    vl->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted) return;

    QJsonObject body; body["token"] = token();
    body["headman_id"] = combo->currentData().toInt() == 0 ? QJsonValue() : combo->currentData().toInt();
    auto* r = req(this);
    r->sendRequest(QString("admin/groups/%1").arg(gid),
        [this, r](const QJsonObject&) { loadGroups(); showStatus("Староста назначен"); r->deleteLater(); },
        [this, r](const QJsonObject& e) { showStatus(e["error"].toString(), false); r->deleteLater(); },
        Requester::Type::PATCH, body.toVariantMap());
}

void AdminWindow::assignCurator()
{
    int gid = currentGroupId();
    if (gid <= 0) { showStatus("Выберите группу", false); return; }

    QDialog dlg(this); dlg.setWindowTitle("Назначить куратора");
    auto* vl = new QVBoxLayout(&dlg);
    auto* combo = new QComboBox(&dlg);
    combo->addItem("— нет куратора —", 0);
    for (const QJsonValue &v : m_teachers) {
        QJsonObject t = v.toObject();
        combo->addItem(t["name"].toString(), t["id"].toInt());
    }
    vl->addWidget(new QLabel("Преподаватель:"));
    vl->addWidget(combo);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    vl->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted) return;

    QJsonObject body; body["token"] = token();
    body["curator_id"] = combo->currentData().toInt() == 0 ? QJsonValue() : combo->currentData().toInt();
    auto* r = req(this);
    r->sendRequest(QString("admin/groups/%1").arg(gid),
        [this, r](const QJsonObject&) { loadGroups(); showStatus("Куратор назначен"); r->deleteLater(); },
        [this, r](const QJsonObject& e) { showStatus(e["error"].toString(), false); r->deleteLater(); },
        Requester::Type::PATCH, body.toVariantMap());
}

// ═══════════════════════════════════════════════════════════════════════════════
// ДЕЙСТВИЯ: СТУДЕНТЫ
// ═══════════════════════════════════════════════════════════════════════════════
void AdminWindow::addStudent()
{
    int gid = currentGroupId();
    if (gid <= 0) { showStatus("Выберите группу", false); return; }

    QDialog dlg(this); dlg.setWindowTitle("Новый студент");
    auto* form = new QFormLayout(&dlg);
    auto* nameEdit  = new QLineEdit(&dlg);
    auto* emailEdit = new QLineEdit(&dlg);
    auto* pwEdit    = new QLineEdit(&dlg);
    pwEdit->setText("password");
    form->addRow("ФИО:",    nameEdit);
    form->addRow("Email:",  emailEdit);
    form->addRow("Пароль:", pwEdit);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;

    if (nameEdit->text().trimmed().isEmpty() || emailEdit->text().trimmed().isEmpty()) {
        showStatus("ФИО и email обязательны", false); return;
    }

    QJsonObject body;
    body["token"]    = token();
    body["name"]     = nameEdit->text().trimmed();
    body["email"]    = emailEdit->text().trimmed();
    body["password"] = pwEdit->text();
    body["group_id"] = gid;
    auto* r = req(this);
    r->sendRequest("admin/students",
        [this, r, gid](const QJsonObject&) {
            loadStudents(gid); showStatus("Студент добавлен"); r->deleteLater();
        },
        [this, r](const QJsonObject& e) { showStatus(e["error"].toString(), false); r->deleteLater(); },
        Requester::Type::POST, body.toVariantMap());
}

void AdminWindow::deleteStudent()
{
    int sid = currentStudentId();
    if (sid <= 0) return;
    if (QMessageBox::question(this, "Удалить студента",
        "Удалить студента из БД?") != QMessageBox::Yes) return;

    auto* r = req(this);
    r->sendRequest(QString("admin/students/%1?token=%2").arg(sid).arg(token()),
        [this, r](const QJsonObject&) {
            int gid = currentGroupId();
            if (gid > 0) loadStudents(gid);
            showStatus("Студент удалён"); r->deleteLater();
        },
        [this, r](const QJsonObject& e) { showStatus(e["error"].toString(), false); r->deleteLater(); },
        Requester::Type::DELET);
}

void AdminWindow::editStudent()
{
    int sid = currentStudentId();
    if (sid <= 0) return;

    // Находим текущие данные
    QJsonObject cur;
    for (const QJsonValue &v : m_students)
        if (v.toObject()["id"].toInt() == sid) { cur = v.toObject(); break; }

    QDialog dlg(this); dlg.setWindowTitle("Редактировать студента");
    auto* form = new QFormLayout(&dlg);
    auto* nameEdit  = new QLineEdit(cur["name"].toString(), &dlg);
    auto* emailEdit = new QLineEdit(cur["email"].toString(), &dlg);
    auto* pwEdit    = new QLineEdit(&dlg);
    pwEdit->setPlaceholderText("оставьте пустым, чтобы не менять");

    // Сменить группу
    auto* groupCombo = new QComboBox(&dlg);
    for (const QJsonValue &v : m_groups) {
        QJsonObject g = v.toObject();
        groupCombo->addItem(g["name"].toString(), g["id"].toInt());
        if (g["id"].toInt() == cur["group_id"].toInt())
            groupCombo->setCurrentIndex(groupCombo->count()-1);
    }

    form->addRow("ФИО:",    nameEdit);
    form->addRow("Email:",  emailEdit);
    form->addRow("Пароль:", pwEdit);
    form->addRow("Группа:", groupCombo);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;

    QJsonObject body; body["token"] = token();
    body["name"]     = nameEdit->text().trimmed();
    body["email"]    = emailEdit->text().trimmed();
    body["group_id"] = groupCombo->currentData().toInt();
    if (!pwEdit->text().isEmpty()) body["password"] = pwEdit->text();

    auto* r = req(this);
    r->sendRequest(QString("admin/students/%1").arg(sid),
        [this, r](const QJsonObject&) {
            int gid = currentGroupId();
            if (gid > 0) loadStudents(gid);
            loadGroups();
            showStatus("Студент обновлён"); r->deleteLater();
        },
        [this, r](const QJsonObject& e) { showStatus(e["error"].toString(), false); r->deleteLater(); },
        Requester::Type::PATCH, body.toVariantMap());
}

// ═══════════════════════════════════════════════════════════════════════════════
// ДЕЙСТВИЯ: ПРЕПОДАВАТЕЛИ
// ═══════════════════════════════════════════════════════════════════════════════
void AdminWindow::addTeacher()
{
    QDialog dlg(this); dlg.setWindowTitle("Новый преподаватель");
    auto* form = new QFormLayout(&dlg);
    auto* nameEdit  = new QLineEdit(&dlg);
    auto* emailEdit = new QLineEdit(&dlg);
    auto* pwEdit    = new QLineEdit(&dlg);
    pwEdit->setText("password");
    auto* curatorCb = new QCheckBox("Куратор", &dlg);
    form->addRow("ФИО:",        nameEdit);
    form->addRow("Email входа:", emailEdit);
    form->addRow("Пароль:",      pwEdit);
    form->addRow("",             curatorCb);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;

    if (nameEdit->text().trimmed().isEmpty()) { showStatus("ФИО обязательно", false); return; }

    QJsonObject body;
    body["token"]      = token();
    body["name"]       = nameEdit->text().trimmed();
    body["emp_email"]  = emailEdit->text().trimmed();
    body["password"]   = pwEdit->text();
    body["is_curator"] = curatorCb->isChecked() ? 1 : 0;
    auto* r = req(this);
    r->sendRequest("admin/teachers",
        [this, r](const QJsonObject&) { loadTeachers(); showStatus("Преподаватель добавлен"); r->deleteLater(); },
        [this, r](const QJsonObject& e) { showStatus(e["error"].toString(), false); r->deleteLater(); },
        Requester::Type::POST, body.toVariantMap());
}

void AdminWindow::deleteTeacher()
{
    int tid = currentTeacherId();
    if (tid <= 0) return;
    if (QMessageBox::question(this, "Удалить преподавателя",
        "Удалить преподавателя и его учётную запись?") != QMessageBox::Yes) return;

    auto* r = req(this);
    r->sendRequest(QString("admin/teachers/%1?token=%2").arg(tid).arg(token()),
        [this, r](const QJsonObject&) { loadTeachers(); showStatus("Преподаватель удалён"); r->deleteLater(); },
        [this, r](const QJsonObject& e) { showStatus(e["error"].toString(), false); r->deleteLater(); },
        Requester::Type::DELET);
}

void AdminWindow::editTeacher()
{
    int tid = currentTeacherId();
    if (tid <= 0) return;

    QJsonObject cur;
    for (const QJsonValue &v : m_teachers)
        if (v.toObject()["id"].toInt() == tid) { cur = v.toObject(); break; }

    QDialog dlg(this); dlg.setWindowTitle("Редактировать преподавателя");
    auto* form = new QFormLayout(&dlg);
    auto* nameEdit  = new QLineEdit(cur["name"].toString(), &dlg);
    auto* emailEdit = new QLineEdit(cur["emp_email"].toString(), &dlg);
    auto* pwEdit    = new QLineEdit(&dlg);
    pwEdit->setPlaceholderText("оставьте пустым, чтобы не менять");
    auto* curatorCb = new QCheckBox("Куратор", &dlg);
    curatorCb->setChecked(cur["is_curator"].toInt() == 1);
    form->addRow("ФИО:",        nameEdit);
    form->addRow("Email входа:", emailEdit);
    form->addRow("Пароль:",      pwEdit);
    form->addRow("",             curatorCb);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;

    QJsonObject body; body["token"] = token();
    body["name"]       = nameEdit->text().trimmed();
    body["emp_email"]  = emailEdit->text().trimmed();
    body["is_curator"] = curatorCb->isChecked() ? 1 : 0;
    if (!pwEdit->text().isEmpty()) body["password"] = pwEdit->text();

    auto* r = req(this);
    r->sendRequest(QString("admin/teachers/%1").arg(tid),
        [this, r](const QJsonObject&) { loadTeachers(); showStatus("Преподаватель обновлён"); r->deleteLater(); },
        [this, r](const QJsonObject& e) { showStatus(e["error"].toString(), false); r->deleteLater(); },
        Requester::Type::PATCH, body.toVariantMap());
}

// ═══════════════════════════════════════════════════════════════════════════════
// УТИЛИТЫ
// ═══════════════════════════════════════════════════════════════════════════════
int AdminWindow::currentGroupId() const {
    auto* item = ui->groupsList->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : 0;
}

int AdminWindow::currentStudentId() const {
    auto* item = ui->studentsList->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : 0;
}

int AdminWindow::currentTeacherId() const {
    auto* item = ui->teachersList->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : 0;
}

void AdminWindow::showStatus(const QString& msg, bool ok) {
    ui->statusLabel->setText(msg);
    ui->statusLabel->setStyleSheet(ok ? "color: green; font-weight: bold;"
                                      : "color: red; font-weight: bold;");
}
