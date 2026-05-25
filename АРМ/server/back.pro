QT = core
QT += httpserver sql network

CONFIG += c++17 cmdline

SOURCES += \
    main.cpp \
    config/config.cpp \
    controllers/studentcontroller.cpp \
    controllers/classescontroller.cpp \
    controllers/journalcontroller.cpp \
    controllers/attendancecontroller.cpp \
    controllers/gradescontroller.cpp \
    controllers/eventscontroller.cpp \
    controllers/admincontroller.cpp \
    controllers/teachercontroller.cpp

HEADERS += \
    config/config.h \
    controllers/weekutils.h \
    controllers/studentcontroller.h \
    controllers/classescontroller.h \
    controllers/journalcontroller.h \
    controllers/attendancecontroller.h \
    controllers/gradescontroller.h \
    controllers/eventscontroller.h \
    controllers/admincontroller.h \
    controllers/teachercontroller.h

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += libmysql.dll
