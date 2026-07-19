#include "PeriodicClippingViews.h"

#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QLabel>

PeriodicClippingViews::PeriodicClippingViews(QWidget* parent)
    : QWidget(parent)
{
    setupViews();
}

void PeriodicClippingViews::setupViews()
{
    // 浮层面板样式（与 CylindricalOffsetViews 一致）
    const QString overlayStyle = R"(
        QWidget#overlayPanel {
            background: rgba(40, 40, 45, 210);
            border: 1px solid #555;
            border-radius: 4px;
        }
        QLabel { color: #ddd; font-size: 11px; }
    )";

    // === View 3 — 裁剪分量（左上）===
    auto* leftFrame = new QFrame(this);
    leftFrame->setFrameShape(QFrame::StyledPanel);
    leftFrame->setFrameShadow(QFrame::Sunken);
    auto* leftLayout = new QVBoxLayout(leftFrame);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(m_beforeView = new Sketch2DView(leftFrame));

    m_beforeView->setReadOnly(true);
    m_beforeView->setBoundaryReadOnly(true);

    auto* leftOverlay = new QLabel(m_beforeView);
    leftOverlay->setObjectName("overlayPanel");
    leftOverlay->setStyleSheet(overlayStyle);
    leftOverlay->setText("  3. 裁剪分量  ");
    leftOverlay->adjustSize();
    leftOverlay->move(8, 8);
    leftOverlay->show();

    // === View 4 — 裁剪并集（右上）===
    auto* middleFrame = new QFrame(this);
    middleFrame->setFrameShape(QFrame::StyledPanel);
    middleFrame->setFrameShadow(QFrame::Sunken);
    auto* middleLayout = new QVBoxLayout(middleFrame);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->addWidget(m_afterView = new Sketch2DView(middleFrame));

    m_afterView->setReadOnly(true);
    m_afterView->setBoundaryReadOnly(true);

    auto* middleOverlay = new QLabel(m_afterView);
    middleOverlay->setObjectName("overlayPanel");
    middleOverlay->setStyleSheet(overlayStyle);
    middleOverlay->setText("  4. 裁剪并集  ");
    middleOverlay->adjustSize();
    middleOverlay->move(8, 8);
    middleOverlay->show();

    // === View 5 — 圆柱区域（左下）===
    auto* bottomFrame = new QFrame(this);
    bottomFrame->setFrameShape(QFrame::StyledPanel);
    bottomFrame->setFrameShadow(QFrame::Sunken);
    auto* bottomLayout = new QVBoxLayout(bottomFrame);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->addWidget(m_mergedView = new Sketch2DView(bottomFrame));

    m_mergedView->setReadOnly(true);
    m_mergedView->setBoundaryReadOnly(true);

    auto* bottomOverlay = new QLabel(m_mergedView);
    bottomOverlay->setObjectName("overlayPanel");
    bottomOverlay->setStyleSheet(overlayStyle);
    bottomOverlay->setText("  5. 圆柱区域  ");
    bottomOverlay->adjustSize();
    bottomOverlay->move(8, 8);
    bottomOverlay->show();

    // === 右下角：3D圆柱视图占位 ===
    m_cylinderPlaceholder = new QWidget(this);
    auto* phLayout = new QVBoxLayout(m_cylinderPlaceholder);
    phLayout->setContentsMargins(0, 0, 0, 0);
    phLayout->setSpacing(0);

    // === 2x2 网格布局 ===
    auto* gridLayout = new QGridLayout(this);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(2);

    gridLayout->addWidget(leftFrame, 0, 0);
    gridLayout->addWidget(middleFrame, 0, 1);
    gridLayout->addWidget(bottomFrame, 1, 0);
    gridLayout->addWidget(m_cylinderPlaceholder, 1, 1);

    gridLayout->setRowStretch(0, 1);
    gridLayout->setRowStretch(1, 1);
    gridLayout->setColumnStretch(0, 1);
    gridLayout->setColumnStretch(1, 1);
}

void PeriodicClippingViews::setBeforePolygons(
    const QVector<Sketch2DView::OffsetResultPolygon>& polygons)
{
    // 使用 fillResults 方式显示（支持多色）
    m_beforeView->clearFillResults();
    m_beforeView->clearSelfIntersectionResults();
    m_beforeView->clearOffsetResults();
    m_beforeView->clearDeselfIntersectionResults();
    m_beforeView->setFillResults(polygons);
}

void PeriodicClippingViews::setAfterPolygons(
    const QVector<Sketch2DView::OffsetResultPolygon>& polygons)
{
    // 使用 fillResults 方式显示
    m_afterView->clearFillResults();
    m_afterView->clearSelfIntersectionResults();
    m_afterView->clearOffsetResults();
    m_afterView->clearDeselfIntersectionResults();
    m_afterView->setFillResults(polygons);
}

void PeriodicClippingViews::setMergedPolygons(
    const QVector<Sketch2DView::OffsetResultPolygon>& polygons)
{
    // 使用 fillResults 方式显示
    m_mergedView->clearFillResults();
    m_mergedView->clearSelfIntersectionResults();
    m_mergedView->clearOffsetResults();
    m_mergedView->clearDeselfIntersectionResults();
    m_mergedView->setFillResults(polygons);
}

void PeriodicClippingViews::clear()
{
    m_beforeView->clearFillResults();
    m_beforeView->clearSelfIntersectionResults();
    m_beforeView->clearOffsetResults();
    m_beforeView->clearDeselfIntersectionResults();
    m_beforeView->update();

    m_afterView->clearFillResults();
    m_afterView->clearSelfIntersectionResults();
    m_afterView->clearOffsetResults();
    m_afterView->clearDeselfIntersectionResults();
    m_afterView->update();

    m_mergedView->clearFillResults();
    m_mergedView->clearSelfIntersectionResults();
    m_mergedView->clearOffsetResults();
    m_mergedView->clearDeselfIntersectionResults();
    m_mergedView->update();
}

void PeriodicClippingViews::setBoundaryLines(float left, float right)
{
    m_beforeView->setBoundaryLines(left, right);
    m_afterView->setBoundaryLines(left, right);
    m_mergedView->setBoundaryLines(left, right);
}

void PeriodicClippingViews::setCylinderView(QWidget* view)
{
    if (!m_cylinderPlaceholder) return;
    // 从旧父控件移除（setParent 会自动从旧布局中移除）
    view->setParent(m_cylinderPlaceholder);
    m_cylinderPlaceholder->layout()->addWidget(view);
    view->show();
}
