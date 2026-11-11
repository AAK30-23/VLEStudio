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

#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include <exception>
#include <vector>

namespace py = pybind11;

// Результат расчёта
struct CalculationResult {
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
            vapor_fraction
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

    QStringList lines;

    lines << "ID расчёта: " + query.value(0).toString();
    lines << "Дата: " + query.value(1).toString();
    lines << "Модель: " + query.value(2).toString();

    lines << "";
    lines << "Параметры расчёта:";

    lines << "Температура: "
        + displayDatabaseValue(query.value(3), 2) + " K";

    lines << "Давление: "
        + displayDatabaseValue(query.value(4), 0) + " Pa";

    lines << "";
    lines << "Исходный состав:";

    lines << "Этанол: "
        + displayDatabaseValue(query.value(5));

    lines << "Вода: "
        + displayDatabaseValue(query.value(6));

    lines << "";
    lines << "Результаты:";

    lines << "Количество фаз: "
        + query.value(7).toString();

    lines << "";

    if (!query.value(8).isNull()) {
        lines << "Жидкая фаза:";
        lines << "Этанол: "
            + displayDatabaseValue(query.value(8));
        lines << "Вода: "
            + displayDatabaseValue(query.value(9));
    } else {
        lines << "Жидкая фаза отсутствует.";
    }

    lines << "";

