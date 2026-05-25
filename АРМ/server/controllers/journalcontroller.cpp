#include "journalcontroller.h"
#include "weekutils.h"

#include <QString>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QHttpServerResponse>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>
#include <QDebug>

// ─── Границы текущего семестра ────────────────────────────────────────────────
QPair<QDate, QDate> semesterBounds() {
    QDate today = QDate::currentDate();
    int m = today.month();
    if (m >= 9)
        return { QDate(today.year(), 9, 1),     QDate(today.year() + 1, 1, 31) };
    else if (m <= 1)
        return { QDate(today.year() - 1, 9, 1), QDate(today.year(), 1, 31) };
    else
        return { QDate(today.year(), 2, 1),      QDate(today.year(), 6, 30) };
}

journalController::journalController(QHttpServer& server) {

    // ── GET /journal/absent/<token> ───────────────────────────────────────────
    server.route("/journal/absent/<arg>", QHttpServerRequest::Method::Get,
    [](const QString& token) {
        int week    = currentWeek();
        int weekDay = currentWeekDay();

        QSqlQuery recQ;
        recQ.prepare(
            "SELECT r.id FROM journal_rec r"
            " JOIN classes c ON c.id = r.class_id"
            " WHERE c.week_day = :wd AND (c.week = :w OR c.week = 3)"
            " AND c.group_id = (SELECT group_id FROM students WHERE token = :token)"
            " AND r.headman_accept = 0");
        recQ.bindValue(":wd",    weekDay);
        recQ.bindValue(":w",     week);
        recQ.bindValue(":token", token);
        recQ.exec();

        QSqlQuery stuQ;
        stuQ.prepare("SELECT id FROM students WHERE token = :token");
        stuQ.bindValue(":token", token);
        stuQ.exec();
        if (!stuQ.next()) {
            QJsonObject err; err["error"] = "Пользователь не найден";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
        }
        int studentId = stuQ.value("id").toInt();

        while (recQ.next()) {
            int recId = recQ.value("id").toInt();
            QSqlQuery existQ;
            existQ.prepare(
                "SELECT 1 FROM journal_rec_data WHERE rec_id = :rid AND student_id = :sid");
            existQ.bindValue(":rid", recId);
            existQ.bindValue(":sid", studentId);
            existQ.exec();
            if (existQ.next()) continue;

            QSqlQuery insQ;
            insQ.prepare(
                "INSERT INTO journal_rec_data (rec_id, student_id) VALUES (:rid, :sid)");
            insQ.bindValue(":rid", recId);
            insQ.bindValue(":sid", studentId);
            insQ.exec();
        }
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
    });

    // ── POST /journal/fill ────────────────────────────────────────────────────
    // {token, class_id, theme, data:[{id, absent}]}
    server.route("/journal/fill", QHttpServerRequest::Method::Post,
    [](const QHttpServerRequest& request) {
        const QJsonObject body = QJsonDocument::fromJson(request.body()).object();

        QString    token   = body["token"].toString();
        int        classId = body["class_id"].toInt();
        QString    theme   = body["theme"].toString();
        QJsonArray data    = body["data"].toArray();

        QSqlQuery recQ;
        recQ.prepare(
            "SELECT id FROM journal_rec WHERE class_id = :cid AND headman_accept = 0");
        recQ.bindValue(":cid", classId);
        recQ.exec();
        if (!recQ.next()) {
            QJsonObject err; err["error"] = "Запись журнала не найдена";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
        }
        int recId = recQ.value("id").toInt();

        if (!validateAccessToRec(token, recId)) {
            QJsonObject err; err["error"] = "Нет доступа";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
        }

        for (int i = 0; i < data.size(); i++) {
            QJsonObject item  = data[i].toObject();
            int studentId = item["id"].toInt();
            int absent    = item["absent"].toInt();

            QSqlQuery existQ;
            existQ.prepare(
                "SELECT 1 FROM journal_rec_data WHERE rec_id = :rid AND student_id = :sid");
            existQ.bindValue(":rid", recId);
            existQ.bindValue(":sid", studentId);
            existQ.exec();
            bool exists = existQ.next();

            if (absent == 1 && !exists) {
                QSqlQuery insQ;
                insQ.prepare(
                    "INSERT INTO journal_rec_data (rec_id, student_id) VALUES (:rid, :sid)");
                insQ.bindValue(":rid", recId);
                insQ.bindValue(":sid", studentId);
                insQ.exec();
            } else if (absent == 0 && exists) {
                QSqlQuery delQ;
                delQ.prepare(
                    "DELETE FROM journal_rec_data WHERE rec_id = :rid AND student_id = :sid");
                delQ.bindValue(":rid", recId);
                delQ.bindValue(":sid", studentId);
                delQ.exec();
            }
        }

        QSqlQuery updQ;
        updQ.prepare(
            "UPDATE journal_rec SET headman_accept = 1, date = :date, theme = :theme"
            " WHERE id = :rid");
        updQ.bindValue(":date",  QDateTime::currentDateTime().toSecsSinceEpoch());
        updQ.bindValue(":theme", theme);
        updQ.bindValue(":rid",   recId);
        if (!updQ.exec()) {
            qDebug() << "journal_rec update error:" << updQ.lastError().text();
            QJsonObject err; err["error"] = "Ошибка БД";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
        }
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
    });


    // ── POST /journal/edit ────────────────────────────────────────────────────
    // Редактирование уже закрытой записи (headman_accept=1). Только для старосты.
    // {token, class_id, theme, data:[{id, absent}]}
    server.route("/journal/edit", QHttpServerRequest::Method::Post,
    [](const QHttpServerRequest& request) {
        const QJsonObject body = QJsonDocument::fromJson(request.body()).object();
        QString    token   = body["token"].toString();
        int        classId = body["class_id"].toInt();
        QString    theme   = body["theme"].toString();
        QJsonArray data    = body["data"].toArray();

        QSqlQuery authQ;
        authQ.prepare(
            "SELECT 1 FROM classes c"
            " JOIN learn_groups lg ON lg.id=c.group_id"
            " JOIN students s ON s.id=lg.headman_id"
            " WHERE c.id=:cid AND s.token=:token");
        authQ.bindValue(":cid",   classId);
        authQ.bindValue(":token", token);
        authQ.exec();
        if (!authQ.next()) {
            QJsonObject err; err["error"] = "Нет доступа";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
        }

        // Берём последнюю запись для этой пары (любой статус)
        QSqlQuery recQ;
        recQ.prepare(
            "SELECT id FROM journal_rec WHERE class_id=:cid ORDER BY id DESC LIMIT 1");
        recQ.bindValue(":cid", classId);
        recQ.exec();

        int recId = 0;
        if (recQ.next()) {
            recId = recQ.value("id").toInt();
        } else {
            QSqlQuery insRec;
            insRec.prepare(
                "INSERT INTO journal_rec (class_id, headman_accept, teacher_accept, dean_accept)"
                " VALUES (:cid, 0, 0, 0)");
            insRec.bindValue(":cid", classId);
            insRec.exec();
            recId = insRec.lastInsertId().toInt();
        }

        // Полностью перезаписываем отметки
        QSqlQuery delAll;
        delAll.prepare("DELETE FROM journal_rec_data WHERE rec_id=:rid");
        delAll.bindValue(":rid", recId);
        delAll.exec();

        for (int i = 0; i < data.size(); i++) {
            QJsonObject item = data[i].toObject();
            if (item["absent"].toInt() == 1) {
                QSqlQuery insQ;
                insQ.prepare(
                    "INSERT INTO journal_rec_data (rec_id, student_id) VALUES (:rid,:sid)");
                insQ.bindValue(":rid", recId);
                insQ.bindValue(":sid", item["id"].toInt());
                insQ.exec();
            }
        }

        QSqlQuery updQ;
        updQ.prepare(
            "UPDATE journal_rec SET headman_accept=1, theme=:theme WHERE id=:rid");
        updQ.bindValue(":theme", theme);
        updQ.bindValue(":rid",   recId);
        updQ.exec();

        return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
    });

    // ── GET /journal/group/<group_id>?token=<teacher_token> ──────────────────
    // Возвращает полный журнал группы за семестр:
    // {students:[{id,name}], recs:[{rec_id,date,subject,class_id}],
    //  data:[{rec_id,student_id,absent,grade}]}
    server.route("/journal/group/<arg>", QHttpServerRequest::Method::Get,
    [](int group_id, const QHttpServerRequest& request) {
        QString token = QUrlQuery(request.url().query()).queryItemValue("token");

        // Проверка доступа — преподаватель ведёт группу
        QSqlQuery check;
        check.prepare(
            "SELECT 1 FROM teacher_groups tg"
            " JOIN teachers t ON t.id = tg.teacher_id"
            " WHERE t.token = :token AND tg.group_id = :gid");
        check.bindValue(":token", token);
        check.bindValue(":gid",   group_id);
        check.exec();
        if (!check.next()) {
            QJsonObject err; err["error"] = "Нет доступа";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
        }

        auto [semStart, semEnd] = semesterBounds();
        qint64 tsStart = QDateTime(semStart, QTime(0, 0)).toSecsSinceEpoch();
        qint64 tsEnd   = QDateTime(semEnd,   QTime(23, 59)).toSecsSinceEpoch();

        // Студенты группы
        QJsonArray students;
        QSqlQuery stuQ;
        stuQ.prepare("SELECT id, name FROM students WHERE group_id = :gid ORDER BY name");
        stuQ.bindValue(":gid", group_id);
        stuQ.exec();
        while (stuQ.next()) {
            QJsonObject s;
            s["id"]   = stuQ.value("id").toInt();
            s["name"] = stuQ.value("name").toString();
            students.append(s);
        }

        // Записи журнала за семестр
        QJsonArray recs;
        QSqlQuery recQ;
        recQ.prepare(
            "SELECT jr.id AS rec_id, jr.date, jr.theme, jr.class_id,"
            "  (SELECT name FROM subjects WHERE id = c.subject_id) AS subject"
            " FROM journal_rec jr"
            " JOIN classes c ON c.id = jr.class_id"
            " WHERE c.group_id = :gid AND jr.headman_accept = 1"
            " AND jr.date BETWEEN :ts1 AND :ts2"
            " ORDER BY jr.date");
        recQ.bindValue(":gid", group_id);
        recQ.bindValue(":ts1", tsStart);
        recQ.bindValue(":ts2", tsEnd);
        recQ.exec();
        while (recQ.next()) {
            QJsonObject rec;
            rec["rec_id"]   = recQ.value("rec_id").toInt();
            rec["class_id"] = recQ.value("class_id").toInt();
            rec["date"]     = QDateTime::fromSecsSinceEpoch(recQ.value("date").toLongLong())
                                  .toString("dd.MM");
            rec["subject"]  = recQ.value("subject").toString();
            rec["theme"]    = recQ.value("theme").toString();
            recs.append(rec);
        }

        // Отсутствия + оценки
        QJsonArray data;
        QSqlQuery dataQ;
        dataQ.prepare(
            "SELECT jrd.rec_id, jrd.student_id,"
            "  1 AS absent,"
            "  COALESCE(g.grade, 0) AS grade"
            " FROM journal_rec_data jrd"
            " JOIN journal_rec jr ON jr.id = jrd.rec_id"
            " JOIN classes c ON c.id = jr.class_id"
            " LEFT JOIN grades g ON g.rec_id = jrd.rec_id AND g.student_id = jrd.student_id"
            " WHERE c.group_id = :gid AND jr.date BETWEEN :ts1 AND :ts2");
        dataQ.bindValue(":gid", group_id);
        dataQ.bindValue(":ts1", tsStart);
        dataQ.bindValue(":ts2", tsEnd);
        dataQ.exec();
        while (dataQ.next()) {
            QJsonObject item;
            item["rec_id"]     = dataQ.value("rec_id").toInt();
            item["student_id"] = dataQ.value("student_id").toInt();
            item["absent"]     = dataQ.value("absent").toInt();
            item["grade"]      = dataQ.value("grade").toInt();
            data.append(item);
        }

        QJsonObject res;
        res["students"] = students;
        res["recs"]     = recs;
        res["data"]     = data;
        return QHttpServerResponse(res, QHttpServerResponse::StatusCode::Ok);
    });
}

