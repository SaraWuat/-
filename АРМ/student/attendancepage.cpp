#include "attendancepage.h"
#include "ui_attendancepage.h"
#include "components/requester/requester.h"

#include <QSettings>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QDate>
#include <QMap>

AttendancePage::AttendancePage(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::AttendancePage)
{
    ui->setupUi(this);
    setWindowTitle("Моя посещаемость");

    connect(ui->backButton, &QPushButton::clicked, this, [this]() { this->close(); });

    loadData();
}

AttendancePage::~AttendancePage()
{
    delete ui;
}

void AttendancePage::loadData()
{
    QSettings settings("NagaevM", "studentApp");
    QString token = settings.value("auth_token").toString();

    Requester* r = new Requester(this);
    r->initRequester("127.0.0.1", 8081, nullptr);
    r->sendRequest(
        QString("attendance/%1").arg(token),
        [this, r](const QJsonObject &data) {
            m_data = data["data"].toArray();

            int total    = data["total"].toInt();
            int attended = data["attended"].toInt();
            int percent  = total > 0 ? (attended * 100 / total) : 0;

            buildTree();

            QString color = percent >= 75 ? "green" : (percent >= 50 ? "orange" : "red");
            ui->summaryLabel->setText(
                QString("Посещено: %1 / %2  (%3%)").arg(attended).arg(total).arg(percent));
            ui->summaryLabel->setStyleSheet(
                QString("font-size: 14px; font-weight: bold; color: %1;").arg(color));

            r->deleteLater();
        },
        [r](const QJsonObject &err) {
            qDebug() << "AttendancePage error:" << err;
            r->deleteLater();
        },
        Requester::Type::GET
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// Определяем «половину семестра» по дате:
//   Учебный год начинается 1 сентября.
//   1-я половина семестра: недели 1–8
//   2-я половина семестра: недели 9+
// (можно настраивать по своей логике)
// ─────────────────────────────────────────────────────────────────────────────
static int academicWeekNumber(const QDate &date)
{
    int year = date.month() >= 9 ? date.year() : date.year() - 1;
    int days = QDate(year, 9, 1).daysTo(date);
    if (days < 0) return -1;
    return days / 7 + 1;
}

void AttendancePage::buildTree()
{
    QTreeWidget* tree = ui->attendanceTree;
    tree->clear();
    tree->setColumnCount(3);
    tree->setHeaderLabels({"Дата / Предмет", "Статус", "Неделя"});
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);

    // Группируем: semester_half → week_number → [записи]
    // semester_half: "1-я половина (нед. 1–8)" / "2-я половина (нед. 9+)"
    struct Record {
        QDate   date;
        QString subject;
        bool    attended;
        int     weekNum;
    };

    QMap<QString, QMap<int, QList<Record>>> grouped;
    // Порядок ключей "половин"
    QStringList halfOrder;

    for (const QJsonValue &v : m_data) {
        QJsonObject item = v.toObject();
        QDate date = QDate::fromString(item["date"].toString(), "yyyy-MM-dd");
        if (!date.isValid()) continue;

        int wn = academicWeekNumber(date);
        if (wn < 0) continue;

        QString half = (wn <= 8)
            ? "1-я половина семестра (нед. 1–8)"
            : "2-я половина семестра (нед. 9+)";

        if (!halfOrder.contains(half))
            halfOrder.append(half);

        Record rec;
        rec.date     = date;
        rec.subject  = item["subject"].toString();
        rec.attended = item["attended"].toInt() == 1;
        rec.weekNum  = wn;

        grouped[half][wn].append(rec);
    }

    // Строим дерево
    for (const QString &half : halfOrder) {
        // Считаем итог по половине
        int halfTotal = 0, halfAtt = 0;
        for (auto &weekRecords : grouped[half]) {
            for (auto &rec : weekRecords) {
                halfTotal++;
                if (rec.attended) halfAtt++;
            }
        }
        int halfPct = halfTotal > 0 ? halfAtt * 100 / halfTotal : 0;

        QTreeWidgetItem* halfItem = new QTreeWidgetItem(tree);
        halfItem->setText(0, half);
        halfItem->setText(1, QString("%1/%2 (%2%)").arg(halfAtt).arg(halfTotal).arg(halfPct));
        halfItem->setFont(0, QFont(halfItem->font(0).family(), -1, QFont::Bold));
        halfItem->setExpanded(false); // свёрнуто по умолчанию

        // Итоговый процент — цвет
        QColor halfColor = halfPct >= 75 ? Qt::darkGreen : (halfPct >= 50 ? QColor("orange") : Qt::red);
        halfItem->setForeground(1, QBrush(halfColor));

        const QMap<int, QList<Record>> &weekMap = grouped[half];
        for (auto weekIt = weekMap.cbegin(); weekIt != weekMap.cend(); ++weekIt) {
            int weekNum = weekIt.key();
            const QList<Record> &records = weekIt.value();

            int wTotal = records.size();
            int wAtt   = 0;
            for (auto &rec : records) { if (rec.attended) wAtt++; }
            int wPct = wTotal > 0 ? wAtt * 100 / wTotal : 0;

            QTreeWidgetItem* weekItem = new QTreeWidgetItem(halfItem);
            weekItem->setText(0, QString("Неделя %1").arg(weekNum));
            weekItem->setText(1, QString("%1/%2 (%3%)").arg(wAtt).arg(wTotal).arg(wPct));
            weekItem->setText(2, QString("нед. %1").arg(weekNum));
            weekItem->setExpanded(false);

            QColor wColor = wPct >= 75 ? Qt::darkGreen : (wPct >= 50 ? QColor("orange") : Qt::red);
            weekItem->setForeground(1, QBrush(wColor));

            // Листья — конкретные занятия
            for (const Record &rec : records) {
                QTreeWidgetItem* leaf = new QTreeWidgetItem(weekItem);
                leaf->setText(0, rec.date.toString("dd.MM  ") + rec.subject);
                leaf->setText(1, rec.attended ? "✓ Был" : "✗ Отсутствовал");
                leaf->setForeground(1, QBrush(rec.attended ? Qt::darkGreen : Qt::red));
                leaf->setText(2, "");
            }
        }
    }

    tree->resizeColumnToContents(1);
    tree->resizeColumnToContents(2);
}
