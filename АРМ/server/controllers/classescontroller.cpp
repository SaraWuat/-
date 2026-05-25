#include "classescontroller.h"
#include "weekutils.h"

#include <QString>
#include <QSqlQuery>
#include <QHttpServerResponse>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonDocument>
#include <QUrlQuery>
#include <QDate>
#include <QDebug>

static QJsonObject lessonFromQuery(QSqlQuery& q) {
    QJsonObject lesson;
    lesson["id"]         = QJsonValue::fromVariant(q.value("id"));
    lesson["subject_id"] = QJsonValue::fromVariant(q.value("subject_id"));
    lesson["subject"]    = QJsonValue::fromVariant(q.value("subject"));
    lesson["time"]       = QJsonValue::fromVariant(q.value("time_str"));
    lesson["teacher_id"] = QJsonValue::fromVariant(q.value("teacher_id"));
    lesson["teacher"]    = QJsonValue::fromVariant(q.value("teacher"));
    lesson["group"]      = QJsonValue::fromVariant(q.value("group_str"));
    lesson["place"]      = QJsonValue::fromVariant(q.value("place"));
    lesson["week_day"]   = QJsonValue::fromVariant(q.value("week_day"));
    lesson["week"]       = QJsonValue::fromVariant(q.value("week"));
    int type = q.value("type").toInt();
    switch (type) {
    case class_type_lab:     lesson["type"] = QString("лаб"); break;
    case class_type_lec:     lesson["type"] = QString("лек"); break;
    case class_type_practic: lesson["type"] = QString("пр");  break;
    }
    return lesson;
}

static const QString SELECT_CLASSES = QStringLiteral(
    "SELECT c.*,"
    " (SELECT name FROM subjects     WHERE id = c.subject_id) AS subject,"
    " (SELECT time FROM times        WHERE id = c.time_id)    AS time_str,"
    " (SELECT name FROM teachers     WHERE id = c.teacher_id) AS teacher,"
    " (SELECT name FROM learn_groups WHERE id = c.group_id)   AS group_str"
    " FROM classes AS c"
);

