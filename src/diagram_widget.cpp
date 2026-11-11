#include "diagram_widget.h"
#include <QPainterPath>
#include <QPainter>
#include <QPaintEvent>
#include <QFont>
#include <QRectF>
#include <algorithm>

DiagramWidget::DiagramWidget(QWidget *parent)
    : QWidget(parent),
      xAxisLabel("Мольная доля компонента"),
      yAxisLabel("Параметр равновесия"),
      emptyMessage("Расчётные данные пока не загружены"),
      yMinimum(0.0),
      yMaximum(1.0),
      hasData(false)
{
    setMinimumSize(550, 350);
    setAutoFillBackground(true);
}

void DiagramWidget::setAxisLabels(
    const QString &xLabel,
    const QString &yLabel,
    double yMin,
    double yMax
)
{
    xAxisLabel = xLabel;
    yAxisLabel = yLabel;

    yMinimum = yMin;
    yMaximum = yMax;

    update();
}

void DiagramWidget::setCurves(
    const QVector<QPointF> &liquidCurve,
    const QVector<QPointF> &vaporCurve
)
{
    liquidPoints = liquidCurve;
    vaporPoints = vaporCurve;

    hasData = !liquidPoints.isEmpty()
              || !vaporPoints.isEmpty();

    emptyMessage = hasData
        ? ""
        : "Нет расчётных точек";

    update();
}

void DiagramWidget::clearDiagram(const QString &message)
{
    liquidPoints.clear();
    vaporPoints.clear();

    hasData = false;
    emptyMessage = message;

    update();
}

void DiagramWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing);

    painter.fillRect(rect(), Qt::white);

    // Область построения графика
    const int left = 85;
    const int right = 30;
    const int top = 35;
    const int bottom = 75;

    QRectF plotArea(
        left,
        top,
        width() - left - right,
        height() - top - bottom
    );

    if (plotArea.width() <= 0 || plotArea.height() <= 0) {
        return;
    }

    // Заголовок
    painter.setPen(QColor(35, 35, 35));

    QFont titleFont = painter.font();
    titleFont.setPointSize(12);
    titleFont.setBold(true);

    painter.setFont(titleFont);

    painter.drawText(
        QRect(0, 5, width(), 25),
        Qt::AlignCenter,
        "Диаграмма фазового равновесия"
    );

    // Сетка
    painter.setFont(QFont("Arial", 9));
    painter.setPen(QPen(QColor(220, 220, 220), 1));

    for (int i = 0; i <= 5; ++i) {
        double ratio = i / 5.0;

        double x = plotArea.left()
                   + ratio * plotArea.width();

        double y = plotArea.bottom()
                   - ratio * plotArea.height();

        painter.drawLine(
            QPointF(x, plotArea.top()),
            QPointF(x, plotArea.bottom())
        );

        painter.drawLine(
            QPointF(plotArea.left(), y),
            QPointF(plotArea.right(), y)
        );

        // Подписи оси X
        painter.setPen(Qt::black);

        painter.drawText(
            QRectF(x - 20, plotArea.bottom() + 8, 40, 20),
            Qt::AlignCenter,
            QString::number(ratio, 'f', 1)
        );

        // Подписи оси Y
        double yValue = yMinimum
                        + ratio * (yMaximum - yMinimum);

        painter.drawText(
            QRectF(5, y - 10, left - 15, 20),
            Qt::AlignRight | Qt::AlignVCenter,
            QString::number(yValue, 'f', 1)
        );

        painter.setPen(QPen(QColor(220, 220, 220), 1));
    }

    // Оси
    painter.setPen(QPen(Qt::black, 1.5));

    painter.drawLine(
        plotArea.bottomLeft(),
        plotArea.bottomRight()
    );

    painter.drawLine(
        plotArea.bottomLeft(),
        plotArea.topLeft()
    );

    // Подпись оси X
    painter.drawText(
        QRectF(
            plotArea.left(),
            height() - 35,
            plotArea.width(),
            25
        ),
        Qt::AlignCenter,
        xAxisLabel
    );

    // Подпись оси Y
    painter.save();

    painter.translate(20, plotArea.center().y());
    painter.rotate(-90);

    painter.drawText(
        QRectF(
            -plotArea.height() / 2,
            -12,
            plotArea.height(),
            24
        ),
        Qt::AlignCenter,
        yAxisLabel
    );

    painter.restore();

    // Если расчётных данных нет,показываем сообщение
    if (!hasData) {
        painter.setPen(QColor(110, 110, 110));
        painter.setFont(QFont("Arial", 11));

        painter.drawText(
            plotArea,
            Qt::AlignCenter,
            emptyMessage
        );

        return;
    }

    // Преобразование реальных координат в координаты графика
    auto transformPoint = [&](const QPointF &point) {
        double x = plotArea.left()
                   + point.x() * plotArea.width();

        double normalizedY = 0.0;

        if (yMaximum != yMinimum) {
            normalizedY =
                (point.y() - yMinimum)
                / (yMaximum - yMinimum);
        }

        double y = plotArea.bottom()
                   - normalizedY * plotArea.height();

        return QPointF(x, y);
    };

    // Построение кривых
    auto drawCurve = [&](const QVector<QPointF> &points,
                         const QColor &color) {
        if (points.size() < 2) {
            return;
        }

        QPainterPath path;

        path.moveTo(transformPoint(points.first()));

        for (int i = 1; i < points.size(); ++i) {
            path.lineTo(transformPoint(points[i]));
        }

        painter.setPen(QPen(color, 2.5));
        painter.drawPath(path);

        // Отображение расчётных точек
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);

        for (const QPointF &point : points) {
            painter.drawEllipse(
                transformPoint(point),
                3.5,
                3.5
            );
        }
    };

    drawCurve(liquidPoints, QColor(40, 110, 210));
    drawCurve(vaporPoints, QColor(220, 90, 50));

    // Легенда
    int legendX = plotArea.left() + 15;
    int legendY = plotArea.top() + 15;

    painter.setFont(QFont("Arial", 9));

    painter.setPen(QPen(QColor(40, 110, 210), 2.5));
    painter.drawLine(legendX, legendY, legendX + 25, legendY);

    painter.setPen(Qt::black);
    painter.drawText(legendX + 32, legendY + 5, "Жидкая фаза");

    painter.setPen(QPen(QColor(220, 90, 50), 2.5));
    painter.drawLine(
        legendX,
        legendY + 22,
        legendX + 25,
        legendY + 22
    );

    painter.setPen(Qt::black);
    painter.drawText(
        legendX + 32,
        legendY + 27,
        "Паровая фаза"
    );
}
