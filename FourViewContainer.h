#pragma once

#include <QWidget>
#include <QGridLayout>
#include <QFrame>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QSlider>
#include <QTreeWidget>
#include <QHash>
#include <vector>
#include "BooleanOperations.h"
#include "CylindricalRegion.h"
#include "Sketch2DView.h"

class FourViewContainer : public QWidget {
    Q_OBJECT

public:
    explicit FourViewContainer(QWidget* parent = nullptr);
    ~FourViewContainer();

    // Main view (top-left) - full functionality
    Sketch2DView* mainView() const { return m_mainView; }

    // Secondary views - view only (pan/zoom only)
    Sketch2DView* topRightView() const { return m_topRightView; }
    Sketch2DView* bottomLeftView() const { return m_bottomLeftView; }
    Sketch2DView* bottomRightView() const { return m_bottomRightView; }

    // Synchronize view state from main view to all views
    void synchronizeViews();

    /// 用圆柱区域树填充 QTreeWidget
    void buildRegionTree(QTreeWidget* tree) const;

    /// 获取圆柱区域树（只读）
    const std::vector<tailor_visualization::CylindricalArea>& cylindricalAreaTree() const {
        return m_cylindricalAreaTree;
    }

signals:
    // 预留信号：曲线偏置操作触发
    void offsetOperationTriggered(double distance);
    void pipelineStepChanged(int step);
    // 边界线变更：传递左右边界位置（同时带出宽度/半径信息）
    void boundariesUpdated(float left, float right);
    // 周期裁剪结果：裁剪前规范化多边形 + 裁剪后周期多边形
    void periodicClipResultReady(
        const QVector<Sketch2DView::OffsetResultPolygon>& before,
        const QVector<Sketch2DView::OffsetResultPolygon>& after);
    // 圆柱区域合并结果（View 8 + 3D圆柱贴面）
    void periodicCylindricalResultReady(
        const QVector<Sketch2DView::OffsetResultPolygon>& cylindrical);
    // 区域树已构建（用于左侧面板刷新区域结果树）
    void regionTreeUpdated();

public slots:
    /// 刷新周期裁剪（用于边界线变更后重新裁剪）
    void refreshPeriodicClip();

private slots:
    void runFullPipeline();

private:
    void setupViews();
    void setupLayout();
    void processSelfIntersection();
    void processCurveOffset(double distance);
    void processDeselfIntersection();
    void processPreserveGlobalSelfIntersection();
    void processPeriodicClip();    // 周期裁剪步骤

    Sketch2DView* m_mainView = nullptr;
    Sketch2DView* m_topRightView = nullptr;
    Sketch2DView* m_bottomLeftView = nullptr;
    Sketch2DView* m_bottomRightView = nullptr;

    // 流水线中间数据：存储弧段结果以便在步骤间传递
    std::vector<std::vector<tailor_visualization::Arc>> m_mergedFillArcs;   // 自交处理+合并后的弧段
    std::vector<std::vector<tailor_visualization::Arc>> m_mergedOffsetArcs; // 偏置+合并后的弧段
    std::vector<bool> m_mergedFillIsHole;    // 与 m_mergedFillArcs 一一对应，标记是否为内环（depth%2==1）
    std::vector<bool> m_mergedOffsetIsHole;  // 与 m_mergedOffsetArcs 一一对应，标记是否为内环

    // 本地 segmentId → 原始输入边 ID 的映射，用于第一视图高亮溯源
    QHash<int, int> m_localToOriginalSegId;

    // 流水线控件
    QComboBox* m_fillTypeCombo = nullptr;
    QComboBox* m_connectTypeCombo = nullptr;
    QComboBox* m_joinConvexStyleCombo = nullptr;
    QSlider* m_offsetDistanceSlider = nullptr;
    QLabel* m_offsetValueLabel = nullptr;
    QCheckBox* m_consoleTimeCheck = nullptr;
    QCheckBox* m_consolePolygonCheck = nullptr;
    QPushButton* m_preserveGlobalBtn = nullptr;

    QGridLayout* m_layout = nullptr;

    // 圆柱区域树（周期裁剪步骤构建，供左侧模型树展示）
    std::vector<tailor_visualization::CylindricalArea> m_cylindricalAreaTree;

    // 区域 → cylindricalResults 高亮索引映射（树控件选中时高亮对应的渲染项）
    // key = 区域对象的地址（m_cylindricalAreaTree 中的稳定地址）
    std::map<const tailor_visualization::CylindricalArea*, QVector<int>> m_areaHighlightIndices;

    // 每个区域的每个 loop 产生的边索引数量（与 boundary loop 顺序对应）
    std::map<const tailor_visualization::CylindricalArea*, std::vector<int>> m_areaLoopEdgeCounts;

    // cylindricalResults 中填充多边形的数量（前面 N 项是填充，后续是边缘描边）
    int m_cylindricalResultFillCount = 0;
};
