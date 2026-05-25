#ifndef JOURNALCONTROLLER_H
#define JOURNALCONTROLLER_H

#include <QHttpServer>
#include <QObject>
#include <QDate>

// Границы текущего семестра — используется в attendancecontroller тоже
QPair<QDate, QDate> semesterBounds();

class journalController : public QObject
{
    Q_OBJECT
public:
    journalController(QHttpServer& server);

public slots:
    void addJournalRecsExec();
};

void addJournalRecs();
bool validateAccessToRec(const QString& token, int rec_id);

#endif // JOURNALCONTROLLER_H
