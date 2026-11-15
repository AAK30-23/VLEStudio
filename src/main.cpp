#include <QApplication>
#include <QGroupBox>
#include "diagram_widget.h"
#include <QWidget>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QMessageBox>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QVariant>
#include <QComboBox>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <QScrollArea>
#include <QSignalBlocker>

#include <algorithm>

#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include <exception>
#include <vector>

namespace py = pybind11;

// Результат расчёта
struct CalculationResult {
    QString mixture = "ETHANOL_WATER";
    QString modelCode = "PR";
    QString model;

    double temperature = 0.0;
    double pressure = 0.0;

    double feedEthanol = 0.0;
    double feedWater = 0.0;

    int phaseCount = 0;

    bool hasLiquid = false;
    double liquidEthanol = 0.0;
    double liquidWater = 0.0;

    bool hasVapor = false;
    double vaporEthanol = 0.0;
    double vaporWater = 0.0;

    bool hasVaporFraction = false;
    double vaporFraction = 0.0;
};

// Результат расчёта диаграммы T-x-y или P-x-y
struct DiagramResult {
    QString kind;       // TXY или PXY
    QString mixture;
    QString model;

    double pressure = 0.0;     // для T-x-y
    double temperature = 0.0;  // для P-x-y

    QVector<double> x1;
    QVector<double> y1;
    QVector<double> values;    // температура или давление

    bool hasAzeotrope = false;
    double azeotropeX1 = 0.0;
    double azeotropeValue = 0.0;
};

QString mixtureDisplayName(const QString &mixture)
{
    if (mixture == "ACETONE_CHLOROFORM") {
        return "Ацетон – хлороформ";
    }

    return "Этанол – вода";
}

QString firstComponentName(const QString &mixture)
{
    if (mixture == "ACETONE_CHLOROFORM") {
        return "ацетон";
    }

    return "этанол";
}

QString secondComponentName(const QString &mixture)
{
    if (mixture == "ACETONE_CHLOROFORM") {
        return "хлороформ";
    }

    return "вода";
}

QString modelDisplayName(const QString &model)
{
    if (model == "UNIFAC_PR") {
        return "UNIFAC + Peng–Robinson (gamma–phi)";
    }

    return "Peng–Robinson (phi–phi)";
}

QString diagramKindName(const QString &kind)
{
    if (kind == "PXY") {
        return "P–x–y";
    }

    return "T–x–y";
}

// Элемент таблицы с корректной сортировкой чисел
class SortableTableWidgetItem : public QTableWidgetItem {
public:
    SortableTableWidgetItem(
        const QString &text,
        const QVariant &sortValue,
        bool numeric = false
    )
        : QTableWidgetItem(text),
          sortValue(sortValue),
          numeric(numeric)
    {
    }

    bool operator<(const QTableWidgetItem &other) const override
    {
        const auto *otherItem =
            dynamic_cast<const SortableTableWidgetItem *>(&other);

        if (otherItem == nullptr) {
            return QTableWidgetItem::operator<(other);
        }

        if (numeric && otherItem->numeric) {
            if (!sortValue.isValid() || sortValue.isNull()) {
                return true;
            }

            if (!otherItem->sortValue.isValid()
                || otherItem->sortValue.isNull()) {
                return false;
            }

            return sortValue.toDouble()
                < otherItem->sortValue.toDouble();
        }

        return QString::localeAwareCompare(
            text(),
            otherItem->text()
        ) < 0;
    }

private:
    QVariant sortValue;
    bool numeric;
};

// Создание таблицы SQLite
bool createDatabaseTable(QSqlDatabase &db)
{
    QSqlQuery query(db);

    QString sql = R"(
        CREATE TABLE IF NOT EXISTS calculations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            created_at TEXT NOT NULL DEFAULT
                (datetime('now', 'localtime')),
            model TEXT NOT NULL,
            temperature_K REAL NOT NULL,
            pressure_Pa REAL NOT NULL,
            feed_ethanol REAL NOT NULL,
            feed_water REAL NOT NULL,
            phase_count INTEGER NOT NULL,
            liquid_ethanol REAL,
            liquid_water REAL,
            vapor_ethanol REAL,
            vapor_water REAL,
            vapor_fraction REAL
        )
    )";

    if (!query.exec(sql)) {
        return false;
    }

    // Таблицы для сохранения диаграмм T–x–y и P–x–y
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS diagrams (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            created_at TEXT NOT NULL DEFAULT
                (datetime('now', 'localtime')),
            kind TEXT NOT NULL,
            mixture TEXT NOT NULL,
            model TEXT NOT NULL,
            pressure_Pa REAL,
            temperature_K REAL,
            point_count INTEGER NOT NULL,
            azeotrope_x1 REAL,
            azeotrope_temperature_K REAL,
            azeotrope_pressure_Pa REAL
        )
    )")) {
        return false;
    }

    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS diagram_points (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            diagram_id INTEGER NOT NULL,
            x1 REAL NOT NULL,
            y1 REAL NOT NULL,
            temperature_K REAL,
            pressure_Pa REAL
        )
    )")) {
        return false;
    }

    // Миграция базы, созданной предыдущими версиями приложения.
    // Ошибка "duplicate column name" означает, что столбец уже есть.
    query.exec(
        "ALTER TABLE calculations ADD COLUMN mixture TEXT NOT NULL "
        "DEFAULT 'ETHANOL_WATER'"
    );
    query.exec(
        "ALTER TABLE calculations ADD COLUMN model_code TEXT NOT NULL "
        "DEFAULT 'PR'"
    );

    return true;
}

// Сохранение расчёта
bool saveCalculation(
    QSqlDatabase &db,
    const CalculationResult &result,
    QString &errorMessage
)
{
    QSqlQuery query(db);

    query.prepare(R"(
        INSERT INTO calculations (
            model,
            model_code,
            mixture,
            temperature_K,
            pressure_Pa,
            feed_ethanol,
            feed_water,
            phase_count,
            liquid_ethanol,
            liquid_water,
            vapor_ethanol,
            vapor_water,
            vapor_fraction
        )
        VALUES (
            :model,
            :model_code,
            :mixture,
            :temperature,
            :pressure,
            :feed_ethanol,
            :feed_water,
            :phase_count,
            :liquid_ethanol,
            :liquid_water,
            :vapor_ethanol,
            :vapor_water,
            :vapor_fraction
        )
    )");

    query.bindValue(":model", result.model);
    query.bindValue(":model_code", result.modelCode);
    query.bindValue(":mixture", result.mixture);
    query.bindValue(":temperature", result.temperature);
query.bindValue(":pressure",

result.pressure);
    query.bindValue(":feed_ethanol", result.feedEthanol);
    query.bindValue(":feed_water", result.feedWater);
    query.bindValue(":phase_count", result.phaseCount);

    if (result.hasLiquid) {
        query.bindValue(":liquid_ethanol", result.liquidEthanol);
        query.bindValue(":liquid_water", result.liquidWater);
    } else {
        query.bindValue(":liquid_ethanol", QVariant());
        query.bindValue(":liquid_water", QVariant());
    }

    if (result.hasVapor) {
        query.bindValue(":vapor_ethanol", result.vaporEthanol);
        query.bindValue(":vapor_water", result.vaporWater);
    } else {
        query.bindValue(":vapor_ethanol", QVariant());
        query.bindValue(":vapor_water", QVariant());
    }

    if (result.hasVaporFraction) {
        query.bindValue(":vapor_fraction", result.vaporFraction);
    } else {
        query.bindValue(":vapor_fraction", QVariant());
    }

    if (!query.exec()) {
        errorMessage = query.lastError().text();
        return false;
    }

    return true;
}