    if (!query.value(10).isNull()) {
        lines << "Паровая фаза:";
        lines << "Этанол: "
            + displayDatabaseValue(query.value(10));
        lines << "Вода: "
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
            vapor_fraction
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
        "Модель",
        "Температура, K",
        "Давление, Pa",
        "Этанол (исходный)",
        "Вода (исходная)",
        "Количество фаз",
        "Этанол (жидкость)",
        "Вода (жидкость)",
        "Этанол (пар)",
        "Вода (пар)",
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

        for (int column = 0; column < 13; ++column) {
            if (query.value(column).isNull()) {
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
        "Модель",
        "T, K",
        "P, Pa",
        "Этанол (исх.)",
        "Вода (исх.)",
        "Фазы",
        "Этанол (жидк.)",
        "Вода (жидк.)",
        "Этанол (пар)",
        "Вода (пар)",
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

            if (value.isNull()) {
                text = "—";
            } else {
                text = value.toString();
            }

            bool numeric =
                column == 0 || (column >= 3 && column <= 12);

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
        pressureMax->setValue(1000000.0);

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
        auto *historyButton = new QPushButton("История расчётов");

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
            "Выберите режим T–x–y или P–x–y.\n"
            "Расчётные данные появятся после подключения расчётной функции."
        );

        diagramLayout->addWidget(diagramWidget);

        // Размещение элементов внутри прокручиваемого виджета
        layout->addWidget(title);
        layout->addLayout(form);

        auto *buttonsLayout = new QHBoxLayout();

        buttonsLayout->addWidget(calculateButton);
        buttonsLayout->addWidget(saveButton);
        buttonsLayout->addWidget(historyButton);

        layout->addLayout(buttonsLayout);
        layout->addWidget(resultLabel);
        layout->addWidget(diagramGroup, 1);

        // Последний расчёт
        CalculationResult lastResult;
        bool hasCalculation = false;

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

            if (isTxy) {
                diagramWidget->setAxisLabels(
                    firstComponentAxis,
                    "Температура, K",
                    temperatureMin->value(),
                    temperatureMax->value()
                );

                diagramWidget->clearDiagram(
                    "Параметры диаграммы T–x–y заданы.\n"
                    "Расчёт будет доступен после подключения функции напарника."
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
                    "Расчёт будет доступен после подключения функции напарника."
                );
            } else {
                diagramWidget->clearDiagram(
                    "Режим расчёта одной точки равновесия.\n"
                    "Для построения диаграммы выберите T–x–y или P–x–y."
                );
            }

            const bool supportedMixture =
                mixture == "ETHANOL_WATER";

            modelCombo->setEnabled(supportedMixture);
            calculateButton->setEnabled(supportedMixture);

            if (!supportedMixture) {
                diagramWidget->clearDiagram(
                    "Расчёт смеси ацетон – хлороформ пока не подключён."
                );

                resultLabel->setText(
                    "Для выбранной смеси расчётный модуль пока не реализован."
                );
            } else {
                resultLabel->setText(
                    "Результат расчёта появится здесь."
                );
            }

            hasCalculation = false;
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

        // Обновляем границы осей при изменении диапазонов.
        QObject::connect(
            temperatureMin,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            &window,
            [updateCalculationInterface](double) {
                updateCalculationInterface();
            }
        );

        QObject::connect(
            temperatureMax,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            &window,
            [updateCalculationInterface](double) {
                updateCalculationInterface();
            }
        );

        QObject::connect(
            pressureMin,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            &window,
            [updateCalculationInterface](double) {
                updateCalculationInterface();
            }
        );

        QObject::connect(
            pressureMax,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            &window,
            [updateCalculationInterface](double) {
                updateCalculationInterface();
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

                // Проверяем выбранную смесь
                if (selectedMixture != "ETHANOL_WATER") {
                    QMessageBox::information(
                        &window,
                        "Расчёт пока недоступен",
                        "Расчёт смеси ацетон – хлороформ пока не подключён."
                    );
                    return;
                }

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

                // Пока поддерживается только расчёт одной точки.
                // Поля диапазонов и число точек уже подготовлены для функций напарника.
                if (selectedMode != "POINT") {
                    QMessageBox::information(
                        &window,
                        "Расчёт диаграммы",
                        "Параметры диапазона и количество точек заданы в форме. "
                        "Расчёт диаграмм будет подключён после готовности функций напарника."
                    );
                    return;
                }

                // Проверяем выбранную модель
                if (modelCombo->currentData().toString() == "UNIFAC_PR") {
                    QMessageBox::information(
                        &window,
                        "Модель пока недоступна",
                        "Расчёт UNIFAC + Peng–Robinson пока не подключён к интерфейсу."
                    );
                    return;
                }

                try {
                    // Вызываем Python-функцию
                    py::dict result = calc.attr("calculate_pr")(
                        temperature->value(),
                        pressure->value(),
                        composition->value()
                    ).cast<py::dict>();

                    lastResult = CalculationResult{};

                    lastResult.model = "Peng–Robinson (phi–phi)";

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
                        "Модель: Peng–Robinson (phi–phi)\n";

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
                        "Исходный состав: этанол = "
                        + QString::number(
                            lastResult.feedEthanol,
                            'f',
                            4
                        )
                        + ", вода = "
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
                            "Жидкая фаза: этанол = "
                            + QString::number(
                                lastResult.liquidEthanol,
                                'f',
                                5
                            )
                            + ", вода = "
                            + QString::number(
                                lastResult.liquidWater,
                                'f',
                                5
                            )
                            + "\n";
                    }

                    if (lastResult.hasVapor) {
                        output +=
                            "Паровая фаза: этанол = "
                            + QString::number(
                                lastResult.vaporEthanol,
                                'f',
                                5
                            )
                            + ", вода = "
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

                    // Для одной точки полноценная диаграмма не строится
                    diagramWidget->clearDiagram(
                        "Расчёт одной точки выполнен.\n"
                        "Для построения T–x–y или P–x–y нужны массивы равновесных точек."
                    );

                } catch (const py::error_already_set &error) {
                    hasCalculation = false;

                    QMessageBox::critical(
                        &window,
                        "Ошибка Python",
                        QString::fromStdString(error.what())
                    );

                } catch (const std::exception &error) {
                    hasCalculation = false;

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

        // Кнопка просмотра истории
        QObject::connect(
            historyButton,
            &QPushButton::clicked,
            &window,
            [&]() {
                showHistory(db, &window);
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