// ─── Создаёт journal_rec для пар текущего дня ────────────────────────────────
void addJournalRecs() {
    int week    = currentWeek();
    int weekDay = currentWeekDay();

    QSqlQuery classQ;
    classQ.prepare(
        "SELECT id FROM classes"
        " WHERE week_day = :wd AND (week = :w OR week = 3)"
        " AND id NOT IN (SELECT class_id FROM journal_rec WHERE headman_accept = 0)");
    classQ.bindValue(":wd", weekDay);
    classQ.bindValue(":w",  week);
    classQ.exec();

    while (classQ.next()) {
        QSqlQuery insQ;
        insQ.prepare(
            "INSERT INTO journal_rec (class_id, headman_accept, teacher_accept, dean_accept)"
            " VALUES (:cid, 0, 0, 0)");
        insQ.bindValue(":cid", classQ.value("id").toInt());
        insQ.exec();
    }
}

void journalController::addJournalRecsExec() {
    addJournalRecs();
}

bool validateAccessToRec(const QString& token, int rec_id) {
    QSqlQuery q;
    q.prepare(
        "SELECT 1 FROM journal_rec jr"
        " JOIN classes c ON c.id = jr.class_id"
        " JOIN learn_groups lg ON lg.id = c.group_id"
        " JOIN students s ON s.id = lg.headman_id"
        " WHERE jr.id = :rid AND s.token = :token");
    q.bindValue(":rid",   rec_id);
    q.bindValue(":token", token);
    q.exec();
    return q.next();
}
