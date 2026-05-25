#ifndef CONFIG_H
#define CONFIG_H

#include <QString>
#include <QSqlDatabase>

class config
{
private:
    // Укажи путь к своей БД
    QString DdbName = "C:/Users/KitsuJi/Desktop/АРМ/server/database.sqlite";
    static QSqlDatabase db;
    void dbCreate();
public:
    config();
};

#endif // CONFIG_H