// Сохранение диаграммы
bool saveDiagram(
    QSqlDatabase &db,
    const DiagramResult &result,
    QString &errorMessage
)
{
    if (!db.transaction()) {
        errorMessage = db.lastError().text();
        return false;
    }

    QSqlQuery query(db);

    query.prepare(R"(
        INSERT INTO diagrams (
            kind,
            mixture,
            model,
            pressure_Pa,
            temperature_K,
            point_count,
            azeotrope_x1,
            azeotrope_temperature_K,
            azeotrope_pressure_Pa
        )
        VALUES (
            :kind,
            :mixture,
            :model,
            :pressure,
            :temperature,
            :point_count,
            :azeotrope_x1,
            :azeotrope_temperature,
            :azeotrope_pressure
        )
    )");

    query.bindValue(":kind", result.kind);
    query.bindValue(":mixture", result.mixture);
    query.bindValue(":model", result.model);
    query.bindValue(
        ":pressure",
        result.kind == "TXY" ? QVariant(result.pressure) : QVariant()
    );
    query.bindValue(
        ":temperature",
        result.kind == "PXY" ? QVariant(result.temperature) : QVariant()
    );
    query.bindValue(":point_count", result.x1.size());

    if (result.hasAzeotrope) {
        query.bindValue(":azeotrope_x1", result.azeotropeX1);
        query.bindValue(
            ":azeotrope_temperature",
            result.kind == "TXY"
                ? QVariant(result.azeotropeValue)
                : QVariant()
        );
        query.bindValue(
            ":azeotrope_pressure",
            result.kind == "PXY"
                ? QVariant(result.azeotropeValue)
                : QVariant()
        );
    } else {
        query.bindValue(":azeotrope_x1", QVariant());
        query.bindValue(":azeotrope_temperature", QVariant());
        query.bindValue(":azeotrope_pressure", QVariant());
    }

    if (!query.exec()) {
        errorMessage = query.lastError().text();
        db.rollback();
        return false;
    }

    const int diagramId = query.lastInsertId().toInt();

    QSqlQuery pointQuery(db);

    pointQuery.prepare(R"(
        INSERT INTO diagram_points (
            diagram_id,
            x1,
            y1,
            temperature_K,
            pressure_Pa
        )
        VALUES (
            :diagram_id,
            :x1,
            :y1,
            :temperature,
            :pressure
        )
    )");

    for (int index = 0; index < result.x1.size(); ++index) {
        pointQuery.bindValue(":diagram_id", diagramId);
        pointQuery.bindValue(":x1", result.x1[index]);
        pointQuery.bindValue(":y1", result.y1[index]);
        pointQuery.bindValue(
            ":temperature",
            result.kind == "TXY"
                ? QVariant(result.values[index])
                : QVariant()
        );
        pointQuery.bindValue(
            ":pressure",
            result.kind == "PXY"
                ? QVariant(result.values[index])
                : QVariant()
        );

        if (!pointQuery.exec()) {
            errorMessage = pointQuery.lastError().text();
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        errorMessage = db.lastError().text();
        db.rollback();
        return false;
    }

    return true;
}

// Преобразование значения базы данных в текст
QString displayDatabaseValue(const QVariant &value, int decimals = 5)
{
    if (!value.isValid() || value.isNull()) {
        return "Не определено";
    }

    bool ok = false;
    double number = value.toDouble(&ok);

    if (ok) {
        return QString::number(number, 'f', decimals);
    }

    return value.toString();
}

// Просмотр подробностей отдельного расчёта
void showCalculationDetails(
    QSqlDatabase &db,
    int calculationId,
    QWidget *parent
)
{
    QSqlQuery query(db);

    query.prepare(R"(
        SELECT
            id,
            created_at,
            model,
            temperature_K,
            pressure_Pa,
            feed_ethanol,
            feed_water,
            phase_count,
            liquid_ethanol,
            liquid_water,
            vapor_ethanol,
            vapor_water,
            vapor_fraction,
            mixture
        FROM calculations
        WHERE id = :id
    )");

    query.bindValue(":id", calculationId);

    if (!query.exec() || !query.next()) {
        QMessageBox::critical(
            parent,
            "Ошибка",
            "Не удалось получить подробности расчёта:\n"
                + query.lastError().text()
        );
        return;
    }

    const QString mixture = query.value(13).toString();
    const QString firstComponent = firstComponentName(mixture);
    const QString secondComponent = secondComponentName(mixture);

    QStringList lines;

    lines << "ID расчёта: " + query.value(0).toString();
    lines << "Дата: " + query.value(1).toString();
    lines << "Смесь: " + mixtureDisplayName(mixture);
    lines << "Модель: " + query.value(2).toString();

    lines << "";
    lines << "Параметры расчёта:";

    lines << "Температура: "
        + displayDatabaseValue(query.value(3), 2) + " K";

    lines << "Давление: "
        + displayDatabaseValue(query.value(4), 0) + " Pa";

    lines << "";
    lines << "Исходный состав:";

    lines << firstComponent + ": "
        + displayDatabaseValue(query.value(5));

    lines << secondComponent + ": "
        + displayDatabaseValue(query.value(6));

    lines << "";
    lines << "Результаты:";

    lines << "Количество фаз: "
        + query.value(7).toString();

    lines << "";

    if (!query.value(8).isNull()) {
        lines << "Жидкая фаза:";
        lines << firstComponent + ": "
            + displayDatabaseValue(query.value(8));
        lines << secondComponent + ": "
            + displayDatabaseValue(query.value(9));
    } else {
        lines << "Жидкая фаза отсутствует.";
    }

    lines << "";

    if (!query.value(10).isNull()) {
        lines << "Паровая фаза:";
        lines << firstComponent + ": "
            + displayDatabaseValue(query.value(10));
        lines << secondComponent + ": "
            + displayDatabaseValue(query.value(11));
    } else {
        lines << "Паровая фаза отсутствует.";
    }

    lines << "";

    if (!query.value(12).isNull()) {
        lines << "Доля пара: "
            + displayDatabaseValue(query.value(12));
    } else {
        lines << "Доля пара не определена.";
    }

    QDialog dialog(parent);
    dialog.setWindowTitle("Подробности расчёта №"
        + QString::number(calculationId));
    dialog.resize(450, 500);

    auto *layout = new QVBoxLayout(&dialog);

    auto *detailsLabel = new QLabel(lines.join("\n"));
    detailsLabel->setTextInteractionFlags(
        Qt::TextSelectableByMouse
    );
    detailsLabel->setWordWrap(true);

    auto *closeButton = new QPushButton("Закрыть");

    layout->addWidget(detailsLabel);
    layout->addStretch();
    layout->addWidget(closeButton);

    QObject::connect(
        closeButton,
        &QPushButton::clicked,
        &dialog,
        &QDialog::accept
    );

    dialog.exec();
}

// Получение ID выбранной записи
int selectedCalculationId(QTableWidget *table)
{
    int row = table->currentRow();

    if (row < 0) {
        return -1;
    }

    QTableWidgetItem *item = table->item(row, 0);

    if (item == nullptr) {
        return -1;
    }

    bool ok = false;
    int id = item->text().toInt(&ok);

    return ok ? id : -1;
}

// Экспорт истории в CSV
void exportHistoryToCsv(
    QSqlDatabase &db,
    QWidget *parent
)
{
    QString fileName = QFileDialog::getSaveFileName(
        parent,
        "Экспорт истории расчётов",
        QDir::homePath() + "/vle_history.csv",
        "CSV-файлы (*.csv)"
    );

    if (fileName.isEmpty()) {
        return;
    }

    if (!fileName.endsWith(".csv", Qt::CaseInsensitive)) {
        fileName += ".csv";
    }

    QSqlQuery query(db);

    if (!query.exec(R"(
        SELECT
            id,
            created_at,
            model,
            temperature_K,
            pressure_Pa,
            feed_ethanol,
            feed_water,
            phase_count,
            liquid_ethanol,
            liquid_water,
            vapor_ethanol,
            vapor_water,
            vapor_fraction,
            mixture
        FROM calculations
        ORDER BY id
    )")) {
        QMessageBox::critical(
            parent,
            "Ошибка экспорта",
            query.lastError().text()
        );
        return;
    }

    QFile file(fileName);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(
            parent,
            "Ошибка экспорта",
            "Не удалось создать файл:\n" + file.errorString()
        );
        return;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    // BOM для корректного распознавания UTF-8 в Excel
    stream << QChar(0xFEFF);

    QStringList headers = {
        "ID",
        "Дата",
        "Смесь",
        "Модель",
        "Температура, K",
        "Давление, Pa",
        "Компонент 1 (исходный)",
        "Компонент 2 (исходный)",
        "Количество фаз",
        "Компонент 1 (жидкость)",
        "Компонент 2 (жидкость)",
        "Компонент 1 (пар)",
        "Компонент 2 (пар)",
        "Доля пара"
    };

    // Все поля заключаем в кавычки.
    // Внутренние кавычки удваиваем.
    auto writeCsvRow = [&stream](const QStringList &values) {
        QStringList escapedValues;

        for (const QString &value : values) {
            QString escaped = value;
            escaped.replace("\"", "\"\"");
            escapedValues << "\"" + escaped + "\"";
        }

        stream << escapedValues.join(";") << "\n";
    };

    writeCsvRow(headers);

    while (query.next()) {
        QStringList values;

        for (int column = 0; column < 14; ++column) {
            if (column == 2) {
                values << mixtureDisplayName(
                    query.value(column).toString()
                );
            } else if (query.value(column).isNull()) {
                values << "";
            } else {
                values << query.value(column).toString();
            }
        }

        writeCsvRow(values);
    }

    stream.flush();
    file.close();

    QMessageBox::information(
        parent,
        "Экспорт завершён",
        "История расчётов успешно сохранена:\n" + fileName
    );
}

// Окно истории расчётов
void showHistory(QSqlDatabase &db, QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle("История расчётов");
    dialog.resize(1250, 550);

    auto *layout = new QVBoxLayout(&dialog);

    auto *table = new QTableWidget(&dialog);

    QStringList headers = {
        "ID",
        "Дата",
        "Смесь",
        "Модель",
        "T, K",
        "P, Pa",
        "Комп. 1 (исх.)",
        "Комп. 2 (исх.)",
        "Фазы",
        "Комп. 1 (жидк.)",
        "Комп. 2 (жидк.)",
        "Комп. 1 (пар)",
        "Комп. 2 (пар)",
        "Доля пара"
    };

    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);

    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);

    table->setSortingEnabled(false);

    QSqlQuery query(db);

    if (!query.exec(R"(
        SELECT
            id,
            created_at,
            mixture,
            model,
            temperature_K,
            pressure_Pa,
            feed_ethanol,
            feed_water,
            phase_count,
            liquid_ethanol,
            liquid_water,
            vapor_ethanol,
            vapor_water,
            vapor_fraction
        FROM calculations
        ORDER BY id DESC
    )")) {
        QMessageBox::critical(
            &dialog,
            "Ошибка",
            "Не удалось загрузить историю:\n"
                + query.lastError().text()
        );
        return;
    }

    // Заполнение таблицы
    int row = 0;

    while (query.next()) {
        table->insertRow(row);

        for (int column = 0; column < headers.size(); ++column) {
            QVariant value = query.value(column);

            QString text;

            if (column == 2) {
                text = mixtureDisplayName(value.toString());
            } else if (value.isNull()) {
                text = "—";
            } else {
                text = value.toString();
            }

            bool numeric =
                column == 0 || (column >= 4 && column <= 13);

            auto *item = new SortableTableWidgetItem(
                text,
                value,
                numeric
            );

            table->setItem(row, column, item);
        }

        ++row;
    }

    table->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents
    );

    table->horizontalHeader()->setStretchLastSection(true);

    // Включаем сортировку после заполнения таблицы
    table->setSortingEnabled(true);

    layout->addWidget(table);

    // Кнопки управления историей
    auto *buttonsLayout = new QHBoxLayout();

    auto *detailsButton =
        new QPushButton("Подробности");

    auto (*deleteButton) =
        new QPushButton("Удалить запись");

    auto *exportButton =
        new QPushButton("Экспорт в CSV");

    auto *closeButton =
        new QPushButton("Закрыть");

    buttonsLayout->addWidget(detailsButton);
    buttonsLayout->addWidget(deleteButton);
    buttonsLayout->addWidget(exportButton);
    buttonsLayout->addStretch();
    buttonsLayout->addWidget(closeButton);

    layout->addLayout(buttonsLayout);

    // Подробности выбранной записи
    auto showSelectedDetails = [&]() {
        int id = selectedCalculationId(table);

        if (id < 0) {
            QMessageBox::warning(
                &dialog,
                "Запись не выбрана",
                "Сначала выберите расчёт в таблице."
            );
            return;
        }

        showCalculationDetails(db, id, &dialog);
    };

    QObject::connect(
        detailsButton,
        &QPushButton::clicked,
        &dialog,
        showSelectedDetails
    );

    // Двойной щелчок по строке открывает подробности
    QObject::connect(
        table,
        &QTableWidget::cellDoubleClicked,
        &dialog,
        [showSelectedDetails](int, int) {
            showSelectedDetails();
        }
    );

    // Удаление выбранной записи
    QObject::connect(
        deleteButton,
        &QPushButton::clicked,
        &dialog,
        [&]() {
            int id = selectedCalculationId(table);

            if (id < 0) {
                QMessageBox::warning(
                    &dialog,
                    "Запись не выбрана",
                    "Сначала выберите запись, которую нужно удалить."
                );
                return;
            }

            auto answer = QMessageBox::question(
                &dialog,
                "Подтверждение удаления",
                "Вы действительно хотите удалить расчёт №"
                    + QString::number(id) + "?",
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No
            );

            if (answer != QMessageBox::Yes) {
                return;
            }

            QSqlQuery deleteQuery(db);

            deleteQuery.prepare(
                "DELETE FROM calculations WHERE id = :id"
            );

            deleteQuery.bindValue(":id", id);

            if (!deleteQuery.exec()) {
                QMessageBox::critical(
                    &dialog,
                    "Ошибка удаления",
                    deleteQuery.lastError().text()
                );
                return;
            }

            int row = table->currentRow();

            if (row >= 0) {
                table->removeRow(row);
            }

            QMessageBox::information(
                &dialog,
                "Удаление",
                "Запись №" + QString::number(id)
                    + " успешно удалена."
            );
        }
    );

    // Экспорт всей истории
    QObject::connect(
        exportButton,
        &QPushButton::clicked,
        &dialog,
        [&]() {
            exportHistoryToCsv(db, &dialog);
        }
    );

    QObject::connect(
        closeButton,
        &QPushButton::clicked,
        &dialog,
        &QDialog::accept
    );

    dialog.exec();
}

