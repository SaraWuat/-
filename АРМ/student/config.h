#ifndef CONFIG_H
#define CONFIG_H

#include "components/requester/requester.h"
#include <QSslConfiguration>

class config : public QObject
{
private:
    QString host = QString("127.0.0.1");
    int port = 8081;
    QSslConfiguration *ssl = nullptr;
public:
    config();
    ~config();
};

#endif // CONFIG_H
