#pragma once

#include <QWidget>
#include <QVector>
#include "Sketch2DView.h"

class QSplitter;
class QSlider;
class QLabel;

class CylindricalOffsetViews : public QWidget {
    Q_OBJECT

public:
    explicit CylindricalOffsetViews(QWidget* parent = nullptr);

    // 设置各视图的偏置结果
    void setOffsetBoundaryResults(
        const QVector<Sketch2DView::OffsetResultPolygon>& polygons);
    void setBooleanResults(
        const QVector<Sketch2DView::OffsetResultPolygon>& polygons);
    void setFinalResults(
        const QVector<Sketch2DView::OffsetResultPolygon>& polygons);

    // 同步边界线
    void setBoundaryLines(float left, float right);

    // 获取/设置偏置距离
    double offsetDistance() const;
    void setOffsetDistance(double dist);

    // 清空所有结果
    void clear();
    void clearResults();

signals:
    // 偏置距离变更（用户拖动滑块）
    void offsetDistanceChanged(double distance);

private:
    void setupViews();

    Sketch2DView* m_offsetBoundaryView = nullptr;
    Sketch2DView* m_booleanResultView = nullptr;
    Sketch2DView* m_finalResultView = nullptr;

    QSplitter* m_horizSplitter = nullptr;
    QSplitter* m_vertSplitter = nullptr;
    QSlider* m_offsetSlider = nullptr;
    QLabel* m_offsetValueLabel = nullptr;
};
