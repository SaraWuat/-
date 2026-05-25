#include "attendancecontroller.h"
#include "journalcontroller.h"

#include <QSqlQuery>
#include <QHttpServerResponse>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QDateTime>
#include <QDebug>

attendanceController::attendanceController(QHttpServer& server) {

    // GET /attendance/<token>
    // ИСПРАВЛЕНИЕ: total = все закрытые journal_rec группы,
    // attended = total - пропущенные (journal_rec_data)
    server.route("/attendance/<arg>", QHttpServerRequest::Method::Get,
    [](const QString& token) {
        auto [semStart, semEnd] = semesterBounds();
        qint64 tsStart = QDateTime(semStart, QTime(0,0)).toSecsSinceEpoch();
        qint64 tsEnd   = QDateTime(semEnd,   QTime(23,59)).toSecsSinceEpoch();

        // Получаем student_id и group_id
        QSqlQuery stuQ;
        stuQ.prepare("SELECT id, group_id FROM students WHERE token=:t");
        stuQ.bindValue(":t", token);
        stuQ.exec();
        if (!stuQ.next()) {
            QJsonObject err; err["error"] = "Токен не найден";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
        }
        int studentId = stuQ.value("id").toInt();
        int groupId   = stuQ.value("group_id").toInt();

        // Все закрытые записи журнала группы за семестр
        QSqlQuery q;
        q.prepare(
            "SELECT"
            "  jr.date AS date,"
            "  jr.theme AS theme,"
            "  (SELECT name FROM subjects WHERE id=c.subject_id) AS subject,"
            "  CASE WHEN jrd.student_id IS NOT NULL THEN 0 ELSE 1 END AS attended"
            " FROM journal_rec jr"
            " JOIN classes c ON c.id=jr.class_id"
            " LEFT JOIN journal_rec_data jrd"
            "   ON jrd.rec_id=jr.id AND jrd.student_id=:sid"
            " WHERE c.group_id=:gid"
            " AND jr.headman_accept=1"
            " AND jr.date BETWEEN :ts1 AND :ts2"
            " ORDER BY jr.date"
        );
        q.bindValue(":sid", studentId);
        q.bindValue(":gid", groupId);
        q.bindValue(":ts1", tsStart);
        q.bindValue(":ts2", tsEnd);
        q.exec();

        QJsonArray data;
        int total = 0, attended = 0;
        while (q.next()) {
            QJsonObject item;
            // yyyy-MM-dd для корректной группировки по неделям на клиенте
            item["date"]     = QDateTime::fromSecsSinceEpoch(q.value("date").toLongLong())
                                   .toString("yyyy-MM-dd");
            item["subject"]  = q.value("subject").toString();
            item["theme"]    = q.value("theme").toString();
            int att = q.value("attended").toInt();
            item["attended"] = att;
            data.append(item);
            total++;
            attended += att;
        }

        QJsonObject res;
        res["data"]     = data;
        res["total"]    = total;
        res["attended"] = attended;
        res["percent"]  = total > 0 ? qRound(attended * 100.0 / total) : 0;
        return QHttpServerResponse(res, QHttpServerResponse::StatusCode::Ok);
    });

    // GET /attendance/group/<group_id>?token=
    server.route("/attendance/group/<arg>", QHttpServerRequest::Method::Get,
    [](int group_id, const QHttpServerRequest& request) {
        QString token = QUrlQuery(request.url().query()).queryItemValue("token");

        QSqlQuery check;
        check.prepare(
            "SELECT 1 FROM teacher_groups tg"
            " JOIN teachers t ON t.id=tg.teacher_id"
            " JOIN employee e ON e.teacher_id=t.id"
            " WHERE e.token=:token AND tg.group_id=:gid");
        check.bindValue(":token", token);
        check.bindValue(":gid",   group_id);
        check.exec();
        if (!check.next()) {
            QJsonObject err; err["error"] = "Нет доступа";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
        }

        auto [semStart, semEnd] = semesterBounds();
        qint64 tsStart = QDateTime(semStart, QTime(0,0)).toSecsSinceEpoch();
        qint64 tsEnd   = QDateTime(semEnd,   QTime(23,59)).toSecsSinceEpoch();

        // Подсчитываем total как кол-во закрытых записей журнала группы
        QSqlQuery totalQ;
        totalQ.prepare(
            "SELECT COUNT(*) FROM journal_rec jr"
            " JOIN classes c ON c.id=jr.class_id"
            " WHERE c.group_id=:gid AND jr.headman_accept=1"
            " AND jr.date BETWEEN :ts1 AND :ts2");
        totalQ.bindValue(":gid", group_id);
        totalQ.bindValue(":ts1", tsStart);
        totalQ.bindValue(":ts2", tsEnd);
        totalQ.exec();
        int totalClasses = totalQ.next() ? totalQ.value(0).toInt() : 0;

        QSqlQuery q;
        q.prepare(
            "SELECT s.id AS student_id, s.name,"
            " COUNT(jrd.student_id) AS missed"
            " FROM students s"
            " LEFT JOIN journal_rec_data jrd ON jrd.student_id=s.id"
            " LEFT JOIN journal_rec jr ON jr.id=jrd.rec_id"
            " LEFT JOIN classes c ON c.id=jr.class_id"
            " AND c.group_id=:gid"
            " AND jr.headman_accept=1"
            " AND jr.date BETWEEN :ts1 AND :ts2"
            " WHERE s.group_id=:gid2"
            " GROUP BY s.id, s.name"
            " ORDER BY s.name");
        q.bindValue(":gid",  group_id);
        q.bindValue(":ts1",  tsStart);
        q.bindValue(":ts2",  tsEnd);
        q.bindValue(":gid2", group_id);
        q.exec();

        QJsonArray data;
        while (q.next()) {
            int missed   = q.value("missed").toInt();
            int attended = totalClasses - missed;
            QJsonObject item;
            item["student_id"] = q.value("student_id").toInt();
            item["name"]       = q.value("name").toString();
            item["total"]      = totalClasses;
            item["attended"]   = attended;
            item["percent"]    = totalClasses > 0
                                 ? qRound(attended * 100.0 / totalClasses) : 0;
            data.append(item);
        }

        QJsonObject res; res["data"] = data;
        return QHttpServerResponse(res, QHttpServerResponse::StatusCode::Ok);
    });
}