classesController::classesController(QHttpServer& server) {

    // ── Справочники (регистрируем первыми — без <arg>, не конфликтуют) ────────

    // GET /subjects
    server.route("/subjects", QHttpServerRequest::Method::Get, []() {
        QSqlQuery q("SELECT id, name FROM subjects ORDER BY name");
        QJsonArray res;
        while (q.next()) {
            QJsonObject item;
            item["id"]   = q.value("id").toInt();
            item["name"] = q.value("name").toString();
            res.append(item);
        }
        QJsonObject obj; obj["data"] = res;
        return QHttpServerResponse(obj, QHttpServerResponse::StatusCode::Ok);
    });

    // GET /teachers
    server.route("/teachers", QHttpServerRequest::Method::Get, []() {
        QSqlQuery q("SELECT id, name FROM teachers ORDER BY name");
        QJsonArray res;
        while (q.next()) {
            QJsonObject item;
            item["id"]   = q.value("id").toInt();
            item["name"] = q.value("name").toString();
            res.append(item);
        }
        QJsonObject obj; obj["data"] = res;
        return QHttpServerResponse(obj, QHttpServerResponse::StatusCode::Ok);
    });

    // GET /times
    server.route("/times", QHttpServerRequest::Method::Get, []() {
        QSqlQuery q("SELECT id, time FROM times ORDER BY id");
        QJsonArray res;
        while (q.next()) {
            QJsonObject item;
            item["id"]   = q.value("id").toInt();
            item["time"] = q.value("time").toString();
            res.append(item);
        }
        QJsonObject obj; obj["data"] = res;
        return QHttpServerResponse(obj, QHttpServerResponse::StatusCode::Ok);
    });


    // GET /subjects/group/<group_id>?token=
    // Уникальные предметы которые ведутся в данной группе (для фильтра посещаемости)
    server.route("/subjects/group/<arg>", QHttpServerRequest::Method::Get,
    [](int group_id, const QHttpServerRequest& request) {
        QString token = QUrlQuery(request.url().query()).queryItemValue("token");
        // Проверяем что это employee (куратор/препод) или пропускаем проверку
        QSqlQuery q;
        q.prepare(
            "SELECT DISTINCT s.id, s.name"
            " FROM subjects s"
            " JOIN classes c ON c.subject_id = s.id"
            " WHERE c.group_id = :gid"
            " ORDER BY s.name");
        q.bindValue(":gid", group_id);
        q.exec();
        QJsonArray res;
        while (q.next()) {
            QJsonObject item;
            item["id"]   = q.value("id").toInt();
            item["name"] = q.value("name").toString();
            res.append(item);
        }
        QJsonObject obj; obj["data"] = res;
        return QHttpServerResponse(obj, QHttpServerResponse::StatusCode::Ok);
    });

    // ── Роуты с фиксированными сегментами — регистрируем до <arg> роутов ─────

    // GET /classes/today/token/<token>
    server.route("/classes/today/token/<arg>", QHttpServerRequest::Method::Get,
    [](const QString& token) {
        QSqlQuery q;
        q.prepare(SELECT_CLASSES +
            " WHERE c.week_day = :wd AND (c.week = :w OR c.week = 3)"
            " AND c.group_id = (SELECT group_id FROM students WHERE token = :token)"
            " ORDER BY c.time_id");
        q.bindValue(":wd", currentWeekDay());
        q.bindValue(":w",  currentWeek());
        q.bindValue(":token", token);
        q.exec();
        QJsonArray res;
        while (q.next()) res.append(lessonFromQuery(q));
        QJsonObject obj; obj["data"] = res;
        return QHttpServerResponse(obj, QHttpServerResponse::StatusCode::Ok);
    });

    // GET /classes/today/teacher/<token>
    server.route("/classes/today/teacher/<arg>", QHttpServerRequest::Method::Get,
    [](const QString& token) {
        QSqlQuery q;
        q.prepare(SELECT_CLASSES +
            " WHERE c.week_day = :wd AND (c.week = :w OR c.week = 3)"
            " AND c.teacher_id = (SELECT id FROM teachers WHERE token = :token)"
            " ORDER BY c.time_id");
        q.bindValue(":wd", currentWeekDay());
        q.bindValue(":w",  currentWeek());
        q.bindValue(":token", token);
        q.exec();
        QJsonArray res;
        while (q.next()) res.append(lessonFromQuery(q));
        QJsonObject obj; obj["data"] = res;
        return QHttpServerResponse(obj, QHttpServerResponse::StatusCode::Ok);
    });


    // GET /classes/date/teacher/<token>/<date>
    // Расписание конкретного препода на выбранную дату (для TeacherCalendarPage)
    server.route("/classes/date/teacher/<arg>/<arg>", QHttpServerRequest::Method::Get,
    [](const QString& token, const QString& dateStr) {
        QDate date = QDate::fromString(dateStr, "yyyy-MM-dd");
        if (!date.isValid()) {
            QJsonObject err; err["error"] = "Неверный формат даты";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }
        int weekDay = date.dayOfWeek();
        int year    = date.month() >= 9 ? date.year() : date.year() - 1;
        int week    = (QDate(year, 9, 1).daysTo(date) / 7 % 2) + 1;
        QSqlQuery q;
        q.prepare(SELECT_CLASSES +
            " WHERE c.week_day = :wd AND (c.week = :w OR c.week = 3)"
            " AND c.teacher_id = ("
            "   SELECT t.id FROM teachers t"
            "   JOIN employee e ON e.teacher_id = t.id"
            "   WHERE e.token = :token)"
            " ORDER BY c.time_id");
        q.bindValue(":wd",    weekDay);
        q.bindValue(":w",     week);
        q.bindValue(":token", token);
        q.exec();
        QJsonArray res;
        while (q.next()) res.append(lessonFromQuery(q));
        QJsonObject obj; obj["data"] = res;
        return QHttpServerResponse(obj, QHttpServerResponse::StatusCode::Ok);
    });

    // GET /classes/date/<token>/<date>
    server.route("/classes/date/<arg>/<arg>", QHttpServerRequest::Method::Get,
    [](const QString& token, const QString& dateStr) {
        QDate date = QDate::fromString(dateStr, "yyyy-MM-dd");
        if (!date.isValid()) {
            QJsonObject err; err["error"] = "Неверный формат даты";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }
        int weekDay = date.dayOfWeek();
        int year    = date.month() >= 9 ? date.year() : date.year() - 1;
        int week    = (QDate(year, 9, 1).daysTo(date) / 7 % 2) + 1;
        QSqlQuery q;
        q.prepare(SELECT_CLASSES +
            " WHERE c.week_day = :wd AND (c.week = :w OR c.week = 3)"
            " AND c.group_id = (SELECT group_id FROM students WHERE token = :token)"
            " ORDER BY c.time_id");
        q.bindValue(":wd",    weekDay);
        q.bindValue(":w",     week);
        q.bindValue(":token", token);
        q.exec();
        QJsonArray res;
        while (q.next()) res.append(lessonFromQuery(q));
        QJsonObject obj; obj["data"] = res;
        return QHttpServerResponse(obj, QHttpServerResponse::StatusCode::Ok);
    });

    // GET /groups/teacher/<token>
    server.route("/groups/teacher/<arg>", QHttpServerRequest::Method::Get,
    [](const QString& token) {
        QSqlQuery q;
        q.prepare(
            "SELECT DISTINCT lg.id, lg.name"
            " FROM learn_groups lg"
            " JOIN teacher_groups tg ON tg.group_id = lg.id"
            " JOIN teachers t ON t.id = tg.teacher_id"
            " WHERE t.token = :token ORDER BY lg.name");
        q.bindValue(":token", token);
        q.exec();
        QJsonArray res;
        while (q.next()) {
            QJsonObject g;
            g["id"]   = q.value("id").toInt();
            g["name"] = q.value("name").toString();
            res.append(g);
        }
        QJsonObject obj; obj["data"] = res;
        return QHttpServerResponse(obj, QHttpServerResponse::StatusCode::Ok);
    });

    // GET /schedule/teacher/<token>  — ВАЖНО: до /schedule/<arg>
    server.route("/schedule/teacher/<arg>", QHttpServerRequest::Method::Get,
    [](const QString& token) {
        QSqlQuery q;
        q.prepare(SELECT_CLASSES +
            " WHERE c.teacher_id = (SELECT id FROM teachers WHERE token = :token)"
            " ORDER BY c.week_day, c.time_id");
        q.bindValue(":token", token);
        q.exec();
        QJsonArray res;
        while (q.next()) res.append(lessonFromQuery(q));
        QJsonObject obj; obj["data"] = res;
        return QHttpServerResponse(obj, QHttpServerResponse::StatusCode::Ok);
    });

    // POST /schedule  — добавить пару (только для старосты)
    server.route("/schedule", QHttpServerRequest::Method::Post,
    [](const QHttpServerRequest& request) {
        const QJsonObject body = QJsonDocument::fromJson(request.body()).object();
        QString token = body["token"].toString();
        int group_id  = body["group_id"].toInt();

        QSqlQuery checkQ;
        checkQ.prepare(
            "SELECT 1 FROM learn_groups lg JOIN students s ON s.id = lg.headman_id"
            " WHERE s.token = :token AND lg.id = :gid");
        checkQ.bindValue(":token", token);
        checkQ.bindValue(":gid",   group_id);
        checkQ.exec();
        if (!checkQ.next()) {
            QJsonObject err; err["error"] = "Нет доступа";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
        }

        QSqlQuery ins;
        ins.prepare(
            "INSERT INTO classes (group_id, subject_id, teacher_id, time_id, week_day, week, place, type)"
            " VALUES (:gid, :sid, :tid, :timeid, :wd, :w, :place, :type)");
        ins.bindValue(":gid",    group_id);
        ins.bindValue(":sid",    body["subject_id"].toInt());
        ins.bindValue(":tid",    body["teacher_id"].toInt());
        ins.bindValue(":timeid", body["time_id"].toInt());
        ins.bindValue(":wd",     body["week_day"].toInt());
        ins.bindValue(":w",      body["week"].toInt());
        ins.bindValue(":place",  body["place"].toString());
        ins.bindValue(":type",   body["type"].toInt()); // 0=лаб, 1=лек, 2=пр
        if (!ins.exec()) {
            QJsonObject err; err["error"] = "Ошибка БД";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
        }
        QJsonObject ok; ok["id"] = ins.lastInsertId().toInt();
        return QHttpServerResponse(ok, QHttpServerResponse::StatusCode::Ok);
    });

    // GET /schedule/<group_id>  — ПОСЛЕ /schedule/teacher/<arg>
    server.route("/schedule/<arg>", QHttpServerRequest::Method::Get,
    [](int group_id) {
        QSqlQuery q;
        q.prepare(SELECT_CLASSES +
            " WHERE c.group_id = :gid ORDER BY c.week_day, c.time_id");
        q.bindValue(":gid", group_id);
        q.exec();
        QJsonArray res;
        while (q.next()) res.append(lessonFromQuery(q));
        QJsonObject obj; obj["data"] = res;
        return QHttpServerResponse(obj, QHttpServerResponse::StatusCode::Ok);
    });

    // DELETE /schedule/<class_id>?token=
    server.route("/schedule/<arg>", QHttpServerRequest::Method::Delete,
    [](int class_id, const QHttpServerRequest& request) {
        QString token = QUrlQuery(request.url().query()).queryItemValue("token");
        QSqlQuery checkQ;
        checkQ.prepare(
            "SELECT 1 FROM classes c"
            " JOIN learn_groups lg ON lg.id = c.group_id"
            " JOIN students s ON s.id = lg.headman_id"
            " WHERE c.id = :cid AND s.token = :token");
        checkQ.bindValue(":cid",   class_id);
        checkQ.bindValue(":token", token);
        checkQ.exec();
        if (!checkQ.next()) {
            QJsonObject err; err["error"] = "Нет доступа";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
        }
        QSqlQuery del;
        del.prepare("DELETE FROM classes WHERE id = :cid");
        del.bindValue(":cid", class_id);
        del.exec();
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
    });
}
