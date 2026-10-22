#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QMessageBox>
#include <QStringList>

#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include <exception>
#include <vector>

namespace py = pybind11;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    try {
        // Python должен работать всё время, пока открыто окно
        py::scoped_interpreter guard{};

        py::module_ sys = py::module_::import("sys");
        sys.attr("path").attr("insert")(0, VLE_SOURCE_DIR "/python");
        sys.attr("path").attr("insert")(0, VLE_PYTHON_SITE_PACKAGES);

        py::module_ calc = py::module_::import("vle_calc");

        QWidget window;
        window.setWindowTitle("VLE Studio — фазовое равновесие");
        window.resize(650, 480);

        auto *layout = new QVBoxLayout(&window);
        auto *form = new QFormLayout();

        auto *title = new QLabel("VLE Studio");
        title->setStyleSheet(
            "font-size: 24px; font-weight: bold;"
        );

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

        form->addRow("Температура:", temperature);
        form->addRow("Давление:", pressure);
        form->addRow("Мольная доля этанола:", composition);

        auto *button = new QPushButton("Рассчитать равновесие");
        auto *resultLabel = new QLabel(
            "Задайте параметры и нажмите «Рассчитать равновесие»."
        );
        resultLabel->setWordWrap(true);
        resultLabel->setTextInteractionFlags(
            Qt::TextSelectableByMouse
        );

        layout->addWidget(title);
        layout->addLayout(form);
        layout->addWidget(button);
        layout->addWidget(resultLabel);
        layout->addStretch();

        QObject::connect(
            button,
            &QPushButton::clicked,
            [&]() {
                try {
                    py::dict result = calc.attr("calculate_pr")(
                        temperature->value(),
                        pressure->value(),
                        composition->value()
                    ).cast<py::dict>();

                    auto feed =
                        py::cast<std::vector<double>>(result["feed"]);

                    auto liquid =
                        py::cast<std::vector<double>>(result["liquid"]);

                    auto vapor =
                        py::cast<std::vector<double>>(result["vapor"]);

                    QStringList lines;
                    lines << "Модель: Peng–Robinson (phi-phi)";
                    lines << QString("Температура: %1 K")
                        .arg(py::cast<double>(result["temperature_K"]), 0, 'f', 2);
                    lines << QString("Давление: %1 Pa")
                        .arg(py::cast<double>(result["pressure_Pa"]), 0, 'f', 0);
                    lines << QString("Количество фаз: %1")
                        .arg(py::cast<int>(result["phase_count"]));
                    lines << "";
                    lines << QString("Исходный состав: этанол %1, вода %2")
                        .arg(feed[0], 0, 'f', 5)
                        .arg(feed[1], 0, 'f', 5);
                    lines << QString("Жидкость: этанол %1, вода %2")
                        .arg(liquid[0], 0, 'f', 5)
                        .arg(liquid[1], 0, 'f', 5);
                    lines << QString("Пар: этанол %1, вода %2")
.arg(vapor[0], 0, 'f', 5)
                        .arg(vapor[1], 0, 'f', 5);
                    lines << QString("Доля пара: %1")
                        .arg(py::cast<double>(result["vapor_fraction"]), 0, 'f', 5);

                    resultLabel->setText(lines.join("\n"));
                }
                catch (const std::exception &e) {
                    QMessageBox::critical(
                        &window,
                        "Ошибка расчёта",
                        QString::fromUtf8(e.what())
                    );
                }
            }
        );

        window.show();
        return app.exec();
    }
    catch (const std::exception &e) {
        QMessageBox::critical(
            nullptr,
            "Ошибка запуска",
            QString::fromUtf8(e.what())
        );
        return 1;
    }
}