// Запись диаграммы в CSV-файл
bool writeDiagramCsv(
    const QString &fileName,
    const DiagramResult &diagram,
    QString &errorMessage
)
{
    QFile file(fileName);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errorMessage = file.errorString();
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    // BOM для корректного распознавания UTF-8 в Excel
    stream << QChar(0xFEFF);

    stream << "\"# Смесь: ";
    stream << mixtureDisplayName(diagram.mixture);
    stream << "; модель: ";
    stream << modelDisplayName(diagram.model);
    stream << "; вид: ";
    stream << diagramKindName(diagram.kind);

    if (diagram.kind == "TXY") {
        stream << "; давление, Pa: " << QString::number(diagram.pressure, 'f', 0);
    } else {
        stream << "; температура, K: " << QString::number(diagram.temperature, 'f', 2);
    }

    if (diagram.hasAzeotrope) {
        stream << "; азеотроп x1: " << QString::number(diagram.azeotropeX1, 'f', 5);

        if (diagram.kind == "TXY") {
            stream << "; азеотроп T, K: " << QString::number(diagram.azeotropeValue, 'f', 3);
        } else {
            stream << "; азеотроп P, Pa: " << QString::number(diagram.azeotropeValue, 'f', 0);
        }
    }

    stream << "\"\n";

    stream << "\"x1\";\"y1\";\"";
    stream << (diagram.kind == "TXY" ? "Температура, K" : "Давление, Pa");
    stream << "\"\n";

    for (int index = 0; index < diagram.x1.size(); ++index) {
        stream << QString::number(diagram.x1[index], 'f', 6) << ";"
               << QString::number(diagram.y1[index], 'f', 6) << ";";

        if (diagram.kind == "TXY") {
            stream << QString::number(diagram.values[index], 'f', 3);
        } else {
            stream << QString::number(diagram.values[index], 'f', 1);
        }

        stream << "\n";
    }

    stream.flush();
    file.close();

    return true;
}

