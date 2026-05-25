#include "config.h"
#include <QDebug>
#include <QSqlError>

QSqlDatabase config::db;

void config::dbCreate() {
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(DdbName);
    if (!db.open()) {
        qCritical() << "DB open error:" << db.lastError().text();
    } else {
        qDebug() << "DB opened:" << db.databaseName();
    }
}

config::config() {
    config::dbCreate();
}
