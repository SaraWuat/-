#include "eventscontroller.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>
#include <QDebug>

static bool isHeadmanOfGroup(const QString& token, int groupId)
{
    QSqlQuery q;
    q.prepare(
        "SELECT 1 FROM students s"
        " JOIN learn_groups lg ON lg.headman_id = s.id"
        " WHERE s.token = :t AND s.group_id = :gid");
    q.bindValue(":t",   token);
    q.bindValue(":gid", groupId);
    q.exec();
    return q.next();
}

static bool eventOwnedByHeadman(const QString& token, int eventId)
{
    QSqlQuery q;
    q.prepare(
        "SELECT 1 FROM group_events ge"
        " JOIN students s ON s.group_id = ge.group_id"
        " JOIN learn_groups lg ON lg.headman_id = s.id"
        " WHERE s.token = :t AND ge.id = :eid");
    q.bindValue(":t",   token);
    q.bindValue(":eid", eventId);
    q.exec();
    return q.next();
}

static bool holidayOwnedByHeadman(const QString& token, int holidayId)
{
    QSqlQuery q;
    q.prepare(
        "SELECT 1 FROM group_holidays gh"
        " JOIN students s ON s.group_id = gh.group_id"
        " JOIN learn_groups lg ON lg.headman_id = s.id"
        " WHERE s.token = :t AND gh.id = :hid");
    q.bindValue(":t",   token);
    q.bindValue(":hid", holidayId);
    q.exec();
    return q.next();
}

eventsController::eventsController(QHttpServer& server)
{
    // ══ СОБЫТИЯ ══════════════════════════════════════════════════════════════

    // GET /events/<group_id>  → {data:[{id, date, time, title}]}
    server.route("/events/<arg>", QHttpServerRequest::Method::Get,
    [](int group_id) {
        QSqlQuery q;
        q.prepare(
            "SELECT id, date, time, title FROM group_events"
            " WHERE group_id = :gid ORDER BY date, time");
        q.bindValue(":gid", group_id);
        q.exec();
        QJsonArray arr;
        while (q.next()) {
            QJsonObject item;
            item["id"]    = q.value("id").toInt();
            item["date"]  = q.value("date").toString();
            item["time"]  = q.value("time").toString();
            item["title"] = q.value("title").toString();
            arr.append(item);
        }
        QJsonObject res; res["data"] = arr;
        return QHttpServerResponse(res, QHttpServerResponse::StatusCode::Ok);
    });

    // POST /events  body:{token, group_id, date, time, title}
    server.route("/events", QHttpServerRequest::Method::Post,
    [](const QHttpServerRequest& request) {
        const QJsonObject body = QJsonDocument::fromJson(request.body()).object();
        const QString token   = body["token"].toString();
        const int     groupId = body["group_id"].toInt();

        if (!isHeadmanOfGroup(token, groupId)) {
            QJsonObject err; err["error"] = "Нет доступа";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
        }

        const QString date  = body["date"].toString();
        const QString time  = body["time"].toString();
        const QString title = body["title"].toString().trimmed();

        if (title.isEmpty() || date.isEmpty()) {
            QJsonObject err; err["error"] = "Дата и название обязательны";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }

        QSqlQuery q;
        q.prepare(
            "INSERT INTO group_events (group_id, date, time, title)"
            " VALUES (:gid, :date, :time, :title)");
        q.bindValue(":gid",   groupId);
        q.bindValue(":date",  date);
        q.bindValue(":time",  time);
        q.bindValue(":title", title);
        if (!q.exec()) {
            qDebug() << "events insert:" << q.lastError().text();
            QJsonObject err; err["error"] = "Ошибка БД";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
        }
        QJsonObject ok; ok["id"] = q.lastInsertId().toInt();
        return QHttpServerResponse(ok, QHttpServerResponse::StatusCode::Ok);
    });

    // DELETE /events/<id>?token=
    server.route("/events/<arg>", QHttpServerRequest::Method::Delete,
    [](int event_id, const QHttpServerRequest& request) {
        const QString token = QUrlQuery(request.url().query()).queryItemValue("token");
        if (!eventOwnedByHeadman(token, event_id)) {
            QJsonObject err; err["error"] = "Нет доступа";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
        }
        QSqlQuery q;
        q.prepare("DELETE FROM group_events WHERE id = :id");
        q.bindValue(":id", event_id);
        q.exec();
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
    });

    // ══ ВЫХОДНЫЕ ДНИ ═════════════════════════════════════════════════════════

    // GET /holidays/<group_id>  → {data:[{id, date}]}
    server.route("/holidays/<arg>", QHttpServerRequest::Method::Get,
    [](int group_id) {
        QSqlQuery q;
        q.prepare(
            "SELECT id, date FROM group_holidays"
            " WHERE group_id = :gid ORDER BY date");
        q.bindValue(":gid", group_id);
        q.exec();
        QJsonArray arr;
        while (q.next()) {
            QJsonObject item;
            item["id"]   = q.value("id").toInt();
            item["date"] = q.value("date").toString();
            arr.append(item);
        }
        QJsonObject res; res["data"] = arr;
        return QHttpServerResponse(res, QHttpServerResponse::StatusCode::Ok);
    });

    // POST /holidays  body:{token, group_id, date}
    server.route("/holidays", QHttpServerRequest::Method::Post,
    [](const QHttpServerRequest& request) {
        const QJsonObject body = QJsonDocument::fromJson(request.body()).object();
        const QString token   = body["token"].toString();
        const int     groupId = body["group_id"].toInt();
        const QString date    = body["date"].toString();

        if (!isHeadmanOfGroup(token, groupId)) {
            QJsonObject err; err["error"] = "Нет доступа";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
        }
        if (date.isEmpty()) {
            QJsonObject err; err["error"] = "Дата обязательна";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }

        // Проверка дубликата
        QSqlQuery dupQ;
        dupQ.prepare(
            "SELECT id FROM group_holidays WHERE group_id = :gid AND date = :date");
        dupQ.bindValue(":gid",  groupId);
        dupQ.bindValue(":date", date);
        dupQ.exec();
        if (dupQ.next()) {
            QJsonObject err; err["error"] = "Эта дата уже добавлена";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Conflict);
        }

        QSqlQuery q;
        q.prepare(
            "INSERT INTO group_holidays (group_id, date) VALUES (:gid, :date)");
        q.bindValue(":gid",  groupId);
        q.bindValue(":date", date);
        if (!q.exec()) {
            qDebug() << "holidays insert:" << q.lastError().text();
            QJsonObject err; err["error"] = "Ошибка БД";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
        }
        QJsonObject ok; ok["id"] = q.lastInsertId().toInt();
        return QHttpServerResponse(ok, QHttpServerResponse::StatusCode::Ok);
    });

    // DELETE /holidays/<id>?token=
    server.route("/holidays/<arg>", QHttpServerRequest::Method::Delete,
    [](int holiday_id, const QHttpServerRequest& request) {
        const QString token = QUrlQuery(request.url().query()).queryItemValue("token");
        if (!holidayOwnedByHeadman(token, holiday_id)) {
            QJsonObject err; err["error"] = "Нет доступа";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
        }
        QSqlQuery q;
        q.prepare("DELETE FROM group_holidays WHERE id = :id");
        q.bindValue(":id", holiday_id);
        q.exec();
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
    });
}