// Экспорт текущей диаграммы в CSV
void exportDiagramToCsv(
    const DiagramResult &diagram,
    QWidget *parent
)
{
    QString defaultName =
        QDir::homePath()
        + "/vle_"
        + diagram.kind.toLower()
        + "_"
        + diagram.mixture.toLower()
        + ".csv";

    QString fileName = QFileDialog::getSaveFileName(
        parent,
        "Экспорт диаграммы",
        defaultName,
        "CSV-файлы (*.csv)"
    );

    if (fileName.isEmpty()) {
        return;
    }

    if (!fileName.endsWith(".csv", Qt::CaseInsensitive)) {
        fileName += ".csv";
    }

    QString errorMessage;

    if (!writeDiagramCsv(fileName, diagram, errorMessage)) {
        QMessageBox::critical(
            parent,
            "Ошибка экспорта",
            "Не удалось создать файл:\n" + errorMessage
        );
        return;
    }

    QMessageBox::information(
        parent,
        "Экспорт завершён",
        "Диаграмма сохранена:\n" + fileName
    );
}

// Загрузка сохранённой диаграммы из базы
bool loadDiagram(
    QSqlDatabase &db,
    int diagramId,
    DiagramResult &diagram,
    QString &errorMessage
)
{
    QSqlQuery query(db);

    query.prepare(R"(
        SELECT
            kind,
            mixture,
            model,
            pressure_Pa,
            temperature_K,
            azeotrope_x1,
            azeotrope_temperature_K,
            azeotrope_pressure_Pa
        FROM diagrams
        WHERE id = :id
    )");

    query.bindValue(":id", diagramId);

    if (!query.exec() || !query.next()) {
        errorMessage = query.lastError().text();
        return false;
    }

    diagram = DiagramResult{};

    diagram.kind = query.value(0).toString();
    diagram.mixture = query.value(1).toString();
    diagram.model = query.value(2).toString();
    diagram.pressure = query.value(3).toDouble();
    diagram.temperature = query.value(4).toDouble();

    if (!query.value(5).isNull()) {
        diagram.hasAzeotrope = true;
        diagram.azeotropeX1 = query.value(5).toDouble();

        if (diagram.kind == "TXY") {
            diagram.azeotropeValue = query.value(6).toDouble();
        } else {
            diagram.azeotropeValue = query.value(7).toDouble();
        }
    }

    QSqlQuery pointsQuery(db);

    pointsQuery.prepare(R"(
        SELECT x1, y1, temperature_K, pressure_Pa
        FROM diagram_points
        WHERE diagram_id = :id
        ORDER BY id
    )");

    pointsQuery.bindValue(":id", diagramId);

    if (!pointsQuery.exec()) {
        errorMessage = pointsQuery.lastError().text();
        return false;
    }

    while (pointsQuery.next()) {
        diagram.x1 << pointsQuery.value(0).toDouble();
        diagram.y1 << pointsQuery.value(1).toDouble();

        if (diagram.kind == "TXY") {
            diagram.values << pointsQuery.value(2).toDouble();
        } else {
            diagram.values << pointsQuery.value(3).toDouble();
        }
    }

    return true;
}

