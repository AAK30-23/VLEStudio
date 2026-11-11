#ifndef DIAGRAM_WIDGET_H
#define DIAGRAM_WIDGET_H

#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QString>

class DiagramWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DiagramWidget(QWidget *parent = nullptr);

    // Устанавливает подписи и диапазон осей
    void setAxisLabels(
        const QString &xLabel,
        const QString &yLabel,
        double yMin,
        double yMax
    );

    // Передаёт реальные расчётные точки
    // x — мольная доля компонента (0...1)
    // y — температура или давление
    void setCurves(
        const QVector<QPointF> &liquidCurve,
        const QVector<QPointF> &vaporCurve
    );

    // Очищает график
    void clearDiagram(const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString xAxisLabel;
    QString yAxisLabel;
    QString emptyMessage;

    double yMinimum;
    double yMaximum;

    QVector<QPointF> liquidPoints;
    QVector<QPointF> vaporPoints;

    bool hasData;
};

#endif
