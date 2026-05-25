#include "teachercontroller.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>
#include <QDateTime>
#include <QSet>
#include <QDebug>

teacherController::teacherController(QHttpServer& server)
{
    // ── GET /teacher/info/<token> ─────────────────────────────────────────────
    // Ответ: {id, name, is_curator, curator_group_id, groups:[{id,name}]}
    // Ищет по employee.token (не teachers.token) — именно employee.token
    // выдаётся при /employee/login и хранится в QSettings
    server.route("/teacher/info/<arg>", QHttpServerRequest::Method::Get,
    [](const QString& token) {
        QSqlQuery eq;
        eq.prepare(
            "SELECT t.id, t.name, t.is_curator,"
            " (SELECT lg.id FROM learn_groups lg WHERE lg.curator_id = t.id LIMIT 1) AS cur_gid"
            " FROM teachers t"
            " JOIN employee e ON e.teacher_id = t.id"
            " WHERE e.token = :tok");
        eq.bindValue(":tok", token);
        eq.exec();

        if (!eq.next()) {
            QJsonObject err; err["error"] = "Токен не найден";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
        }

        int  tid       = eq.value("id").toInt();
        bool isCurator = eq.value("is_curator").toInt() == 1;
        int  curGid    = eq.value("cur_gid").isNull() ? 0 : eq.value("cur_gid").toInt();

        // Группы, где ведёт занятия (через teacher_groups)
        QSqlQuery gq;
        gq.prepare(
            "SELECT DISTINCT lg.id, lg.name"
            " FROM learn_groups lg"
            " JOIN teacher_groups tg ON tg.group_id = lg.id"
            " WHERE tg.teacher_id = :tid"
            " ORDER BY lg.name");
        gq.bindValue(":tid", tid);
        gq.exec();

        QJsonArray groups;
        QSet<int> seen;
        while (gq.next()) {
            int gid = gq.value("id").toInt();
            seen.insert(gid);
            QJsonObject g;
            g["id"]   = gid;
            g["name"] = gq.value("name").toString();
            groups.append(g);
        }

        // Куратор видит свою группу даже если там не ведёт пар
        if (isCurator && curGid > 0 && !seen.contains(curGid)) {
            QSqlQuery lgq;
            lgq.prepare("SELECT name FROM learn_groups WHERE id=:id");
            lgq.bindValue(":id", curGid);
            lgq.exec();
            if (lgq.next()) {
                QJsonObject g;
                g["id"]   = curGid;
                g["name"] = lgq.value("name").toString();
                groups.append(g);
            }
        }

        QJsonObject res;
        res["id"]               = tid;
        res["name"]             = eq.value("name").toString();
        res["is_curator"]       = isCurator;
        res["curator_group_id"] = curGid;
        res["groups"]           = groups;
        return QHttpServerResponse(res, QHttpServerResponse::StatusCode::Ok);
    });

    // ── GET /teacher/subjects/<token> ─────────────────────────────────────────
    // Предметы которые ведёт конкретный преподаватель (для фильтра оценок).
    // Возвращает уникальные предметы из classes.
    server.route("/teacher/subjects/<arg>", QHttpServerRequest::Method::Get,
    [](const QString& token) {
        QSqlQuery q;
        q.prepare(
            "SELECT DISTINCT s.id, s.name"
            " FROM subjects s"
            " JOIN classes c ON c.subject_id = s.id"
            " JOIN teachers t ON t.id = c.teacher_id"
            " JOIN employee e ON e.teacher_id = t.id"
            " WHERE e.token = :tok"
            " ORDER BY s.name");
        q.bindValue(":tok", token);
        q.exec();

        QJsonArray arr;
        while (q.next()) {
            QJsonObject item;
            item["id"]   = q.value("id").toInt();
            item["name"] = q.value("name").toString();
            arr.append(item);
        }
        QJsonObject res; res["data"] = arr;
        return QHttpServerResponse(res, QHttpServerResponse::StatusCode::Ok);
    });

    // ── POST /teacher/attendance ──────────────────────────────────────────────
    // {token, rec_id, student_id, absent}
    // Приоритет препода над старостой.
    server.route("/teacher/attendance", QHttpServerRequest::Method::Post,
    [](const QHttpServerRequest& request) {
        const QJsonObject body = QJsonDocument::fromJson(request.body()).object();
        QString token      = body["token"].toString();
        int     rec_id     = body["rec_id"].toInt();
        int     student_id = body["student_id"].toInt();
        int     absent     = body["absent"].toInt();

        QSqlQuery check;
        check.prepare(
            "SELECT 1 FROM journal_rec jr"
            " JOIN classes c ON c.id = jr.class_id"
            " JOIN teachers t ON t.id = c.teacher_id"
            " JOIN employee e ON e.teacher_id = t.id"
            " WHERE jr.id = :rid AND e.token = :tok");
        check.bindValue(":rid", rec_id);
        check.bindValue(":tok", token);
        check.exec();
        if (!check.next()) {
            QJsonObject err; err["error"] = "Нет доступа";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
        }

        if (absent == 1) {
            QSqlQuery ins;
            ins.prepare(
                "INSERT OR IGNORE INTO journal_rec_data (rec_id, student_id) VALUES (:rid,:sid)");
            ins.bindValue(":rid", rec_id);
            ins.bindValue(":sid", student_id);
            ins.exec();
        } else {
            QSqlQuery del;
            del.prepare(
                "DELETE FROM journal_rec_data WHERE rec_id=:rid AND student_id=:sid");
            del.bindValue(":rid", rec_id);
            del.bindValue(":sid", student_id);
            del.exec();
        }
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
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
