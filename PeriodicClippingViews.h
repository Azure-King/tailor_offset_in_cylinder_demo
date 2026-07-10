#pragma once

#include <QWidget>
#include <QSplitter>
#include <QFrame>
#include <QLabel>
#include <QVector>
#include "Sketch2DView.h"

/**
 * @brief 周期裁剪视图容器：包含三个只读视图
 * 
 * 左侧（View 6）：去自交规范化后的多边形（周期裁剪前各分量）
 * 中间（View 7）：周期裁剪后的多边形（简单布尔并集）
 * 右侧（View 8）：圆柱区域合并结果（BuildCylindricalAreas + 展平）
 */
class PeriodicClippingViews : public QWidget {
    Q_OBJECT

public:
    explicit PeriodicClippingViews(QWidget* parent = nullptr);

    Sketch2DView* beforeView() const { return m_beforeView; }
    Sketch2DView* afterView() const { return m_afterView; }
    Sketch2DView* mergedView() const { return m_mergedView; }

    /// 设置裁剪前的规范化多边形（左侧视图 6）
    void setBeforePolygons(const QVector<Sketch2DView::OffsetResultPolygon>& polygons);
    /// 设置裁剪后的简单并集多边形（中间视图 7）
    void setAfterPolygons(const QVector<Sketch2DView::OffsetResultPolygon>& polygons);
    /// 设置圆柱区域合并结果（右侧视图 8）
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
    QSplitter* m_vertSplitter = nullptr;
    QSplitter* m_horizSplitter = nullptr;
};
