#include "CylindricalOffsetViews.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QLabel>
#include <QSlider>
#include <QString>

CylindricalOffsetViews::CylindricalOffsetViews(QWidget* parent)
    : QWidget(parent)
{
    setupViews();
}

void CylindricalOffsetViews::setupViews() {
    // ---- 创建三个只读素描视图 ----
    m_offsetBoundaryView = new Sketch2DView(this);
    m_booleanResultView = new Sketch2DView(this);
    m_finalResultView = new Sketch2DView(this);

    m_offsetBoundaryView->setReadOnly(true);
    m_booleanResultView->setReadOnly(true);
    m_finalResultView->setReadOnly(true);

    // 浮层面板样式
    const QString overlayStyle = R"(
        QWidget#overlayPanel {
            background: rgba(40, 40, 45, 210);
            border: 1px solid #555;
            border-radius: 4px;
        }
        QLabel { color: #ddd; font-size: 11px; }
        QSlider::groove:horizontal {
            background: #555; height: 4px; border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background: #888; width: 12px; margin: -4px 0; border-radius: 6px;
        }
    )";

    // ---- 左上：View 9 — 偏置边界 ----
    auto* frameLeft = new QFrame(this);
    frameLeft->setFrameShape(QFrame::StyledPanel);
    frameLeft->setFrameShadow(QFrame::Sunken);
    auto* layoutLeft = new QVBoxLayout(frameLeft);
    layoutLeft->setContentsMargins(0, 0, 0, 0);
    layoutLeft->addWidget(m_offsetBoundaryView);

    auto* overlayLeft = new QLabel(m_offsetBoundaryView);
    overlayLeft->setObjectName("overlayPanel");
    overlayLeft->setStyleSheet(overlayStyle);
    overlayLeft->setText("  偏置边界 (去自交后, B)  ");
    overlayLeft->setWordWrap(false);
    overlayLeft->adjustSize();
    overlayLeft->move(8, 8);
    overlayLeft->show();

    // ---- 右上：View 10 — 布尔运算结果 ----
    auto* frameRight = new QFrame(this);
    frameRight->setFrameShape(QFrame::StyledPanel);
    frameRight->setFrameShadow(QFrame::Sunken);
    auto* layoutRight = new QVBoxLayout(frameRight);
    layoutRight->setContentsMargins(0, 0, 0, 0);
    layoutRight->addWidget(m_booleanResultView);

    auto* overlayRight = new QLabel(m_booleanResultView);
    overlayRight->setObjectName("overlayPanel");
    overlayRight->setStyleSheet(overlayStyle);
    overlayRight->setText("  布尔运算结果 (A \u00b1 B)  ");
    overlayRight->setWordWrap(false);
    overlayRight->adjustSize();
    overlayRight->move(8, 8);
    overlayRight->show();

    // ---- 左下：View 11 — 最终偏置区域 + 滑块 ----
    auto* frameBottom = new QFrame(this);
    frameBottom->setFrameShape(QFrame::StyledPanel);
    frameBottom->setFrameShadow(QFrame::Sunken);
    auto* layoutBottom = new QVBoxLayout(frameBottom);
    layoutBottom->setContentsMargins(0, 0, 0, 0);
    layoutBottom->addWidget(m_finalResultView);

    // 浮层面板：偏置距离滑块（父控件为 m_finalResultView）
    auto* overlayBottom = new QWidget(m_finalResultView);
    overlayBottom->setObjectName("overlayPanel");
    overlayBottom->setStyleSheet(overlayStyle);
    auto* overlayLayout = new QHBoxLayout(overlayBottom);
    overlayLayout->setContentsMargins(6, 3, 6, 3);
    overlayLayout->setSpacing(4);

    auto* titleLabel = new QLabel("\u5706\u67f1\u504f\u7f6e:", overlayBottom);
    overlayLayout->addWidget(titleLabel);

    m_offsetSlider = new QSlider(Qt::Horizontal, overlayBottom);
    m_offsetSlider->setRange(-100, 100);
    m_offsetSlider->setValue(10);
    m_offsetSlider->setFixedWidth(140);
    overlayLayout->addWidget(m_offsetSlider);

    m_offsetValueLabel = new QLabel("10", overlayBottom);
    m_offsetValueLabel->setFixedWidth(32);
    m_offsetValueLabel->setAlignment(Qt::AlignCenter);
    m_offsetValueLabel->setStyleSheet("color: #fff; font-weight: bold;");
    overlayLayout->addWidget(m_offsetValueLabel);

    auto* finalTitleLabel = new QLabel("  \u6700\u7ec8\u5706\u67f1\u504f\u7f6e\u533a\u57df  ", m_finalResultView);
    finalTitleLabel->setObjectName("overlayPanel");
    finalTitleLabel->setStyleSheet(overlayStyle);
    finalTitleLabel->adjustSize();
    finalTitleLabel->move(8, 8);
    finalTitleLabel->show();

    // 滑块信号
    connect(m_offsetSlider, &QSlider::valueChanged, this, [this](int val) {
        m_offsetValueLabel->setText(QString::number(val));
        emit offsetDistanceChanged(static_cast<double>(val));
    });

    overlayBottom->adjustSize();
    overlayBottom->move(8, 36);
    overlayBottom->show();

    // ---- 右下角：3D圆柱视图占位 ----
    m_cylinderPlaceholder = new QWidget(this);
    auto* phLayout = new QVBoxLayout(m_cylinderPlaceholder);
    phLayout->setContentsMargins(0, 0, 0, 0);
    phLayout->setSpacing(0);

    // ---- 2x2 网格布局 ----
    auto* gridLayout = new QGridLayout(this);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(2);

    gridLayout->addWidget(frameLeft, 0, 0);
    gridLayout->addWidget(frameRight, 0, 1);
    gridLayout->addWidget(frameBottom, 1, 0);
    gridLayout->addWidget(m_cylinderPlaceholder, 1, 1);

    // 行列等分
    gridLayout->setRowStretch(0, 1);
    gridLayout->setRowStretch(1, 1);
    gridLayout->setColumnStretch(0, 1);
    gridLayout->setColumnStretch(1, 1);
}

