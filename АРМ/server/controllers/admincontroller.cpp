#include "admincontroller.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>
#include <QCryptographicHash>
#include <QDebug>

static QString hashPw(const QString& plain) {
    return QCryptographicHash::hash(plain.toUtf8(),
        QCryptographicHash::Sha256).toBase64();
}

// Только is_admin=1 может управлять через /admin/*
static bool isAdmin(const QString& token) {
    QSqlQuery q;
    q.prepare("SELECT 1 FROM employee WHERE token = :t AND is_admin = 1");
    q.bindValue(":t", token);
    q.exec();
    return q.next();
}

// Любой employee — для чтения списков (студенты группы, преподаватели)
static bool isEmployee(const QString& token) {
    QSqlQuery q;
    q.prepare("SELECT 1 FROM employee WHERE token = :t");
    q.bindValue(":t", token);
    q.exec();
    return q.next();
}

adminController::adminController(QHttpServer& server)
{
    // ══ ГРУППЫ ════════════════════════════════════════════════════════════════

    // GET /admin/groups?token=   — читать может любой employee
    server.route("/admin/groups", QHttpServerRequest::Method::Get,
    [](const QHttpServerRequest& request) {
        QString token = QUrlQuery(request.url().query()).queryItemValue("token");
        if (!isEmployee(token)) {
            QJsonObject e; e["error"] = "Нет доступа";
            return QHttpServerResponse(e, QHttpServerResponse::StatusCode::Forbidden);
        }
        QSqlQuery q(
            "SELECT lg.id, lg.name,"
            " s.id AS hid, s.name AS hname,"
            " t.id AS cid, t.name AS cname"
            " FROM learn_groups lg"
            " LEFT JOIN students s ON s.id = lg.headman_id"
            " LEFT JOIN teachers t ON t.id = lg.curator_id"
            " ORDER BY lg.name");
        QJsonArray arr;
        while (q.next()) {
            QJsonObject g;
            g["id"]           = q.value("id").toInt();
            g["name"]         = q.value("name").toString();
            g["headman_id"]   = q.value("hid").isNull()   ? QJsonValue() : q.value("hid").toInt();
            g["headman_name"] = q.value("hname").toString();
            g["curator_id"]   = q.value("cid").isNull()   ? QJsonValue() : q.value("cid").toInt();
            g["curator_name"] = q.value("cname").toString();
            arr.append(g);
        }
        QJsonObject res; res["data"] = arr;
        return QHttpServerResponse(res, QHttpServerResponse::StatusCode::Ok);
    });

    // POST /admin/groups — только админ
    server.route("/admin/groups", QHttpServerRequest::Method::Post,
    [](const QHttpServerRequest& request) {
        auto body = QJsonDocument::fromJson(request.body()).object();
        if (!isAdmin(body["token"].toString())) {
            QJsonObject e; e["error"] = "Нет доступа";
            return QHttpServerResponse(e, QHttpServerResponse::StatusCode::Forbidden);
        }
        QString name = body["name"].toString().trimmed();
        if (name.isEmpty()) {
            QJsonObject e; e["error"] = "Название обязательно";
            return QHttpServerResponse(e, QHttpServerResponse::StatusCode::BadRequest);
        }
        QSqlQuery q;
        q.prepare("INSERT INTO learn_groups (name) VALUES (:n)");
        q.bindValue(":n", name);
        if (!q.exec()) {
            QJsonObject e; e["error"] = q.lastError().text();
            return QHttpServerResponse(e, QHttpServerResponse::StatusCode::InternalServerError);
        }
        QJsonObject ok; ok["id"] = q.lastInsertId().toInt();
        return QHttpServerResponse(ok, QHttpServerResponse::StatusCode::Ok);
    });

    // PATCH /admin/groups/<id> — только админ
    server.route("/admin/groups/<arg>", QHttpServerRequest::Method::Patch,
    [](int gid, const QHttpServerRequest& request) {
        auto body = QJsonDocument::fromJson(request.body()).object();
        if (!isAdmin(body["token"].toString())) {
            QJsonObject e; e["error"] = "Нет доступа";
            return QHttpServerResponse(e, QHttpServerResponse::StatusCode::Forbidden);
        }
        if (body.contains("name")) {
            QSqlQuery q;
            q.prepare("UPDATE learn_groups SET name=:n WHERE id=:id");
            q.bindValue(":n", body["name"].toString());
            q.bindValue(":id", gid); q.exec();
        }
        if (body.contains("headman_id")) {
            QSqlQuery q;
            q.prepare("UPDATE learn_groups SET headman_id=:h WHERE id=:id");
            q.bindValue(":h",  body["headman_id"].isNull() ? QVariant() : body["headman_id"].toInt());
            q.bindValue(":id", gid); q.exec();
        }
        if (body.contains("curator_id")) {
            QSqlQuery q;
            q.prepare("UPDATE learn_groups SET curator_id=:c WHERE id=:id");
            q.bindValue(":c",  body["curator_id"].isNull() ? QVariant() : body["curator_id"].toInt());
            q.bindValue(":id", gid); q.exec();
        }
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
    });

    // DELETE /admin/groups/<id>?token= — только админ
    server.route("/admin/groups/<arg>", QHttpServerRequest::Method::Delete,
    [](int gid, const QHttpServerRequest& request) {
        QString token = QUrlQuery(request.url().query()).queryItemValue("token");
        if (!isAdmin(token)) {
            QJsonObject e; e["error"] = "Нет доступа";
            return QHttpServerResponse(e, QHttpServerResponse::StatusCode::Forbidden);
        }
        QSqlQuery q;
        q.prepare("DELETE FROM learn_groups WHERE id=:id");
        q.bindValue(":id", gid); q.exec();
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
    });

    // ══ СТУДЕНТЫ ══════════════════════════════════════════════════════════════

    // GET /admin/students?token=&group_id= — любой employee
    server.route("/admin/students", QHttpServerRequest::Method::Get,
    [](const QHttpServerRequest& request) {
        QUrlQuery uq(request.url().query());
        if (!isEmployee(uq.queryItemValue("token"))) {
            QJsonObject e; e["error"] = "Нет доступа";
            return QHttpServerResponse(e, QHttpServerResponse::StatusCode::Forbidden);
        }
        QString gidStr = uq.queryItemValue("group_id");
        QSqlQuery q;
        if (gidStr.isEmpty())
            q.exec("SELECT id, name, email, group_id FROM students ORDER BY group_id, name");
        else {
            q.prepare("SELECT id, name, email, group_id FROM students WHERE group_id=:gid ORDER BY name");
            q.bindValue(":gid", gidStr.toInt()); q.exec();
        }
        QJsonArray arr;
        while (q.next()) {
            QJsonObject s;
            s["id"]       = q.value("id").toInt();
            s["name"]     = q.value("name").toString();
            s["email"]    = q.value("email").toString();
            s["group_id"] = q.value("group_id").toInt();
            arr.append(s);
        }
        QJsonObject res; res["data"] = arr;
        return QHttpServerResponse(res, QHttpServerResponse::StatusCode::Ok);
    });

    // POST/PATCH/DELETE /admin/students — только админ
    server.route("/admin/students", QHttpServerRequest::Method::Post,
    [](const QHttpServerRequest& request) {
        auto body = QJsonDocument::fromJson(request.body()).object();
        if (!isAdmin(body["token"].toString())) {
            QJsonObject e; e["error"] = "Нет доступа";
            return QHttpServerResponse(e, QHttpServerResponse::StatusCode::Forbidden);
        }
        QSqlQuery q;
        q.prepare("INSERT INTO students (name, email, password, group_id) VALUES (:n,:e,:p,:g)");
        q.bindValue(":n", body["name"].toString());
        q.bindValue(":e", body["email"].toString());
        q.bindValue(":p", hashPw(body["password"].toString()));
        q.bindValue(":g", body["group_id"].toInt());
        if (!q.exec()) {
            QJsonObject e; e["error"] = q.lastError().text();
            return QHttpServerResponse(e, QHttpServerResponse::StatusCode::InternalServerError);
        }
        QJsonObject ok; ok["id"] = q.lastInsertId().toInt();
        return QHttpServerResponse(ok, QHttpServerResponse::StatusCode::Ok);
    });

    server.route("/admin/students/<arg>", QHttpServerRequest::Method::Patch,
    [](int sid, const QHttpServerRequest& request) {
        auto body = QJsonDocument::fromJson(request.body()).object();
        if (!isAdmin(body["token"].toString())) {
            QJsonObject e; e["error"] = "Нет доступа";
            return QHttpServerResponse(e, QHttpServerResponse::StatusCode::Forbidden);
        }
        auto upd = [&](const QString& col, const QJsonValue& val) {
            QSqlQuery q;
            q.prepare(QString("UPDATE students SET %1=:v WHERE id=:id").arg(col));
            q.bindValue(":v", val.toVariant()); q.bindValue(":id", sid); q.exec();
        };
        if (body.contains("name"))     upd("name",     body["name"]);
        if (body.contains("email"))    upd("email",    body["email"]);
        if (body.contains("group_id")) upd("group_id", body["group_id"]);
        if (body.contains("password")) {
            QSqlQuery q;
            q.prepare("UPDATE students SET password=:p WHERE id=:id");
            q.bindValue(":p", hashPw(body["password"].toString()));
            q.bindValue(":id", sid); q.exec();
        }
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
    });

    server.route("/admin/students/<arg>", QHttpServerRequest::Method::Delete,
    [](int sid, const QHttpServerRequest& request) {
        QString token = QUrlQuery(request.url().query()).queryItemValue("token");
        if (!isAdmin(token)) {
            QJsonObject e; e["error"] = "Нет доступа";
            return QHttpServerResponse(e, QHttpServerResponse::StatusCode::Forbidden);
        }
        QSqlQuery q;
        q.prepare("DELETE FROM students WHERE id=:id");
        q.bindValue(":id", sid); q.exec();
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
    });

    // ══ ПРЕПОДАВАТЕЛИ ═════════════════════════════════════════════════════════

    // GET /admin/teachers?token= — любой employee
    server.route("/admin/teachers", QHttpServerRequest::Method::Get,
    [](const QHttpServerRequest& request) {
        QString token = QUrlQuery(request.url().query()).queryItemValue("token");
        if (!isEmployee(token)) {
            QJsonObject e; e["error"] = "Нет доступа";
            return QHttpServerResponse(e, QHttpServerResponse::StatusCode::Forbidden);
        }
        QSqlQuery q(
            "SELECT t.id, t.name, t.is_curator,"
            " e.email AS emp_email, e.id AS emp_id, e.is_admin"
            " FROM teachers t"
            " LEFT JOIN employee e ON e.teacher_id = t.id"
            " ORDER BY t.name");
        QJsonArray arr;
        while (q.next()) {
            QJsonObject t;
            t["id"]         = q.value("id").toInt();
            t["name"]       = q.value("name").toString();
            t["is_curator"] = q.value("is_curator").toInt();
            t["emp_email"]  = q.value("emp_email").toString();
            t["emp_id"]     = q.value("emp_id").toInt();
            t["is_admin"]   = q.value("is_admin").toInt();
            arr.append(t);
        }
        QJsonObject res; res["data"] = arr;
        return QHttpServerResponse(res, QHttpServerResponse::StatusCode::Ok);
    });

    // POST/PATCH/DELETE /admin/teachers — только админ
    server.route("/admin/teachers", QHttpServerRequest::Method::Post,
    [](const QHttpServerRequest& request) {
        auto body = QJsonDocument::fromJson(request.body()).object();
        if (!isAdmin(body["token"].toString())) {
            QJsonObject e; e["error"] = "Нет доступа";
            return QHttpServerResponse(e, QHttpServerResponse::StatusCode::Forbidden);
        }
        QString name      = body["name"].toString().trimmed();
        QString empEmail  = body["emp_email"].toString().trimmed();
        QString password  = body["password"].toString();
        int     isCurator = body["is_curator"].toInt(0);
        int     isAdminV  = body["is_admin"].toInt(0);

        QSqlQuery qt;
        qt.prepare("INSERT INTO teachers (name, is_curator) VALUES (:n, :c)");
        qt.bindValue(":n", name); qt.bindValue(":c", isCurator);
        if (!qt.exec()) {
            QJsonObject e; e["error"] = qt.lastError().text();
            return QHttpServerResponse(e, QHttpServerResponse::StatusCode::InternalServerError);
        }
        int tid = qt.lastInsertId().toInt();

        QSqlQuery qe;
        qe.prepare("INSERT INTO employee (name, email, password, teacher_id, is_admin) VALUES (:n,:e,:p,:t,:a)");
        qe.bindValue(":n", name); qe.bindValue(":e", empEmail);
        qe.bindValue(":p", hashPw(password)); qe.bindValue(":t", tid);
        qe.bindValue(":a", isAdminV);
        qe.exec();

        QJsonObject ok; ok["id"] = tid;
        return QHttpServerResponse(ok, QHttpServerResponse::StatusCode::Ok);
    });

    server.route("/admin/teachers/<arg>", QHttpServerRequest::Method::Patch,
    [](int tid, const QHttpServerRequest& request) {
        auto body = QJsonDocument::fromJson(request.body()).object();
        if (!isAdmin(body["token"].toString())) {
            QJsonObject e; e["error"] = "Нет доступа";
            return QHttpServerResponse(e, QHttpServerResponse::StatusCode::Forbidden);
        }
        if (body.contains("name")) {
            QSqlQuery q;
            q.prepare("UPDATE teachers SET name=:n WHERE id=:id");
            q.bindValue(":n", body["name"].toString()); q.bindValue(":id", tid); q.exec();
            QSqlQuery q2;
            q2.prepare("UPDATE employee SET name=:n WHERE teacher_id=:id");
            q2.bindValue(":n", body["name"].toString()); q2.bindValue(":id", tid); q2.exec();
        }
        if (body.contains("is_curator")) {
            QSqlQuery q;
            q.prepare("UPDATE teachers SET is_curator=:c WHERE id=:id");
            q.bindValue(":c", body["is_curator"].toInt()); q.bindValue(":id", tid); q.exec();
        }
        if (body.contains("emp_email")) {
            QSqlQuery q;
            q.prepare("UPDATE employee SET email=:e WHERE teacher_id=:id");
            q.bindValue(":e", body["emp_email"].toString()); q.bindValue(":id", tid); q.exec();
        }
        if (body.contains("password")) {
            QSqlQuery q;
            q.prepare("UPDATE employee SET password=:p WHERE teacher_id=:id");
            q.bindValue(":p", hashPw(body["password"].toString())); q.bindValue(":id", tid); q.exec();
        }
        if (body.contains("is_admin")) {
            QSqlQuery q;
            q.prepare("UPDATE employee SET is_admin=:a WHERE teacher_id=:id");
            q.bindValue(":a", body["is_admin"].toInt()); q.bindValue(":id", tid); q.exec();
        }
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
    });

    server.route("/admin/teachers/<arg>", QHttpServerRequest::Method::Delete,
    [](int tid, const QHttpServerRequest& request) {
        QString token = QUrlQuery(request.url().query()).queryItemValue("token");
        if (!isAdmin(token)) {
            QJsonObject e; e["error"] = "Нет доступа";
            return QHttpServerResponse(e, QHttpServerResponse::StatusCode::Forbidden);
        }
        QSqlQuery qe;
        qe.prepare("DELETE FROM employee WHERE teacher_id=:id");
        qe.bindValue(":id", tid); qe.exec();
        QSqlQuery qt;
        qt.prepare("DELETE FROM teachers WHERE id=:id");
        qt.bindValue(":id", tid); qt.exec();
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
    });

    // ══ TEACHER_GROUPS ════════════════════════════════════════════════════════

    server.route("/admin/teacher_groups", QHttpServerRequest::Method::Post,
    [](const QHttpServerRequest& request) {
        auto body = QJsonDocument::fromJson(request.body()).object();
        if (!isAdmin(body["token"].toString())) {
            QJsonObject e; e["error"] = "Нет доступа";
            return QHttpServerResponse(e, QHttpServerResponse::StatusCode::Forbidden);
        }
        QSqlQuery q;
        q.prepare("INSERT OR IGNORE INTO teacher_groups (teacher_id, group_id) VALUES (:t,:g)");
        q.bindValue(":t", body["teacher_id"].toInt());
        q.bindValue(":g", body["group_id"].toInt());
        q.exec();
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
    });

    server.route("/admin/teacher_groups", QHttpServerRequest::Method::Delete,
    [](const QHttpServerRequest& request) {
        QUrlQuery uq(request.url().query());
        if (!isAdmin(uq.queryItemValue("token"))) {
            QJsonObject e; e["error"] = "Нет доступа";
            return QHttpServerResponse(e, QHttpServerResponse::StatusCode::Forbidden);
        }
        QSqlQuery q;
        q.prepare("DELETE FROM teacher_groups WHERE teacher_id=:t AND group_id=:g");
        q.bindValue(":t", uq.queryItemValue("teacher_id").toInt());
        q.bindValue(":g", uq.queryItemValue("group_id").toInt());
        q.exec();
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
    });
}
