#pragma once

#include <QWidget>
#include <QSplitter>
#include <QFrame>
#include <QLabel>
#include <QVector>
#include "Sketch2DView.h"

/**
 * @brief 周期裁剪视图容器：包含两个只读视图
 * 
 * 左侧（View 6）：去自交规范化后的多边形（周期裁剪前）
 * 右侧（View 7）：周期裁剪后的多边形（已平移到圆柱带内）
 */
class PeriodicClippingViews : public QWidget {
    Q_OBJECT

public:
    explicit PeriodicClippingViews(QWidget* parent = nullptr);

    Sketch2DView* beforeView() const { return m_beforeView; }
    Sketch2DView* afterView() const { return m_afterView; }

    /// 设置裁剪前的规范化多边形（左侧视图）
    void setBeforePolygons(const QVector<Sketch2DView::OffsetResultPolygon>& polygons);
    /// 设置裁剪后的周期多边形（右侧视图）
    void setAfterPolygons(const QVector<Sketch2DView::OffsetResultPolygon>& polygons);

    /// 同步边界线到两个子视图
    void setBoundaryLines(float left, float right);

    /// 清除两个视图的内容
    void clear();

private:
    void setupViews();

    Sketch2DView* m_beforeView = nullptr;
    Sketch2DView* m_afterView = nullptr;
    QSplitter* m_splitter = nullptr;
};
