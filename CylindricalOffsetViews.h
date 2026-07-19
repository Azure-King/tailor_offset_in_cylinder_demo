#pragma once

#include <QWidget>
#include <QVector>
#include "Sketch2DView.h"

class QSlider;
class QLabel;

/**
 * @brief 圆柱偏置视图容器：2x2 网格布局，右下角为3D圆柱视图
 *
 * 左上（View 9）：偏置边界
 * 右上（View 10）：布尔运算结果
 * 左下（View 11）：最终偏置区域（含滑块）
 * 右下：3D圆柱视图（外部注入，通过 setCylinderView）
 */
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

    /// 获取右下角 3D 视图占位容器（用于 reparent 操作）
    QWidget* cylinderPlaceholder() const { return m_cylinderPlaceholder; }
    /// 将 3D 圆柱视图放入右下角占位区
    void setCylinderView(QWidget* view);

signals:
    // 偏置距离变更（用户拖动滑块）
    void offsetDistanceChanged(double distance);

private:
    void setupViews();

    Sketch2DView* m_offsetBoundaryView = nullptr;
    Sketch2DView* m_booleanResultView = nullptr;
    Sketch2DView* m_finalResultView = nullptr;

    QSlider* m_offsetSlider = nullptr;
    QLabel* m_offsetValueLabel = nullptr;
    QWidget* m_cylinderPlaceholder = nullptr;  // 右下角3D视图占位容器
};
