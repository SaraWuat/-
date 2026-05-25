#include "studentcontroller.h"

#include <QString>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlTableModel>
#include <QSqlRecord>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUrlQuery>
#include <QDateTime>
#include <QDebug>

QString studentController::generateAuthToken() {
    const QString chars("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    QString token;
    for (int i = 0; i < 32; i++)
        token.append(chars.at(QRandomGenerator::global()->bounded(chars.length())));
    return token;
}

bool studentController::validatePassword(const QSqlRecord& rec, const QString& password) {
    QString hash = QCryptographicHash::hash(
        password.toUtf8(), QCryptographicHash::Sha256).toBase64();
    return rec.value("password").toString() == hash;
}

studentController::studentController(QHttpServer& server) {

    // ── POST /student/login ───────────────────────────────────────────────────
    server.route("/student/login", QHttpServerRequest::Method::Post,
    [this](const QHttpServerRequest& request) {
        const QJsonObject body = QJsonDocument::fromJson(request.body()).object();
        QSqlTableModel* m = new QSqlTableModel(nullptr, QSqlDatabase::database());
        m->setTable("students");
        m->setFilter(QString("email = '%1'").arg(body["email"].toString()));
        m->select();
        if (m->rowCount() != 1) {
            delete m;
            QJsonObject err; err["error"] = "Пользователь не найден";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
        }
        QSqlRecord rec = m->record(0);
        if (!validatePassword(rec, body["password"].toString())) {
            delete m;
            QJsonObject err; err["error"] = "Неверный пароль";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Unauthorized);
        }
        QString token = generateAuthToken();
        rec.setValue("token", token);
        m->setRecord(0, rec);
        m->submit();
        delete m;
        QJsonObject ok;
        ok["token"] = token;
        ok["name"]  = rec.value("name").toString();
        return QHttpServerResponse(ok, QHttpServerResponse::StatusCode::Ok);
    });

    // ── POST /employee/login ──────────────────────────────────────────────────
    server.route("/employee/login", QHttpServerRequest::Method::Post,
    [this](const QHttpServerRequest& request) {
        const QJsonObject body = QJsonDocument::fromJson(request.body()).object();
        QSqlTableModel* m = new QSqlTableModel(nullptr, QSqlDatabase::database());
        m->setTable("employee");
        m->setFilter(QString("email = '%1'").arg(body["email"].toString()));
        m->select();
        if (m->rowCount() != 1) {
            delete m;
            QJsonObject err; err["error"] = "Пользователь не найден";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
        }
        QSqlRecord rec = m->record(0);
        if (!validatePassword(rec, body["password"].toString())) {
            delete m;
            QJsonObject err; err["error"] = "Неверный пароль";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Unauthorized);
        }
        QString token = generateAuthToken();
        rec.setValue("token", token);
        m->setRecord(0, rec);
        m->submit();
        int teacherId = rec.value("teacher_id").toInt();
        int isAdmin   = rec.value("is_admin").toInt();
        delete m;
        if (teacherId > 0) {
            QSqlQuery tq;
            tq.prepare("UPDATE teachers SET token=:t WHERE id=:id");
            tq.bindValue(":t",  token);
            tq.bindValue(":id", teacherId);
            tq.exec();
        }
        QJsonObject ok;
        ok["token"]    = token;
        ok["name"]     = rec.value("name").toString();
        ok["is_admin"] = isAdmin;
        return QHttpServerResponse(ok, QHttpServerResponse::StatusCode::Ok);
    });

    // ── GET /student/info/<token> ─────────────────────────────────────────────
    server.route("/student/info/<arg>", QHttpServerRequest::Method::Get,
    [](const QString& token) {
        QSqlQuery q;
        q.prepare(
            "SELECT s.id, s.group_id,"
            " CASE WHEN lg.headman_id=s.id THEN 1 ELSE 0 END AS is_headman"
            " FROM students s"
            " JOIN learn_groups lg ON lg.id=s.group_id"
            " WHERE s.token=:token");
        q.bindValue(":token", token);
        q.exec();
        if (!q.next()) {
            QJsonObject err; err["error"] = "Пользователь не найден";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
        }
        QJsonObject res;
        res["id"]         = q.value("id").toInt();
        res["group_id"]   = q.value("group_id").toInt();
        res["is_headman"] = q.value("is_headman").toBool();
        return QHttpServerResponse(res, QHttpServerResponse::StatusCode::Ok);
    });

    // ── GET /student/journalrec/teacher/<class_id>?token= ────────────────────
    // ИСПРАВЛЕНИЕ: если journal_rec не существует — возвращаем студентов
    // с absent=0 и submitted=false, запись создастся при /journal/fill
    server.route("/student/journalrec/teacher/<arg>", QHttpServerRequest::Method::Get,
    [](int class_id, const QHttpServerRequest& request) {
        QString token = QUrlQuery(request.url().query()).queryItemValue("token");

        // Проверка доступа
        QSqlQuery isTeacher;
        isTeacher.prepare(
            "SELECT 1 FROM classes c"
            " JOIN teachers t ON t.id=c.teacher_id"
            " JOIN employee e ON e.teacher_id=t.id"
            " WHERE c.id=:cid AND e.token=:token");
        isTeacher.bindValue(":cid",   class_id);
        isTeacher.bindValue(":token", token);
        isTeacher.exec();
        bool teacherOk = isTeacher.next();

        QSqlQuery isHeadman;
        isHeadman.prepare(
            "SELECT 1 FROM classes c"
            " JOIN learn_groups lg ON lg.id=c.group_id"
            " JOIN students s ON s.id=lg.headman_id"
            " WHERE c.id=:cid AND s.token=:token");
        isHeadman.bindValue(":cid",   class_id);
        isHeadman.bindValue(":token", token);
        isHeadman.exec();
        bool headmanOk = isHeadman.next();

        if (!teacherOk && !headmanOk) {
            QJsonObject err; err["error"] = "Нет доступа";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
        }

        // Ищем запись журнала (может не существовать — это нормально)
        QSqlQuery recQ;
        recQ.prepare(
            "SELECT id, headman_accept FROM journal_rec"
            " WHERE class_id=:cid"
            " ORDER BY headman_accept ASC, id DESC"
            " LIMIT 1");
        recQ.bindValue(":cid", class_id);
        recQ.exec();

        bool submitted = false;
        int  recId     = 0;
        if (recQ.next()) {
            recId     = recQ.value("id").toInt();
            submitted = recQ.value("headman_accept").toInt() == 1;
        }
        // Если recId==0 — записи нет, submitted=false, absent у всех 0

        // Студенты группы с отметками
        QSqlQuery q;
        q.prepare(
            "SELECT s.id, s.name,"
            " CASE WHEN jrd.student_id IS NOT NULL THEN 1 ELSE 0 END AS absent"
            " FROM classes c"
            " JOIN learn_groups g ON g.id=c.group_id"
            " JOIN students s ON s.group_id=g.id"
            " LEFT JOIN journal_rec_data jrd"
            "   ON jrd.rec_id=:rid AND jrd.student_id=s.id"
            " WHERE c.id=:cid"
            " ORDER BY s.name");
        q.bindValue(":rid", recId);
        q.bindValue(":cid", class_id);
        q.exec();

        QJsonArray data;
        while (q.next()) {
            QJsonObject obj;
            obj["id"]     = q.value("id").toInt();
            obj["name"]   = q.value("name").toString();
            obj["absent"] = q.value("absent").toInt();
            obj["to_add"] = 0;
            data.append(obj);
        }

        if (data.isEmpty()) {
            QJsonObject err; err["error"] = "Студенты не найдены";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
        }

        // Тема занятия
        QString theme;
        if (submitted && recId > 0) {
            QSqlQuery thQ;
            thQ.prepare("SELECT theme FROM journal_rec WHERE id=:rid");
            thQ.bindValue(":rid", recId);
            thQ.exec();
            if (thQ.next()) theme = thQ.value("theme").toString();
        }

        QSqlQuery subQ;
        subQ.prepare(
            "SELECT name FROM subjects"
            " WHERE id=(SELECT subject_id FROM classes WHERE id=:cid)");
        subQ.bindValue(":cid", class_id);
        subQ.exec();

        QJsonObject res;
        res["data"]      = data;
        res["name"]      = subQ.next() ? subQ.value("name").toString() : "";
        res["submitted"] = submitted;
        res["theme"]     = theme;
        return QHttpServerResponse(res, QHttpServerResponse::StatusCode::Ok);
    });

    // ── GET /student/grades/<token> ───────────────────────────────────────────
    server.route("/student/grades/<arg>", QHttpServerRequest::Method::Get,
    [](const QString& token) {
        QSqlQuery sidQ;
        sidQ.prepare("SELECT id FROM students WHERE token=:t");
        sidQ.bindValue(":t", token);
        sidQ.exec();
        if (!sidQ.next()) {
            QJsonObject err; err["error"] = "Токен не найден";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
        }
        int sid = sidQ.value("id").toInt();

        QSqlQuery q;
        q.prepare(
            "SELECT g.grade, jr.date, jr.theme,"
            " (SELECT name FROM subjects WHERE id=c.subject_id) AS subject"
            " FROM grades g"
            " JOIN journal_rec jr ON jr.id=g.rec_id"
            " JOIN classes c ON c.id=jr.class_id"
            " WHERE g.student_id=:sid"
            " ORDER BY jr.date");
        q.bindValue(":sid", sid);
        q.exec();

        QJsonArray arr;
        while (q.next()) {
            QJsonObject item;
            item["grade"]   = q.value("grade").toInt();
            item["date"]    = QDateTime::fromSecsSinceEpoch(q.value("date").toLongLong())
                                  .toString("dd.MM.yyyy");
            item["theme"]   = q.value("theme").toString();
            item["subject"] = q.value("subject").toString();
            arr.append(item);
        }
        QJsonObject res; res["data"] = arr;
        return QHttpServerResponse(res, QHttpServerResponse::StatusCode::Ok);
    });
}
