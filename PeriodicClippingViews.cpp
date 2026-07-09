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
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);

    // === 左侧：View 6 — 周期裁剪各分量 ===
    auto* leftFrame = new QFrame(m_splitter);
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

    // === 右侧：View 7 — 周期裁剪分量求并集 ===
    auto* rightFrame = new QFrame(m_splitter);
    rightFrame->setFrameStyle(QFrame::Box | QFrame::Plain);
    auto* rightLayout = new QVBoxLayout(rightFrame);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    auto* rightLabel = new QLabel("7. 周期并集 (融合结果)", rightFrame);
    rightLabel->setAlignment(Qt::AlignCenter);
    rightLabel->setStyleSheet("QLabel { background: #333; color: #ccc; font-size: 11px; padding: 2px; }");
    rightLayout->addWidget(rightLabel);

    m_afterView = new Sketch2DView(rightFrame);
    m_afterView->setReadOnly(true);
    rightLayout->addWidget(m_afterView);

    m_splitter->addWidget(leftFrame);
    m_splitter->addWidget(rightFrame);
    m_splitter->setSizes({500, 500});

    mainLayout->addWidget(m_splitter);
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
}

void PeriodicClippingViews::setBoundaryLines(float left, float right)
{
    m_beforeView->setBoundaryLines(left, right);
    m_afterView->setBoundaryLines(left, right);
}