// Подробности сохранённой диаграммы
void showDiagramDetails(
    QSqlDatabase &db,
    int diagramId,
    QWidget *parent
)
{
    DiagramResult diagram;
    QString errorMessage;

    if (!loadDiagram(db, diagramId, diagram, errorMessage)) {
        QMessageBox::critical(
            parent,
            "Ошибка",
            "Не удалось загрузить диаграмму:\n" + errorMessage
        );
        return;
    }

    QStringList lines;

    lines << "ID диаграммы: " + QString::number(diagramId);
    lines << "Вид: " + diagramKindName(diagram.kind);
    lines << "Смесь: " + mixtureDisplayName(diagram.mixture);
    lines << "Модель: " + modelDisplayName(diagram.model);

    if (diagram.kind == "TXY") {
        lines << "Давление: "
            + QString::number(diagram.pressure, 'f', 0) + " Pa";
    } else {
        lines << "Температура: "
            + QString::number(diagram.temperature, 'f', 2) + " K";
    }

    lines << "Количество точек: "
        + QString::number(diagram.x1.size());

    lines << "";

    if (diagram.hasAzeotrope) {
        lines << "Азеотропная точка:";
        lines << "x1 = "
            + QString::number(diagram.azeotropeX1, 'f', 5);

        if (diagram.kind == "TXY") {
            lines << "T = "
                + QString::number(diagram.azeotropeValue, 'f', 3) + " K";
        } else {
            lines << "P = "
                + QString::number(diagram.azeotropeValue, 'f', 0) + " Pa";
        }
    } else {
        lines << "Азеотропная точка не найдена.";
    }

    if (!diagram.x1.isEmpty()) {
        lines << "";
        lines << "Первая точка: x1 = "
            + QString::number(diagram.x1.first(), 'f', 4)
            + ", y1 = "
            + QString::number(diagram.y1.first(), 'f', 4);

        lines << "Последняя точка: x1 = "
            + QString::number(diagram.x1.last(), 'f', 4)
            + ", y1 = "
            + QString::number(diagram.y1.last(), 'f', 4);
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(
        "Подробности диаграммы №" + QString::number(diagramId)
    );
    dialog.resize(450, 400);

    auto *layout = new QVBoxLayout(&dialog);

    auto *detailsLabel = new QLabel(lines.join("\n"));
    detailsLabel->setTextInteractionFlags(
        Qt::TextSelectableByMouse
    );
    detailsLabel->setWordWrap(true);

    auto *closeButton = new QPushButton("Закрыть");

    layout->addWidget(detailsLabel);
    layout->addStretch();
    layout->addWidget(closeButton);

    QObject::connect(
        closeButton,
        &QPushButton::clicked,
        &dialog,
        &QDialog::accept
    );

    dialog.exec();
}

// История сохранённых диаграмм
void showDiagramHistory(QSqlDatabase &db, QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle("История диаграмм");
    dialog.resize(1150, 500);

    auto *layout = new QVBoxLayout(&dialog);

    auto *table = new QTableWidget(&dialog);

    QStringList headers = {
        "ID",
        "Дата",
        "Вид",
        "Смесь",
        "Модель",
        "P, Pa",
        "T, K",
        "Точек",
        "Азеотроп x1",
        "Азеотроп T, K",
        "Азеотроп P, Pa"
    };

    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSortingEnabled(false);

    QSqlQuery query(db);

    if (!query.exec(R"(
        SELECT
            id,
            created_at,
            kind,
            mixture,
            model,
            pressure_Pa,
            temperature_K,
            point_count,
            azeotrope_x1,
            azeotrope_temperature_K,
            azeotrope_pressure_Pa
        FROM diagrams
        ORDER BY id DESC
    )")) {
        QMessageBox::critical(
            &dialog,
            "Ошибка",
            "Не удалось загрузить историю диаграмм:\n"
                + query.lastError().text()
        );
        return;
    }

    int row = 0;

    while (query.next()) {
        table->insertRow(row);

        for (int column = 0; column < headers.size(); ++column) {
            QVariant value = query.value(column);
            QString text;

            if (column == 2) {
                text = diagramKindName(value.toString());
            } else if (column == 3) {
                text = mixtureDisplayName(value.toString());
            } else if (column == 4) {
                text = modelDisplayName(value.toString());
            } else if (column == 5 && value.isNull()) {
                text = "—";
            } else if (column == 6 && value.isNull()) {
                text = "—";
            } else if (column == 8 && value.isNull()) {
                text = "—";
            } else if (column == 9 && value.isNull()) {
                text = "—";
            } else if (column == 10 && value.isNull()) {
                text = "—";
            } else if (value.isNull()) {
                text = "—";
            } else if (column == 8) {
                text = QString::number(value.toDouble(), 'f', 5);
            } else if (column == 9 || column == 10) {
                text = QString::number(value.toDouble(), 'f', 5);
            } else {
                text = value.toString();
            }

            bool numeric = column == 0 || (column >= 5 && column <= 10);

            auto *item = new SortableTableWidgetItem(
                text,
                value,
                numeric
            );

            table->setItem(row, column, item);
        }

        ++row;
    }

    table->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents
    );
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSortingEnabled(true);

    layout->addWidget(table);

    auto *buttonsLayout = new QHBoxLayout();

    auto *detailsButton = new QPushButton("Подробности");
    auto *exportButton = new QPushButton("Экспорт в CSV");
    auto *deleteButton = new QPushButton("Удалить запись");
    auto *closeButton = new QPushButton("Закрыть");

    buttonsLayout->addWidget(detailsButton);
    buttonsLayout->addWidget(exportButton);
    buttonsLayout->addWidget(deleteButton);
    buttonsLayout->addStretch();
    buttonsLayout->addWidget(closeButton);

    layout->addLayout(buttonsLayout);

    auto selectedId = [table]() {
        int currentRow = table->currentRow();

        if (currentRow < 0) {
            return -1;
        }

        QTableWidgetItem *item = table->item(currentRow, 0);

        return item == nullptr ? -1 : item->text().toInt();
    };

    auto showDetails = [&]() {
        int id = selectedId();

        if (id < 0) {
            QMessageBox::warning(
                &dialog,
                "Запись не выбрана",
                "Сначала выберите диаграмму в таблице."
            );
            return;
        }

        showDiagramDetails(db, id, &dialog);
    };

    QObject::connect(
        detailsButton,
        &QPushButton::clicked,
        &dialog,
        showDetails
    );

    QObject::connect(
        table,
        &QTableWidget::cellDoubleClicked,
        &dialog,
        [showDetails](int, int) {
            showDetails();
        }
    );

    QObject::connect(
        exportButton,
        &QPushButton::clicked,
        &dialog,
        [&]() {
            int id = selectedId();

            if (id < 0) {
                QMessageBox::warning(
                    &dialog,
                    "Запись не выбрана",
                    "Сначала выберите диаграмму для экспорта."
                );
                return;
            }

            DiagramResult diagram;
            QString errorMessage;

            if (!loadDiagram(db, id, diagram, errorMessage)) {
                QMessageBox::critical(
                    &dialog,
                    "Ошибка экспорта",
                    "Не удалось загрузить диаграмму:\n"
                        + errorMessage
                );
                return;
            }

            exportDiagramToCsv(diagram, &dialog);
        }
    );

    QObject::connect(
        deleteButton,
        &QPushButton::clicked,
        &dialog,
        [&]() {
            int id = selectedId();

            if (id < 0) {
                QMessageBox::warning(
                    &dialog,
                    "Запись не выбрана",
                    "Сначала выберите диаграмму для удаления."
                );
                return;
            }

            auto answer = QMessageBox::question(
                &dialog,
                "Подтверждение удаления",
                "Удалить диаграмму №" + QString::number(id) + "?",
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No
            );

            if (answer != QMessageBox::Yes) {
                return;
            }

            QSqlQuery deletePoints(db);
            deletePoints.prepare(
                "DELETE FROM diagram_points WHERE diagram_id = :id"
            );
            deletePoints.bindValue(":id", id);
            deletePoints.exec();

            QSqlQuery deleteDiagram(db);
            deleteDiagram.prepare(
                "DELETE FROM diagrams WHERE id = :id"
            );
            deleteDiagram.bindValue(":id", id);

            if (!deleteDiagram.exec()) {
                QMessageBox::critical(
                    &dialog,
                    "Ошибка удаления",
                    deleteDiagram.lastError().text()
                );
                return;
            }

            int currentRow = table->currentRow();

            if (currentRow >= 0) {
                table->removeRow(currentRow);
            }
        }
    );

    QObject::connect(
        closeButton,
        &QPushButton::clicked,
        &dialog,
        &QDialog::accept
    );

    dialog.exec();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    try {
        // Запуск Python
        py::scoped_interpreter guard{};

        py::module_ sys = py::module_::import("sys");
        sys.attr("path").attr("insert")(0, VLE_SOURCE_DIR "/python");
        sys.attr("path").attr("insert")(0, VLE_PYTHON_SITE_PACKAGES);

        py::module_ calc = py::module_::import("vle_calc");

        // Подготовка папки базы данных
        QString dataDirectory =
            QString(VLE_SOURCE_DIR) + "/data";

        if (!QDir().mkpath(dataDirectory)) {
            QMessageBox::critical(
                nullptr,
                "Ошибка",
                "Не удалось создать папку data."
            );
            return 1;
        }

        // Подключение SQLite
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");

        QString databasePath =
            dataDirectory + "/vle_history.db";

        db.setDatabaseName(databasePath);

        if (!db.open()) {
            QMessageBox::critical(
                nullptr,
                "Ошибка базы данных",
                "Не удалось открыть SQLite:\n"
                    + db.lastError().text()
            );
            return 1;
        }

        if (!createDatabaseTable(db)) {
            QMessageBox::critical(
                nullptr,
                "Ошибка базы данных",
                "Не удалось создать таблицу:\n"
                    + db.lastError().text()
            );
            return 1;
        }

        // Главное окно
        QWidget window;
        window.setWindowTitle("VLE Studio — фазовое равновесие");
        window.resize(800, 650);

        // Область прокрутки
        auto *scrollArea = new QScrollArea();
        scrollArea->setWidgetResizable(true);
        scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        // Виджет со всем содержимым главного окна
        auto *contentWidget = new QWidget();
        auto *layout = new QVBoxLayout(contentWidget);

        auto *form = new QFormLayout();

        auto *title = new QLabel("VLE Studio");
        title->setStyleSheet(
            "font-size: 24px; font-weight: bold;"
        );

        // Выбор термодинамической модели
        auto *modelCombo = new QComboBox();

        modelCombo->addItem(
            "Peng–Robinson (phi–phi)",
            "PR"
        );

        modelCombo->addItem(
            "UNIFAC + Peng–Robinson (gamma–phi)",
            "UNIFAC_PR"
        );

        // Параметры расчёта одной точки и диаграмм
        auto *temperature = new QDoubleSpinBox();
        temperature->setRange(200.0, 800.0);
        temperature->setDecimals(2);
        temperature->setSuffix(" K");
        temperature->setValue(350.0);

        auto *pressure = new QDoubleSpinBox();
        pressure->setRange(1000.0, 10000000.0);
        pressure->setDecimals(0);
        pressure->setSuffix(" Pa");
        pressure->setValue(101325.0);

        auto *composition = new QDoubleSpinBox();
        composition->setRange(0.001, 0.999);
        composition->setDecimals(3);
        composition->setSingleStep(0.05);
        composition->setValue(0.5);

        // Диапазон температур для T-x-y
        auto *temperatureMin = new QDoubleSpinBox();
        temperatureMin->setRange(200.0, 800.0);
        temperatureMin->setDecimals(2);
        temperatureMin->setSuffix(" K");
        temperatureMin->setValue(300.0);

        auto *temperatureMax = new QDoubleSpinBox();
        temperatureMax->setRange(200.0, 800.0);
        temperatureMax->setDecimals(2);
        temperatureMax->setSuffix(" K");
        temperatureMax->setValue(450.0);

        // Диапазон давлений для P-x-y
        auto *pressureMin = new QDoubleSpinBox();
        pressureMin->setRange(1000.0, 10000000.0);
        pressureMin->setDecimals(0);
        pressureMin->setSuffix(" Pa");
        pressureMin->setValue(10000.0);

        auto *pressureMax = new QDoubleSpinBox();
        pressureMax->setRange(1000.0, 10000000.0);
        pressureMax->setDecimals(0);
        pressureMax->setSuffix(" Pa");
        pressureMax->setValue(250000.0);

        // Количество точек для построения диаграммы
        auto *pointCount = new QSpinBox();
        pointCount->setRange(10, 200);
        pointCount->setValue(30);

        // Выбор бинарной смеси
        auto *mixtureCombo = new QComboBox();
        mixtureCombo->addItem("Этанол – вода", "ETHANOL_WATER");
        mixtureCombo->addItem("Ацетон – хлороформ", "ACETONE_CHLOROFORM");

        // Выбор типа расчёта
        auto *calculationTypeCombo = new QComboBox();
        calculationTypeCombo->addItem("Одна точка равновесия", "POINT");
        calculationTypeCombo->addItem("Диаграмма T–x–y", "TXY");
        calculationTypeCombo->addItem("Диаграмма P–x–y", "PXY");

        // Подписи полей формы
        auto *temperatureLabel = new QLabel("Температура:");
        auto *pressureLabel = new QLabel("Давление:");
        auto *compositionLabel = new QLabel("Мольная доля этанола:");
        auto *temperatureMinLabel = new QLabel("Минимальная температура:");
        auto *temperatureMaxLabel = new QLabel("Максимальная температура:");
        auto *pressureMinLabel = new QLabel("Минимальное давление:");
        auto *pressureMaxLabel = new QLabel("Максимальное давление:");
        auto *pointCountLabel = new QLabel("Количество точек:");

        form->addRow("Бинарная смесь:", mixtureCombo);
        form->addRow("Тип расчёта:", calculationTypeCombo);
        form->addRow("Термодинамическая модель:", modelCombo);
        form->addRow(temperatureLabel, temperature);
        form->addRow(pressureLabel, pressure);
        form->addRow(compositionLabel, composition);
        form->addRow(temperatureMinLabel, temperatureMin);
        form->addRow(temperatureMaxLabel, temperatureMax);
        form->addRow(pressureMinLabel, pressureMin);
        form->addRow(pressureMaxLabel, pressureMax);
        form->addRow(pointCountLabel, pointCount);

        // По умолчанию дополнительные поля диапазонов скрыты.
        temperatureMinLabel->hide();
        temperatureMin->hide();
        temperatureMaxLabel->hide();
        temperatureMax->hide();
        pressureMinLabel->hide();
        pressureMin->hide();
        pressureMaxLabel->hide();
        pressureMax->hide();
        pointCountLabel->hide();
        pointCount->hide();

        // Кнопки
        auto *calculateButton = new QPushButton("Рассчитать");
        auto *saveButton = new QPushButton("Сохранить расчёт");
        auto *exportDiagramButton =
            new QPushButton("Экспорт диаграммы в CSV");
        auto *historyButton = new QPushButton("История расчётов");
        auto *diagramHistoryButton =
            new QPushButton("История диаграмм");

        exportDiagramButton->setEnabled(false);

        // Поле результатов
        auto *resultLabel = new QLabel(
            "Результат расчёта появится здесь."
        );

        resultLabel->setWordWrap(true);
        resultLabel->setTextInteractionFlags(
            Qt::TextSelectableByMouse
        );

        // Область диаграммы
        auto *diagramGroup = new QGroupBox(
            "Диаграмма фазового равновесия"
        );

        auto *diagramLayout = new QVBoxLayout(diagramGroup);

        auto *diagramWidget = new DiagramWidget();

        diagramWidget->setMinimumHeight(350);

        diagramWidget->clearDiagram(
            "Выберите режим T–x–y или P–x–y и нажмите «Рассчитать»."
        );

        diagramLayout->addWidget(diagramWidget);

        // Таблица расчётных точек диаграммы
        auto *tableLabel = new QLabel("Расчётные точки диаграммы");

        auto *resultTable = new QTableWidget();

        resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        resultTable->setAlternatingRowColors(true);
        resultTable->setMinimumHeight(180);

        tableLabel->hide();
        resultTable->hide();

        diagramLayout->addWidget(tableLabel);
        diagramLayout->addWidget(resultTable);

        // Размещение элементов внутри прокручиваемого виджета
        layout->addWidget(title);
        layout->addLayout(form);

        auto *buttonsLayout = new QHBoxLayout();

        buttonsLayout->addWidget(calculateButton);
        buttonsLayout->addWidget(saveButton);
        buttonsLayout->addWidget(exportDiagramButton);

        auto *buttonsLayout2 = new QHBoxLayout();

        buttonsLayout2->addWidget(historyButton);
        buttonsLayout2->addWidget(diagramHistoryButton);
        buttonsLayout2->addStretch();

        layout->addLayout(buttonsLayout);
        layout->addLayout(buttonsLayout2);
        layout->addWidget(resultLabel);
        layout->addWidget(diagramGroup, 1);

        // Последний расчёт
        CalculationResult lastResult;
        bool hasCalculation = false;

        DiagramResult lastDiagram;
        bool hasDiagram = false;

        // Обновление интерфейса при выборе режима и смеси
        auto updateCalculationInterface = [&]() {
            const QString mode =
                calculationTypeCombo->currentData().toString();

            const QString mixture =
                mixtureCombo->currentData().toString();

            const QString firstComponent =
                mixture == "ACETONE_CHLOROFORM" ? "ацетона" : "этанола";

            const QString firstComponentAxis =
                mixture == "ACETONE_CHLOROFORM"
                    ? "Мольная доля ацетона"
                    : "Мольная доля этанола";

            compositionLabel->setText(
                "Мольная доля " + firstComponent + ":"
            );

            auto setFieldVisible = [](QLabel *label, QWidget *field, bool visible) {
                label->setVisible(visible);
                field->setVisible(visible);
            };

            const bool isPoint = mode == "POINT";
            const bool isTxy = mode == "TXY";
            const bool isPxy = mode == "PXY";

            // Точечные параметры
            setFieldVisible(temperatureLabel, temperature, isPoint || isPxy);
            setFieldVisible(pressureLabel, pressure, isPoint || isTxy);
            setFieldVisible(compositionLabel, composition, isPoint);

            // Диапазоны для диаграмм
            setFieldVisible(temperatureMinLabel, temperatureMin, isTxy);
            setFieldVisible(temperatureMaxLabel, temperatureMax, isTxy);
            setFieldVisible(pressureMinLabel, pressureMin, isPxy);
            setFieldVisible(pressureMaxLabel, pressureMax, isPxy);
            setFieldVisible(pointCountLabel, pointCount, !isPoint);

            modelCombo->setEnabled(true);
            calculateButton->setEnabled(true);

            if (isTxy) {
                diagramWidget->setAxisLabels(
                    firstComponentAxis,
                    "Температура, K",
                    temperatureMin->value(),
                    temperatureMax->value()
                );

                diagramWidget->clearDiagram(
                    "Параметры диаграммы T–x–y заданы.\n"
                    "Нажмите «Рассчитать»."
                );
            } else if (isPxy) {
                diagramWidget->setAxisLabels(
                    firstComponentAxis,
                    "Давление, Pa",
                    pressureMin->value(),
                    pressureMax->value()
                );

                diagramWidget->clearDiagram(
                    "Параметры диаграммы P–x–y заданы.\n"
                    "Нажмите «Рассчитать»."
                );
            } else {
                diagramWidget->clearDiagram(
                    "Режим расчёта одной точки равновесия.\n"
                    "Для построения диаграммы выберите T–x–y или P–x–y."
                );
            }

            diagramWidget->setAzeotrope(0.0, 0.0, false);

            tableLabel->setVisible(!isPoint);
            resultTable->setVisible(!isPoint);
            resultTable->clear();
            resultTable->setRowCount(0);
            resultTable->setColumnCount(0);

            exportDiagramButton->setEnabled(false);

            hasCalculation = false;
            hasDiagram = false;

            resultLabel->setText(
                "Результат расчёта появится здесь."
            );
        };

        // Реакция на изменение типа расчёта
        QObject::connect(
            calculationTypeCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            &window,
            [updateCalculationInterface](int) {
                updateCalculationInterface();
            }
        );

        // Реакция на изменение смеси
        QObject::connect(
            mixtureCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            &window,
            [updateCalculationInterface](int) {
                updateCalculationInterface();
            }
        );

        updateCalculationInterface();

        // Обновление только границ осей: рассчитанная диаграмма
        // при этом не сбрасывается.
        auto updateAxisRange = [&]() {
            if (!hasDiagram) {
                return;
            }

            const QString mixture =
                mixtureCombo->currentData().toString();

            const QString axisLabel =
                "Мольная доля " + firstComponentName(mixture);

            if (lastDiagram.kind == "TXY") {
                diagramWidget->setAxisLabels(
                    axisLabel,
                    "Температура, K",
                    temperatureMin->value(),
                    temperatureMax->value()
                );
            } else {
                diagramWidget->setAxisLabels(
                    axisLabel,
                    "Давление, Pa",
                    pressureMin->value(),
                    pressureMax->value()
                );
            }
        };

        // Обновляем границы осей при изменении диапазонов.
        QObject::connect(
            temperatureMin,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            &window,
            [updateAxisRange](double) {
                updateAxisRange();
            }
        );

        QObject::connect(
            temperatureMax,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            &window,
            [updateAxisRange](double) {
                updateAxisRange();
            }
        );

        QObject::connect(
            pressureMin,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            &window,
            [updateAxisRange](double) {
                updateAxisRange();
            }
        );

        QObject::connect(
            pressureMax,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            &window,
            [updateAxisRange](double) {
                updateAxisRange();
            }
        );

        // Кнопка расчёта
        QObject::connect(
            calculateButton,
            &QPushButton::clicked,
            &window,
            [&]() {
                const QString selectedMixture =
                    mixtureCombo->currentData().toString();

                const QString selectedMode =
                    calculationTypeCombo->currentData().toString();

                const QString selectedModel =
                    modelCombo->currentData().toString();

                // Проверяем корректность диапазонов диаграмм.
                if (selectedMode == "TXY"
                    && temperatureMin->value() >= temperatureMax->value()) {
                    QMessageBox::warning(
                        &window,
                        "Некорректный диапазон",
                        "Минимальная температура должна быть меньше максимальной."
                    );
                    return;
                }

                if (selectedMode == "PXY"
                    && pressureMin->value() >= pressureMax->value()) {
                    QMessageBox::warning(
                        &window,
                        "Некорректный диапазон",
                        "Минимальное давление должно быть меньше максимального."
                    );
                    return;
                }

                try {
                    QApplication::setOverrideCursor(Qt::WaitCursor);

                    if (selectedMode == "POINT") {
                    const QString firstComponent =
                        firstComponentName(selectedMixture);
                    const QString secondComponent =
                        secondComponentName(selectedMixture);

                    // Вызываем Python-функцию
                    py::dict result = calc.attr("calculate_point")(
                        temperature->value(),
                        pressure->value(),
                        composition->value(),
                        selectedMixture.toStdString(),
                        selectedModel.toStdString()
                    ).cast<py::dict>();

                    lastResult = CalculationResult{};

                    lastResult.mixture = selectedMixture;
                    lastResult.modelCode = selectedModel;
                    lastResult.model = modelDisplayName(selectedModel);

                    lastResult.temperature =
                        py::float_(result["temperature_K"]).cast<double>();

                    lastResult.pressure =
                        py::float_(result["pressure_Pa"]).cast<double>();

                    lastResult.phaseCount =
                        py::int_(result["phase_count"]).cast<int>();

                    // Исходный состав: Python возвращает список [этанол, вода]
                    py::list feed = result["feed"].cast<py::list>();

                    lastResult.feedEthanol =
                        py::float_(feed[0]).cast<double>();

                    lastResult.feedWater =
                        py::float_(feed[1]).cast<double>();

                    // Состав жидкой фазы
                    py::object liquidObj = result["liquid"];

                    if (!liquidObj.is_none()) {
                        py::list
liquid = liquidObj.cast<py::list>();

                        lastResult.hasLiquid = true;

                        lastResult.liquidEthanol =
                            py::float_(liquid[0]).cast<double>();

                        lastResult.liquidWater =
                            py::float_(liquid[1]).cast<double>();
                    }

                    // Состав паровой фазы
                    py::object vaporObj = result["vapor"];

                    if (!vaporObj.is_none()) {
                        py::list vapor = vaporObj.cast<py::list>();

                        lastResult.hasVapor = true;

                        lastResult.vaporEthanol =
                            py::float_(vapor[0]).cast<double>();

                        lastResult.vaporWater =
                            py::float_(vapor[1]).cast<double>();
                    }

                    // Доля пара
                    py::object vaporFractionObj =
                        result["vapor_fraction"];

                    if (!vaporFractionObj.is_none()) {
                        lastResult.hasVaporFraction = true;

                        lastResult.vaporFraction =
                            py::float_(vaporFractionObj).cast<double>();
                    }

                    hasCalculation = true;

                    // Формируем текст результатов
                    QString output;

                    output +=
                        "Смесь: "
                        + mixtureDisplayName(selectedMixture)
                        + "\n";

                    output +=
                        "Модель: " + lastResult.model + "\n";

                    output +=
                        "Температура: "
                        + QString::number(
                            lastResult.temperature,
                            'f',
                            2
                        )
                        + " K\n";

                    output +=
                        "Давление: "
                        + QString::number(
                            lastResult.pressure,
                            'f',
                            0
                        )
                        + " Pa\n";

                    output +=
                        "Исходный состав: " + firstComponent + " = "
                        + QString::number(
                            lastResult.feedEthanol,
                            'f',
                            4
                        )
                        + ", " + secondComponent + " = "
                        + QString::number(
                            lastResult.feedWater,
                            'f',
                            4
                        )
                        + "\n";

                    output +=
                        "Количество фаз: "
                        + QString::number(lastResult.phaseCount)
                        + "\n";

                    if (lastResult.hasLiquid) {
                        output +=
                            "Жидкая фаза: " + firstComponent + " = "
                            + QString::number(
                                lastResult.liquidEthanol,
                                'f',
                                5
                            )
                            + ", " + secondComponent + " = "
                            + QString::number(
                                lastResult.liquidWater,
                                'f',
                                5
                            )
                            + "\n";
                    }

                    if (lastResult.hasVapor) {
                        output +=
                            "Паровая фаза: " + firstComponent + " = "
                            + QString::number(
                                lastResult.vaporEthanol,
                                'f',
                                5
                            )
                            + ", " + secondComponent + " = "
                            + QString::number(
                                lastResult.vaporWater,
                                'f',
                                5
                            )
                            + "\n";
                    }

                    if
(lastResult.hasVaporFraction) {
                        output +=
                            "Доля пара: "
                            + QString::number(
                                lastResult.vaporFraction,
                                'f',
                                5
                            );
                    }

                    resultLabel->setText(output);

                    hasDiagram = false;
                    exportDiagramButton->setEnabled(false);

                    diagramWidget->clearDiagram(
                        "Расчёт одной точки выполнен.\n"
                        "Для построения T–x–y или P–x–y выберите "
                        "соответствующий режим."
                    );
                    diagramWidget->setAzeotrope(0.0, 0.0, false);

                    resultTable->clear();
                    resultTable->setRowCount(0);
                    resultTable->setColumnCount(0);

                    } else {
                        py::dict result;

                        if (selectedMode == "TXY") {
                            result = calc.attr("calculate_txy")(
                                temperatureMin->value(),
                                temperatureMax->value(),
                                pressure->value(),
                                pointCount->value(),
                                selectedMixture.toStdString(),
                                selectedModel.toStdString()
                            ).cast<py::dict>();
                        } else {
                            result = calc.attr("calculate_pxy")(
                                temperature->value(),
                                pressureMin->value(),
                                pressureMax->value(),
                                pointCount->value(),
                                selectedMixture.toStdString(),
                                selectedModel.toStdString()
                            ).cast<py::dict>();
                        }

                        py::list x1List = result["x1"].cast<py::list>();
                        py::list y1List = result["y1"].cast<py::list>();

                        const char *valuesKey =
                            selectedMode == "TXY"
                                ? "temperature_K"
                                : "pressure_Pa";

                        py::list valuesList =
                            result[valuesKey].cast<py::list>();

                        lastDiagram = DiagramResult{};
                        lastDiagram.kind = selectedMode;
                        lastDiagram.mixture = selectedMixture;
                        lastDiagram.model = selectedModel;

                        if (selectedMode == "TXY") {
                            lastDiagram.pressure = pressure->value();
                        } else {
                            lastDiagram.temperature = temperature->value();
                        }

                        const int count =
                            static_cast<int>(x1List.size());

                        QVector<QPointF> liquidCurve;
                        QVector<QPointF> vaporCurve;

                        for (int index = 0; index < count; ++index) {
                            const double x = py::float_(
                                x1List[index]
                            ).cast<double>();

                            const double y = py::float_(
                                y1List[index]
                            ).cast<double>();

                            const double value = py::float_(
                                valuesList[index]
                            ).cast<double>();

                            lastDiagram.x1 << x;
                            lastDiagram.y1 << y;
                            lastDiagram.values << value;

                            liquidCurve << QPointF(x, value);
                            vaporCurve << QPointF(y, value);
                        }

                        py::object azeotropeObject =
                            result["azeotrope"];

                        if (!azeotropeObject.is_none()) {
                            py::dict azeotrope =
                                azeotropeObject.cast<py::dict>();

                            const char *azeotropeKey =
                                selectedMode == "TXY"
                                    ? "temperature_K"
                                    : "pressure_Pa";

                            lastDiagram.hasAzeotrope = true;

                            lastDiagram.azeotropeX1 = py::float_(
                                azeotrope["x1"]
                            ).cast<double>();

                            lastDiagram.azeotropeValue = py::float_(
                                azeotrope[azeotropeKey]
                            ).cast<double>();
                        }

                        hasDiagram = true;
                        hasCalculation = false;

                        exportDiagramButton->setEnabled(true);

                        const QString firstComponentAxis =
                            "Мольная доля "
                            + firstComponentName(selectedMixture);

                        // Подбираем диапазон оси под расчётные данные,
                        // чтобы кривые не терялись на широком интервале.
                        double dataMin = lastDiagram.values.first();
                        double dataMax = dataMin;

                        for (double value : lastDiagram.values) {
                            dataMin = std::min(dataMin, value);
                            dataMax = std::max(dataMax, value);
                        }

                        const double dataSpan = dataMax - dataMin;
                        const double padding = dataSpan > 0.0
                            ? dataSpan * 0.05
                            : (selectedMode == "TXY" ? 1.0 : 1000.0);

                        if (selectedMode == "TXY") {
                            QSignalBlocker minBlocker(temperatureMin);
                            QSignalBlocker maxBlocker(temperatureMax);

                            temperatureMin->setValue(dataMin - padding);
                            temperatureMax->setValue(dataMax + padding);

                            diagramWidget->setAxisLabels(
                                firstComponentAxis,
                                "Температура, K",
                                temperatureMin->value(),
                                temperatureMax->value()
                            );
                        } else {
                            QSignalBlocker minBlocker(pressureMin);
                            QSignalBlocker maxBlocker(pressureMax);

                            pressureMin->setValue(dataMin - padding);
                            pressureMax->setValue(dataMax + padding);

                            diagramWidget->setAxisLabels(
                                firstComponentAxis,
                                "Давление, Pa",
                                pressureMin->value(),
                                pressureMax->value()
                            );
                        }

                        diagramWidget->setCurves(
                            liquidCurve,
                            vaporCurve
                        );

                        diagramWidget->setAzeotrope(
                            lastDiagram.azeotropeX1,
                            lastDiagram.azeotropeValue,
                            lastDiagram.hasAzeotrope
                        );

                        const QString valueHeader =
                            selectedMode == "TXY"
                                ? "T, K"
                                : "P, Pa";

                        resultTable->clear();
                        resultTable->setColumnCount(3);
                        resultTable->setHorizontalHeaderLabels({
                            "x1 (жидкость)",
                            "y1 (пар)",
                            valueHeader
                        });
                        resultTable->setRowCount(count);

                        for (int index = 0; index < count; ++index) {
                            resultTable->setItem(
                                index,
                                0,
                                new QTableWidgetItem(
                                    QString::number(
                                        lastDiagram.x1[index],
                                        'f',
                                        5
                                    )
                                )
                            );

                            resultTable->setItem(
                                index,
                                1,
                                new QTableWidgetItem(
                                    QString::number(
                                        lastDiagram.y1[index],
                                        'f',
                                        5
                                    )
                                )
                            );

                            resultTable->setItem(
                                index,
                                2,
                                new QTableWidgetItem(
                                    QString::number(
                                        lastDiagram.values[index],
                                        'f',
                                        selectedMode == "TXY" ? 3 : 1
                                    )
                                )
                            );
                        }

                        resultTable->horizontalHeader()
                            ->setSectionResizeMode(
                                QHeaderView::Stretch
                            );

                        QString output;

                        output += "Смесь: "
                            + mixtureDisplayName(selectedMixture)
                            + "\n";

                        output += "Модель: "
                            + modelDisplayName(selectedModel)
                            + "\n";

                        output += "Диаграмма: "
                            + diagramKindName(selectedMode)
                            + "\n";

                        if (selectedMode == "TXY") {
                            output += "Давление: "
                                + QString::number(
                                    lastDiagram.pressure,
                                    'f',
                                    0
                                )
                                + " Pa\n";
                        } else {
                            output += "Температура: "
                                + QString::number(
                                    lastDiagram.temperature,
                                    'f',
                                    2
                                )
                                + " K\n";
                        }

                        output += "Точек: "
                            + QString::number(count)
                            + "\n";

                        if (lastDiagram.hasAzeotrope) {
                            output += "Азеотроп: x1 = "
                                + QString::number(
                                    lastDiagram.azeotropeX1,
                                    'f',
                                    5
                                );

                            if (selectedMode == "TXY") {
                                output += ", T = "
                                    + QString::number(
                                        lastDiagram.azeotropeValue,
                                        'f',
                                        3
                                    )
                                    + " K";
                            } else {
                                output += ", P = "
                                    + QString::number(
                                        lastDiagram.azeotropeValue,
                                        'f',
                                        0
                                    )
                                    + " Pa";
                            }
                        } else {
                            output += "Азеотропная точка не найдена.";
                        }

                        resultLabel->setText(output);
                    }

                    QApplication::restoreOverrideCursor();

                } catch (const py::error_already_set &error) {
                    QApplication::restoreOverrideCursor();
                    hasCalculation = false;
                    hasDiagram = false;
                    exportDiagramButton->setEnabled(false);

                    QMessageBox::critical(
                        &window,
                        "Ошибка Python",
                        QString::fromStdString(error.what())
                    );

                } catch (const std::exception &error) {
                    QApplication::restoreOverrideCursor();
                    hasCalculation = false;
                    hasDiagram = false;
                    exportDiagramButton->setEnabled(false);

                    QMessageBox::critical(
                        &window,
                        "Ошибка расчёта",
                        QString::fromStdString(error.what())
                    );
                }
            }
        );

        // Кнопка сохранения расчёта
        QObject::connect(
            saveButton,
            &QPushButton::clicked,
            &window,
            [&]() {
                if (hasDiagram) {
                    QString errorMessage;

                    if (!saveDiagram(db, lastDiagram, errorMessage)) {
                        QMessageBox::critical(
                            &window,
                            "Ошибка сохранения",
                            errorMessage
                        );
                        return;
                    }

                    QMessageBox::information(
                        &window,
                        "Сохранение",
                        "Диаграмма успешно сохранена в истории диаграмм."
                    );
                    return;
                }

                if (!hasCalculation) {
                    QMessageBox::warning(
                        &window,
                        "Нет результата",
                        "Сначала выполните расчёт."
                    );
                    return;
                }

                QString errorMessage;

                if (!saveCalculation(db, lastResult, errorMessage)) {
                    QMessageBox::critical(
                        &window,
                        "Ошибка сохранения",
                        errorMessage
                    );
                    return;
                }

                QMessageBox::information(
                    &window,
                    "Сохранение",
                    "Расчёт успешно сохранён в истории."
                );
            }
        );

        // Кнопка экспорта текущей диаграммы
        QObject::connect(
            exportDiagramButton,
            &QPushButton::clicked,
            &window,
            [&]() {
                if (!hasDiagram) {
                    QMessageBox::warning(
                        &window,
                        "Нет диаграммы",
                        "Сначала выполните расчёт диаграммы."
                    );
                    return;
                }

                exportDiagramToCsv(lastDiagram, &window);
            }
        );

        // Кнопка просмотра истории расчётов
        QObject::connect(
            historyButton,
            &QPushButton::clicked,
            &window,
            [&]() {
                showHistory(db, &window);
            }
        );

        // Кнопка просмотра истории диаграмм
        QObject::connect(
            diagramHistoryButton,
            &QPushButton::clicked,
            &window,
            [&]() {
                showDiagramHistory(db, &window);
            }
        );

        // Подключаем содержимое к области прокрутки
        scrollArea->setWidget(contentWidget);

        // Размещаем область прокрутки в главном окне
        auto *windowLayout = new QVBoxLayout(&window);

        windowLayout->setContentsMargins(0, 0, 0, 0);
        windowLayout->addWidget(scrollArea);

        // Показываем окно
        window.show();

        return app.exec();
    }
    catch (const std::exception &error) {
        QMessageBox::critical(
            nullptr,
            "Критическая ошибка",
            QString::fromStdString(error.what())
        );

        return 1;
    }
}