void CylindricalOffsetViews::setOffsetBoundaryResults(
    const QVector<Sketch2DView::OffsetResultPolygon>& polygons) {
    m_offsetBoundaryView->clearFillResults();
    m_offsetBoundaryView->clearSelection();
    m_offsetBoundaryView->setFillResults(polygons);
    m_offsetBoundaryView->update();
}

void CylindricalOffsetViews::setBooleanResults(
    const QVector<Sketch2DView::OffsetResultPolygon>& polygons) {
    m_booleanResultView->clearFillResults();
    m_booleanResultView->clearSelection();
    m_booleanResultView->setFillResults(polygons);
    m_booleanResultView->update();
}

void CylindricalOffsetViews::setFinalResults(
    const QVector<Sketch2DView::OffsetResultPolygon>& polygons) {
    m_finalResultView->clearFillResults();
    m_finalResultView->clearSelection();
    m_finalResultView->setFillResults(polygons);
    m_finalResultView->update();
}

void CylindricalOffsetViews::setBoundaryLines(float left, float right) {
    m_offsetBoundaryView->setBoundaryLines(left, right);
    m_booleanResultView->setBoundaryLines(left, right);
    m_finalResultView->setBoundaryLines(left, right);
}

double CylindricalOffsetViews::offsetDistance() const {
    return static_cast<double>(m_offsetSlider->value());
}

void CylindricalOffsetViews::setOffsetDistance(double dist) {
    m_offsetSlider->blockSignals(true);
    m_offsetSlider->setValue(static_cast<int>(dist));
    m_offsetValueLabel->setText(QString::number(static_cast<int>(dist)));
    m_offsetSlider->blockSignals(false);
}

void CylindricalOffsetViews::clear() {
    m_offsetBoundaryView->clearFillResults();
    m_offsetBoundaryView->clearSelection();
    m_offsetBoundaryView->update();

    m_booleanResultView->clearFillResults();
    m_booleanResultView->clearSelection();
    m_booleanResultView->update();

    m_finalResultView->clearFillResults();
    m_finalResultView->clearSelection();
    m_finalResultView->update();
}

void CylindricalOffsetViews::clearResults() {
    clear();
}

void CylindricalOffsetViews::setCylinderView(QWidget* view)
{
    if (!m_cylinderPlaceholder) return;
    view->setParent(m_cylinderPlaceholder);
    m_cylinderPlaceholder->layout()->addWidget(view);
    view->show();
}
