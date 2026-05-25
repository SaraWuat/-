#include "gradespage.h"
#include "ui_gradespage.h"
#include "components/requester/requester.h"

#include <QSettings>
#include <QJsonArray>
#include <QJsonObject>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QDate>
#include <QMap>
#include <QDebug>

// Определяем семестр по дате: "Осенний YYYY" или "Весенний YYYY"
static QString semesterLabel(const QString& dateStr) {
    QDate d = QDate::fromString(dateStr, "dd.MM.yyyy");
    if (!d.isValid()) return "Неизвестный семестр";
    int m = d.month();
    if (m >= 9 || m <= 1)
        return QString("Осенний %1/%2").arg(m >= 9 ? d.year() : d.year()-1).arg(m >= 9 ? d.year()+1 : d.year());
    else
        return QString("Весенний %1").arg(d.year());
}

static QString markStr(int val) {
    return val == 1 ? "+" : QString::number(val);
}

GradesPage::GradesPage(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::GradesPage)
{
    ui->setupUi(this);
    setWindowTitle("Мои оценки");
    connect(ui->backButton, &QPushButton::clicked, this, [this](){ close(); });
    loadGrades();
}

GradesPage::~GradesPage() { delete ui; }

void GradesPage::loadGrades()
{
    QSettings s("NagaevM", "studentApp");
    QString token = s.value("auth_token").toString();

    ui->gradesTree->clear();
    ui->gradesTree->setColumnCount(3);
    ui->gradesTree->setHeaderLabels({"Предмет / Занятие", "Дата", "Оценка"});
    ui->gradesTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    // Сначала получаем student_id
    auto* r0 = new Requester(this);
    r0->initRequester("127.0.0.1", 8081, nullptr);
    r0->sendRequest(
        QString("student/info/%1").arg(token),
        [this, r0, token](const QJsonObject &info) {
            int sid = info["id"].toInt();
            r0->deleteLater();

            // Загружаем журнальные оценки и teacher_marks параллельно
            struct Data { QJsonArray journal; QJsonArray marks; };
            Data* acc = new Data();
            int* pending = new int(2);

            auto tryBuild = [this, acc, pending]() {
                (*pending)--;
                if (*pending > 0) return;

                // ── Структура: семестр → предмет → записи ──
                // Объединяем оба источника
                struct Entry {
                    QString date;      // dd.MM.yyyy или yyyy-MM-dd
                    QString subject;
                    QString detail;    // тема или комментарий
                    int     value;     // grade или mark
                    bool    isMark;    // false=журнал, true=teacher_mark
                };
                QMap<QString, QMap<QString, QList<Entry>>> bySemesterSubject;

                for (const QJsonValue &v : acc->journal) {
                    QJsonObject o = v.toObject();
                    Entry e;
                    e.date    = o["date"].toString();
                    e.subject = o["subject"].toString();
                    e.detail  = o["theme"].toString();
                    e.value   = o["grade"].toInt();
                    e.isMark  = false;
                    bySemesterSubject[semesterLabel(e.date)][e.subject].append(e);
                }
                for (const QJsonValue &v : acc->marks) {
                    QJsonObject o = v.toObject();
                    Entry e;
                    // mark_date приходит как yyyy-MM-dd — конвертируем
                    QDate d = QDate::fromString(o["date"].toString(), "yyyy-MM-dd");
                    e.date    = d.isValid() ? d.toString("dd.MM.yyyy") : o["date"].toString();
                    e.subject = o["subject"].toString();
                    e.detail  = o["comment"].toString().isEmpty()
                                ? o["teacher_name"].toString()
                                : o["teacher_name"].toString() + ": " + o["comment"].toString();
                    e.value   = o["mark"].toInt();
                    e.isMark  = true;
                    bySemesterSubject[semesterLabel(e.date)][e.subject].append(e);
                }

                if (bySemesterSubject.isEmpty()) {
                    auto* empty = new QTreeWidgetItem(ui->gradesTree);
                    empty->setText(0, "Оценок пока нет");
                    delete acc; delete pending;
                    return;
                }

                // Строим дерево: семестр → предмет → записи
                for (auto semIt = bySemesterSubject.cbegin(); semIt != bySemesterSubject.cend(); ++semIt) {
                    auto* semItem = new QTreeWidgetItem(ui->gradesTree);
                    semItem->setText(0, semIt.key());
                    semItem->setFont(0, QFont(semItem->font(0).family(), -1, QFont::Bold));
                    semItem->setExpanded(true);

                    for (auto subjIt = semIt.value().cbegin(); subjIt != semIt.value().cend(); ++subjIt) {
                        const QList<Entry>& entries = subjIt.value();

                        // Средняя оценка по предмету (только числовые, без плюсов)
                        double sum = 0; int cnt = 0;
                        for (const Entry& e : entries)
                            if (e.value > 1) { sum += e.value; cnt++; }
                        QString avgStr = cnt > 0
                            ? QString("  ср. %1").arg(sum/cnt, 0, 'f', 1) : "";

                        auto* subjItem = new QTreeWidgetItem(semItem);
                        subjItem->setText(0, subjIt.key() + avgStr);
                        subjItem->setFont(0, QFont(subjItem->font(0).family(), -1, QFont::Bold));
                        subjItem->setExpanded(true);

                        for (const Entry& e : entries) {
                            auto* leaf = new QTreeWidgetItem(subjItem);
                            leaf->setText(0, e.detail.isEmpty() ? "(без темы)" : e.detail);
                            leaf->setText(1, e.date);
                            leaf->setText(2, markStr(e.value));

                            QColor c = e.value == 1  ? QColor("#0066cc")
                                     : e.value >= 4  ? Qt::darkGreen
                                     : e.value == 3  ? QColor("orange")
                                     : Qt::red;
                            leaf->setForeground(2, QBrush(c));
                        }
                    }
                }
                delete acc; delete pending;
            };

            // Запрос журнальных оценок
            auto* r1 = new Requester(this);
            r1->initRequester("127.0.0.1", 8081, nullptr);
            r1->sendRequest(
                QString("student/grades/%1").arg(token),
                [acc, r1, tryBuild](const QJsonObject &data) {
                    acc->journal = data["data"].toArray();
                    r1->deleteLater();
                    tryBuild();
                },
                [acc, r1, tryBuild](const QJsonObject& e) {
                    qDebug() << e; r1->deleteLater(); tryBuild();
                },
                Requester::Type::GET
            );

            // Запрос teacher_marks
            auto* r2 = new Requester(this);
            r2->initRequester("127.0.0.1", 8081, nullptr);
            r2->sendRequest(
                QString("teacher_marks/%1?token=%2").arg(sid).arg(token),
                [acc, r2, tryBuild](const QJsonObject &data) {
                    acc->marks = data["data"].toArray();
                    r2->deleteLater();
                    tryBuild();
                },
                [acc, r2, tryBuild](const QJsonObject& e) {
                    qDebug() << e; r2->deleteLater(); tryBuild();
                },
                Requester::Type::GET
            );
        },
        [r0](const QJsonObject& e){ qDebug()<<e; r0->deleteLater(); },
        Requester::Type::GET
    );
}
