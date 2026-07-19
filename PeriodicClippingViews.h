#pragma once

#include <QWidget>
#include <QFrame>
#include <QLabel>
#include <QVector>
#include "Sketch2DView.h"

/**
 * @brief 周期裁剪视图容器：2x2 网格布局，右下角为3D圆柱视图
 * 
 * 左上（View 6）：去自交规范化后的多边形（周期裁剪前各分量）
 * 右上（View 7）：周期裁剪后的多边形（简单布尔并集）
 * 左下（View 8）：圆柱区域合并结果（BuildCylindricalAreas + 展平）
 * 右下：3D圆柱视图（外部注入，通过 setCylinderView）
 */
class PeriodicClippingViews : public QWidget {
    Q_OBJECT

public:
    explicit PeriodicClippingViews(QWidget* parent = nullptr);

    Sketch2DView* beforeView() const { return m_beforeView; }
    Sketch2DView* afterView() const { return m_afterView; }
    Sketch2DView* mergedView() const { return m_mergedView; }

    /// 获取右下角 3D 视图占位容器（用于 reparent 操作）
    QWidget* cylinderPlaceholder() const { return m_cylinderPlaceholder; }
    /// 将 3D 圆柱视图放入右下角占位区
    void setCylinderView(QWidget* view);

    /// 设置裁剪前的规范化多边形（左上视图 6）
    void setBeforePolygons(const QVector<Sketch2DView::OffsetResultPolygon>& polygons);
    /// 设置裁剪后的简单并集多边形（右上视图 7）
    void setAfterPolygons(const QVector<Sketch2DView::OffsetResultPolygon>& polygons);
    /// 设置圆柱区域合并结果（左下视图 8）
    void setMergedPolygons(const QVector<Sketch2DView::OffsetResultPolygon>& polygons);

    /// 同步边界线到所有子视图
    void setBoundaryLines(float left, float right);

    /// 清除所有视图的内容
    void clear();

private:
    void setupViews();

    Sketch2DView* m_beforeView = nullptr;
    Sketch2DView* m_afterView = nullptr;
    Sketch2DView* m_mergedView = nullptr;
    QWidget* m_cylinderPlaceholder = nullptr;  // 右下角3D视图占位容器
};
