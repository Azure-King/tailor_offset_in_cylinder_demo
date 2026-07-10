#include "PeriodicClippingViews.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFrame>
#include <QLabel>

PeriodicClippingViews::PeriodicClippingViews(QWidget* parent)
    : QWidget(parent)
{
    setupViews();
}

void PeriodicClippingViews::setupViews()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_vertSplitter = new QSplitter(Qt::Vertical, this);

    // === 上半部分：水平分割器 (View 6 | View 7) ===
    m_horizSplitter = new QSplitter(Qt::Horizontal, m_vertSplitter);

    // === View 6 — 周期裁剪各分量 ===
    auto* leftFrame = new QFrame(m_horizSplitter);
    leftFrame->setFrameStyle(QFrame::Box | QFrame::Plain);
    auto* leftLayout = new QVBoxLayout(leftFrame);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    auto* leftLabel = new QLabel("6. 周期裁剪 (各分量)", leftFrame);
    leftLabel->setAlignment(Qt::AlignCenter);
    leftLabel->setStyleSheet("QLabel { background: #333; color: #ccc; font-size: 11px; padding: 2px; }");
    leftLayout->addWidget(leftLabel);

    m_beforeView = new Sketch2DView(leftFrame);
    m_beforeView->setReadOnly(true);
    leftLayout->addWidget(m_beforeView);

    // === View 7 — 周期裁剪并集（简单布尔并集） ===
    auto* middleFrame = new QFrame(m_horizSplitter);
    middleFrame->setFrameStyle(QFrame::Box | QFrame::Plain);
    auto* middleLayout = new QVBoxLayout(middleFrame);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(0);

    auto* middleLabel = new QLabel("7. 周期并集 (简单并集)", middleFrame);
    middleLabel->setAlignment(Qt::AlignCenter);
    middleLabel->setStyleSheet("QLabel { background: #333; color: #ccc; font-size: 11px; padding: 2px; }");
    middleLayout->addWidget(middleLabel);

    m_afterView = new Sketch2DView(middleFrame);
    m_afterView->setReadOnly(true);
    middleLayout->addWidget(m_afterView);

    m_horizSplitter->addWidget(leftFrame);
    m_horizSplitter->addWidget(middleFrame);
    m_horizSplitter->setSizes({500, 500});

    // === View 8 — 圆柱区域合并结果 ===
    auto* bottomFrame = new QFrame(m_vertSplitter);
    bottomFrame->setFrameStyle(QFrame::Box | QFrame::Plain);
    auto* bottomLayout = new QVBoxLayout(bottomFrame);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(0);

    auto* bottomLabel = new QLabel("8. 圆柱区域 (合并结果)", bottomFrame);
    bottomLabel->setAlignment(Qt::AlignCenter);
    bottomLabel->setStyleSheet("QLabel { background: #333; color: #ccc; font-size: 11px; padding: 2px; }");
    bottomLayout->addWidget(bottomLabel);

    m_mergedView = new Sketch2DView(bottomFrame);
    m_mergedView->setReadOnly(true);
    bottomLayout->addWidget(m_mergedView);

    m_vertSplitter->addWidget(m_horizSplitter);
    m_vertSplitter->addWidget(bottomFrame);
    m_vertSplitter->setSizes({300, 300});

    mainLayout->addWidget(m_vertSplitter);
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
