#include "gradescontroller.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QHttpServerResponse>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>
#include <QDateTime>
#include <QDebug>

gradesController::gradesController(QHttpServer& server) {

    // ── POST /grades  {token, student_id, rec_id, grade} ─────────────────────
    // Только преподаватель, который ведёт ЭТУ ПАРУ (проверяется по rec_id).
    server.route("/grades", QHttpServerRequest::Method::Post,
    [](const QHttpServerRequest& request) {
        const QJsonObject body = QJsonDocument::fromJson(request.body()).object();
        QString token      = body["token"].toString();
        int     student_id = body["student_id"].toInt();
        int     rec_id     = body["rec_id"].toInt();
        int     grade      = body["grade"].toInt();

        if (grade < 1 || grade > 5) {
            QJsonObject err; err["error"] = "Оценка 1–5";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }

        // Проверяем через employee.token (не teachers.token!)
        QSqlQuery check;
        check.prepare(
            "SELECT 1 FROM journal_rec jr"
            " JOIN classes c ON c.id = jr.class_id"
            " JOIN teachers t ON t.id = c.teacher_id"
            " JOIN employee e ON e.teacher_id = t.id"
            " WHERE jr.id = :rid AND e.token = :token");
        check.bindValue(":rid",   rec_id);
        check.bindValue(":token", token);
        check.exec();
        if (!check.next()) {
            QJsonObject err; err["error"] = "Нет доступа";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
        }

        QSqlQuery upsert;
        upsert.prepare(
            "INSERT INTO grades (rec_id, student_id, grade)"
            " VALUES (:rid, :sid, :grade)"
            " ON CONFLICT(rec_id, student_id) DO UPDATE SET grade = excluded.grade");
        upsert.bindValue(":rid",   rec_id);
        upsert.bindValue(":sid",   student_id);
        upsert.bindValue(":grade", grade);
        if (!upsert.exec()) {
            QJsonObject err; err["error"] = "Ошибка БД";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
        }
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
    });

    // ── GET /grades/<student_id>?token= ──────────────────────────────────────
    // Оценки студента для преподавателя — только по предметам этого препода.
    server.route("/grades/<arg>", QHttpServerRequest::Method::Get,
    [](int student_id, const QHttpServerRequest& request) {
        QString token = QUrlQuery(request.url().query()).queryItemValue("token");

        // Проверяем доступ: препод ведёт группу этого студента
        QSqlQuery check;
        check.prepare(
            "SELECT t.id FROM teacher_groups tg"
            " JOIN teachers t ON t.id = tg.teacher_id"
            " JOIN employee e ON e.teacher_id = t.id"
            " JOIN students s ON s.group_id = tg.group_id"
            " WHERE e.token = :token AND s.id = :sid");
        check.bindValue(":token", token);
        check.bindValue(":sid",   student_id);
        check.exec();
        if (!check.next()) {
            QJsonObject err; err["error"] = "Нет доступа";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
        }

        QSqlQuery q;
        q.prepare(
            "SELECT g.id, g.grade, jr.date, jr.theme,"
            " (SELECT name FROM subjects WHERE id = c.subject_id) AS subject"
            " FROM grades g"
            " JOIN journal_rec jr ON jr.id = g.rec_id"
            " JOIN classes c ON c.id = jr.class_id"
            " WHERE g.student_id = :sid"
            " ORDER BY jr.date");
        q.bindValue(":sid", student_id);
        q.exec();

        QJsonArray data;
        while (q.next()) {
            QJsonObject item;
            item["id"]      = q.value("id").toInt();
            item["grade"]   = q.value("grade").toInt();
            item["date"]    = QDateTime::fromSecsSinceEpoch(q.value("date").toLongLong())
                                  .toString("dd.MM.yyyy");
            item["theme"]   = q.value("theme").toString();
            item["subject"] = q.value("subject").toString();
            data.append(item);
        }
        QJsonObject res; res["data"] = data;
        return QHttpServerResponse(res, QHttpServerResponse::StatusCode::Ok);
    });

    // ── POST /teacher_marks ───────────────────────────────────────────────────
    // {token, student_id, subject_id, mark, comment, mark_date}
    // mark: 1=плюс, 2-5=оценка. Можно несколько.
    // mark_date: "yyyy-MM-dd" — дата выставления (выбирается преподавателем).
    server.route("/teacher_marks", QHttpServerRequest::Method::Post,
    [](const QHttpServerRequest& request) {
        const QJsonObject body = QJsonDocument::fromJson(request.body()).object();
        QString token      = body["token"].toString();
        int     student_id = body["student_id"].toInt();
        int     subject_id = body["subject_id"].toInt();
        int     mark       = body["mark"].toInt();
        QString comment    = body["comment"].toString();
        QString mark_date  = body["mark_date"].toString(); // yyyy-MM-dd, пусто = сегодня

        if (mark < 1 || mark > 5) {
            QJsonObject err; err["error"] = "Оценка 1–5 (1=плюс)";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }
        if (mark_date.isEmpty())
            mark_date = QDate::currentDate().toString("yyyy-MM-dd");

        // Проверяем что преподаватель ведёт этот предмет в группе студента
        QSqlQuery check;
        check.prepare(
            "SELECT t.id FROM classes c"
            " JOIN teachers t ON t.id = c.teacher_id"
            " JOIN employee e ON e.teacher_id = t.id"
            " JOIN students s ON s.group_id = c.group_id"
            " WHERE e.token = :tok AND s.id = :sid AND c.subject_id = :subj"
            " LIMIT 1");
        check.bindValue(":tok",  token);
        check.bindValue(":sid",  student_id);
        check.bindValue(":subj", subject_id);
        check.exec();
        if (!check.next()) {
            QJsonObject err; err["error"] = "Нет доступа или предмет не ваш";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
        }
        int teacher_id = check.value("id").toInt();

        QSqlQuery ins;
        ins.prepare(
            "INSERT INTO teacher_marks (student_id, teacher_id, subject_id, mark, comment, mark_date)"
            " VALUES (:sid, :tid, :subj, :mark, :comment, :date)");
        ins.bindValue(":sid",     student_id);
        ins.bindValue(":tid",     teacher_id);
        ins.bindValue(":subj",    subject_id);
        ins.bindValue(":mark",    mark);
        ins.bindValue(":comment", comment);
        ins.bindValue(":date",    mark_date);
        if (!ins.exec()) {
            qDebug() << "teacher_marks insert:" << ins.lastError().text();
            QJsonObject err; err["error"] = "Ошибка БД";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
        }
        QJsonObject ok; ok["id"] = ins.lastInsertId().toInt();
        return QHttpServerResponse(ok, QHttpServerResponse::StatusCode::Ok);
    });

    // ── DELETE /teacher_marks/<id>?token= ────────────────────────────────────
    server.route("/teacher_marks/<arg>", QHttpServerRequest::Method::Delete,
    [](int mark_id, const QHttpServerRequest& request) {
        QString token = QUrlQuery(request.url().query()).queryItemValue("token");
        QSqlQuery check;
        check.prepare(
            "SELECT 1 FROM teacher_marks tm"
            " JOIN employee e ON e.teacher_id = tm.teacher_id"
            " WHERE tm.id = :id AND e.token = :tok");
        check.bindValue(":id",  mark_id);
        check.bindValue(":tok", token);
        check.exec();
        if (!check.next()) {
            QJsonObject err; err["error"] = "Нет доступа";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
        }
        QSqlQuery del;
        del.prepare("DELETE FROM teacher_marks WHERE id=:id");
        del.bindValue(":id", mark_id);
        del.exec();
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
    });

    // ── GET /teacher_marks/<student_id>?token= ────────────────────────────────
    // Для препода — только его оценки студенту.
    // Для студента (по его token) — все его оценки от всех преподов.
    server.route("/teacher_marks/<arg>", QHttpServerRequest::Method::Get,
    [](int student_id, const QHttpServerRequest& request) {
        QString token = QUrlQuery(request.url().query()).queryItemValue("token");

        QSqlQuery chkTeacher;
        chkTeacher.prepare(
            "SELECT t.id FROM teacher_groups tg"
            " JOIN teachers t ON t.id = tg.teacher_id"
            " JOIN employee e ON e.teacher_id = t.id"
            " JOIN students s ON s.group_id = tg.group_id"
            " WHERE e.token = :tok AND s.id = :sid");
        chkTeacher.bindValue(":tok", token);
        chkTeacher.bindValue(":sid", student_id);
        chkTeacher.exec();
        bool isTeacher = chkTeacher.next();
        int  teacherId = isTeacher ? chkTeacher.value("id").toInt() : 0;

        QSqlQuery chkStudent;
        chkStudent.prepare("SELECT 1 FROM students WHERE token=:tok AND id=:sid");
        chkStudent.bindValue(":tok", token);
        chkStudent.bindValue(":sid", student_id);
        chkStudent.exec();
        bool isStudent = chkStudent.next();

        if (!isTeacher && !isStudent) {
            QJsonObject err; err["error"] = "Нет доступа";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
        }

        QSqlQuery q;
        // Препод видит только свои оценки, студент — все
        if (isTeacher && !isStudent) {
            q.prepare(
                "SELECT tm.id, tm.mark, tm.comment, tm.mark_date,"
                " (SELECT name FROM subjects WHERE id=tm.subject_id) AS subject,"
                " t.name AS teacher_name"
                " FROM teacher_marks tm"
                " JOIN teachers t ON t.id = tm.teacher_id"
                " WHERE tm.student_id = :sid AND tm.teacher_id = :tid"
                " ORDER BY tm.mark_date DESC, tm.created_at DESC");
            q.bindValue(":sid", student_id);
            q.bindValue(":tid", teacherId);
        } else {
            q.prepare(
                "SELECT tm.id, tm.mark, tm.comment, tm.mark_date,"
                " (SELECT name FROM subjects WHERE id=tm.subject_id) AS subject,"
                " t.name AS teacher_name"
                " FROM teacher_marks tm"
                " JOIN teachers t ON t.id = tm.teacher_id"
                " WHERE tm.student_id = :sid"
                " ORDER BY tm.mark_date DESC, tm.created_at DESC");
            q.bindValue(":sid", student_id);
        }
        q.exec();

        QJsonArray arr;
        while (q.next()) {
            QJsonObject item;
            item["id"]           = q.value("id").toInt();
            item["mark"]         = q.value("mark").toInt();
            item["comment"]      = q.value("comment").toString();
            item["date"]         = q.value("mark_date").toString(); // yyyy-MM-dd
            item["subject"]      = q.value("subject").toString();
            item["teacher_name"] = q.value("teacher_name").toString();
            arr.append(item);
        }
        QJsonObject res; res["data"] = arr;
        return QHttpServerResponse(res, QHttpServerResponse::StatusCode::Ok);
    });
}
