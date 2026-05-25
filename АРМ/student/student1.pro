QT += widgets
QT += core network

CONFIG += c++17

SOURCES += \
    components/requester/requester.cpp \
    config.cpp \
    login.cpp \
    main.cpp \
    mainwindow.cpp \
    calendarpage.cpp \
    attendancepage.cpp \
    gradespage.cpp \
    teacherwindow.cpp \
    adminwindow.cpp \
    headman/attendanceinputpage.cpp \
    headman/scheduleeditpage.cpp \
    teacher/teachercalendarpage.cpp \
    teacher/teacherjournalpage.cpp \
    teacher/teacherattendancepage.cpp \
    teacher/teachergradespage.cpp

HEADERS += \
    components/requester/requester.h \
    config.h \
    consts.h \
    login.h \
    mainwindow.h \
    calendarpage.h \
    attendancepage.h \
    gradespage.h \
    teacherwindow.h \
    adminwindow.h \
    headman/attendanceinputpage.h \
    headman/scheduleeditpage.h \
    teacher/teachercalendarpage.h \
    teacher/teacherjournalpage.h \
    teacher/teacherattendancepage.h \
    teacher/teachergradespage.h

FORMS += \
    login.ui \
    mainwindow.ui \
    calendarpage.ui \
    attendancepage.ui \
    gradespage.ui \
    teacherwindow.ui \
    adminwindow.ui \
    headman/attendanceinputpage.ui \
    headman/scheduleeditpage.ui \
    teacher/teachercalendarpage.ui \
    teacher/teacherjournalpage.ui \
    teacher/teacherattendancepage.ui \
    teacher/teachergradespage.ui

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    components/requester/requester.pri
