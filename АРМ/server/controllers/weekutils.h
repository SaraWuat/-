#ifndef WEEKUTILS_H
#define WEEKUTILS_H

#include <QDate>

// Возвращает 1 (нечётная) или 2 (чётная)
inline int currentWeek() {
    QDate today = QDate::currentDate();
    int year = today.month() >= 9 ? today.year() : today.year() - 1;
    int weeksSince = QDate(year, 9, 1).daysTo(today) / 7;
    return (weeksSince % 2) + 1;
}

// Возвращает день недели: 1=Пн ... 7=Вс
inline int currentWeekDay() {
    return QDate::currentDate().dayOfWeek();
}

#endif // WEEKUTILS_H
