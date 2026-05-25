#ifndef CLASSESCONTROLLER_H
#define CLASSESCONTROLLER_H

#include <QHttpServer>

const int class_type_lab     = 0;
const int class_type_lec     = 1;
const int class_type_practic = 2;

class classesController
{
public:
    classesController(QHttpServer& server);
};

#endif // CLASSESCONTROLLER_H
