#include "FourViewContainer.h"
#include "Sketch2DView.h"
#include "BooleanOperations.h"
#include "CurveOffset.h"
#include "PeriodicClipper.h"
#include "CylindricalRegion.h"

#include <functional>

// debug
#include "third_party/tailor/test/polygon_io.h"

#include <QFrame>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QVector>
#include <QVariant>
#include <QFormLayout>
#include <QCheckBox>
#include <QMessageBox>

#include <chrono>
#include <iostream>
#include <iomanip>
#include <unordered_set>
#include <unordered_map>
#include <cmath>

// 辅助函数：将 Sketch2DView::Polygon 转换为 Arc 数组，并为每条边分配唯一 ID
static std::vector<tailor_visualization::Arc> polygonToArcs(
	const Sketch2DView::Polygon& poly, int& nextSegmentId) {
	std::vector<tailor_visualization::Arc> arcs;
	for (int i = 0; i < poly.vertices.size(); ++i) {
		const auto& v1 = poly.vertices[i];
		const auto& v2 = poly.vertices[(i + 1) % poly.vertices.size()];
		arcs.push_back(tailor_visualization::Arc(
			tailor_visualization::ArcPoint{ v1.point.x(), v1.point.y() },
			tailor_visualization::ArcPoint{ v2.point.x(), v2.point.y() },
			v1.bulge,
			tailor_visualization::ArcUserData(QRgba64(), nextSegmentId++)
		));
	}
	return arcs;
}

// 辅助函数：将多段线转换为"零面积多边形"的弧段数组
// 多段线本身是开放的，通过构建正反双向边序列模拟一个面积为零的闭合多边形。
// 对于多段线 A→B→C→D，生成六段：AB, BC, CD（正向） + DC, CB, BA（反向）。
// Tailor 算法原生支持处理此类零面积多边形结构，使偏置算法可直接复用。
static std::vector<tailor_visualization::Arc> polylineToArcs(
	const Sketch2DView::Polyline& poly, int& nextSegmentId) {
	std::vector<tailor_visualization::Arc> arcs;
	int n = poly.vertices.size();
	if (n < 2) return arcs;

	// 正向边：沿多段线方向 A→B, B→C, C→D
	for (int i = 0; i < n - 1; ++i) {
		const auto& v1 = poly.vertices[i];
		const auto& v2 = poly.vertices[i + 1];
		arcs.push_back(tailor_visualization::Arc(
			tailor_visualization::ArcPoint{ v1.point.x(), v1.point.y() },
			tailor_visualization::ArcPoint{ v2.point.x(), v2.point.y() },
			v1.bulge,
			tailor_visualization::ArcUserData(QRgba64(), nextSegmentId++)
		));
	}

	// 反向边：沿多段线反向 D→C, C→B, B→A（bulge 取反以表示反向弧）
	for (int i = n - 1; i > 0; --i) {
		const auto& v1 = poly.vertices[i];      // 反向起点（如 D）
		const auto& v2 = poly.vertices[i - 1];  // 反向终点（如 C）
		arcs.push_back(tailor_visualization::Arc(
			tailor_visualization::ArcPoint{ v1.point.x(), v1.point.y() },
			tailor_visualization::ArcPoint{ v2.point.x(), v2.point.y() },
			-v2.bulge,  // 正向弧 bulge 存储在起点 v2，反向弧 bulge 取反
			tailor_visualization::ArcUserData(QRgba64(), nextSegmentId++)
		));
	}

	return arcs;
}

// 辅助函数：将 Arc 数组转换为 Sketch2DView::OffsetResultPolygon（并携带 segmentId + sourceEdgeId + edgeTag + convexJoinVertex）
static Sketch2DView::OffsetResultPolygon arcsToPolygon(
	const std::vector<tailor_visualization::Arc>& arcs,
	const QColor& color = QColor(),
	bool isHole = false) {
	Sketch2DView::OffsetResultPolygon result;
	result.color = color;
	result.isHole = isHole;
	for (const auto& arc : arcs) {
		Sketch2DView::PolygonVertex vertex;
		vertex.point = QPointF(arc.Point0().x, arc.Point0().y);
		vertex.bulge = arc.Bulge();
		result.vertices.append(vertex);
		// 携带 segmentId（当前阶段合并标记）、sourceEdgeId（关系链根节点）和 edgeTag 用于溯源高亮
		result.edgeSegmentIds.append(arc.Data().segmentId);
		result.edgeSourceEdgeIds.append(arc.Data().sourceEdgeId);
		result.edgeTags.append(arc.Data().edgeTag);
		// 凸点连接弧对应的原始顶点坐标（仅在 edgeTag==1 时有效）
		result.edgeConvexJoinVertices.append(
			arc.Data().edgeTag == 1
			? QPointF(arc.Data().convexJoinVertexX, arc.Data().convexJoinVertexY)
			: QPointF());
	}
	return result;
}

// 辅助函数：统计多边形集合中的弧段总数
static size_t countTotalArcs(const std::vector<std::vector<tailor_visualization::Arc>>& polygons) {
	size_t total = 0;
	for (const auto& poly : polygons) {
		total += poly.size();
	}
	return total;
}

FourViewContainer::FourViewContainer(QWidget* parent)
	: QWidget(parent) {
	setupViews();
	setupLayout();
}

FourViewContainer::~FourViewContainer() = default;

void FourViewContainer::setupViews() {
	// Create two views
	m_mainView = new Sketch2DView(this);
	m_topRightView = new Sketch2DView(this);

	// Set secondary view to read-only mode
	m_topRightView->setReadOnly(true);
	m_topRightView->setBoundaryReadOnly(true);

	// Connect main view changes to auto-run the full pipeline
	connect(m_mainView, &Sketch2DView::polylineAdded, this, &FourViewContainer::runFullPipeline);
	connect(m_mainView, &Sketch2DView::polygonAdded, this, &FourViewContainer::runFullPipeline);
	connect(m_mainView, &Sketch2DView::polylineRemoved, this, &FourViewContainer::runFullPipeline);
	connect(m_mainView, &Sketch2DView::polygonRemoved, this, &FourViewContainer::runFullPipeline);
	connect(m_mainView, &Sketch2DView::polylineModified, this, &FourViewContainer::runFullPipeline);
	connect(m_mainView, &Sketch2DView::polygonModified, this, &FourViewContainer::runFullPipeline);
	connect(m_mainView, &Sketch2DView::polygonColorChanged, this, &FourViewContainer::runFullPipeline);

	// 边界线同步：任意视图拖拽边界线后，直接将边界位置同步到所有四个视图
	auto syncBoundaries = [this](float left, float right) {
		m_mainView->setBoundaryLines(left, right);
		m_topRightView->setBoundaryLines(left, right);
		emit boundariesUpdated(left, right);
		// 边界变更后，重新执行周期裁剪（如果已有规范化多边形）
		if (!m_mergedFillArcs.empty()) {
			refreshPeriodicClip();
		}
	};
	connect(m_mainView, &Sketch2DView::boundariesChanged, this, syncBoundaries);
	connect(m_topRightView, &Sketch2DView::boundariesChanged, this, syncBoundaries);
}

void FourViewContainer::setupLayout() {
	m_layout = new QGridLayout(this);
	m_layout->setContentsMargins(0, 0, 0, 0);
	m_layout->setSpacing(2);

	// 浮层控件样式
	const char* overlayStyle = R"(
        QWidget#overlayPanel {
            background: rgba(30, 30, 30, 160);
            border-radius: 6px;
            padding: 2px 6px;
        }
        QLabel { color: #ddd; font-size: 11px; }
        QComboBox {
            background: #3a3a3a; color: #eee; border: 1px solid #555;
            border-radius: 3px; padding: 1px 4px; font-size: 11px; min-width: 70px;
        }
        QComboBox::drop-down { border: none; }
        QComboBox QAbstractItemView {
            background: #2a2a2a; color: #eee; selection-background-color: #5a5a5a;
        }
    )";

	// --- 主视图 (左上) ---
	auto* frameMain = new QFrame(this);
	frameMain->setFrameShape(QFrame::StyledPanel);
	frameMain->setFrameShadow(QFrame::Sunken);
	auto* layoutMain = new QVBoxLayout(frameMain);
	layoutMain->setContentsMargins(0, 0, 0, 0);
	layoutMain->addWidget(m_mainView);

	// --- 第二视图 (右上)：视图填满，控件浮在上方 ---
	auto* frameTopRight = new QFrame(this);
	frameTopRight->setFrameShape(QFrame::StyledPanel);
	frameTopRight->setFrameShadow(QFrame::Sunken);
	auto* layoutTopRight = new QVBoxLayout(frameTopRight);
	layoutTopRight->setContentsMargins(0, 0, 0, 0);
	layoutTopRight->addWidget(m_topRightView);

	// 浮层面板：填充+连接方式 (父控件为 m_topRightView)
	auto* overlayTopRight = new QWidget(m_topRightView);
	overlayTopRight->setObjectName("overlayPanel");
	overlayTopRight->setStyleSheet(overlayStyle);
	auto* ol2 = new QHBoxLayout(overlayTopRight);
	ol2->setContentsMargins(6, 3, 6, 3);
	ol2->setSpacing(4);

	m_fillTypeCombo = new QComboBox(overlayTopRight);
	m_fillTypeCombo->addItem("NonZero", 0);
	m_fillTypeCombo->addItem("EvenOdd", 1);
	m_fillTypeCombo->addItem("Ignore", 2);
	m_fillTypeCombo->addItem("Positive", 3);
	m_fillTypeCombo->addItem("Winding=1", 4);
	m_fillTypeCombo->setCurrentIndex(1);
	ol2->addWidget(new QLabel("填充:", overlayTopRight));
	ol2->addWidget(m_fillTypeCombo);

	m_connectTypeCombo = new QComboBox(overlayTopRight);
	m_connectTypeCombo->addItem("外先", 0);
	m_connectTypeCombo->addItem("内先", 1);
	m_connectTypeCombo->setCurrentIndex(0);
	ol2->addWidget(new QLabel("连接:", overlayTopRight));
	ol2->addWidget(m_connectTypeCombo);

	m_consoleTimeCheck = new QCheckBox("计时", overlayTopRight);
	m_consoleTimeCheck->setChecked(true);
	m_consoleTimeCheck->setStyleSheet("color: #ddd; font-size: 11px;");
	ol2->addWidget(m_consoleTimeCheck);

	m_consolePolygonCheck = new QCheckBox("多边形", overlayTopRight);
	m_consolePolygonCheck->setChecked(false);
	m_consolePolygonCheck->setStyleSheet("color: #ddd; font-size: 11px;");
	connect(m_consolePolygonCheck, &QCheckBox::toggled, this, [](bool checked) {
		tailor_visualization::BooleanOperations::s_consolePolygonOutput = checked;
		});
	ol2->addWidget(m_consolePolygonCheck);

	// 下拉框变更时触发流水线
	connect(m_fillTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &FourViewContainer::runFullPipeline);
	connect(m_connectTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &FourViewContainer::runFullPipeline);

	overlayTopRight->adjustSize();
	overlayTopRight->move(8, 8);
	overlayTopRight->show();

	// 1x2 grid
	m_layout->addWidget(frameMain, 0, 0);
	m_layout->addWidget(frameTopRight, 0, 1);

	m_layout->setColumnStretch(0, 1);
	m_layout->setColumnStretch(1, 1);
	m_layout->setRowStretch(0, 1);
}

void FourViewContainer::synchronizeViews() {
	// 清除所有视图的高亮状态
	m_mainView->clearHighlightedSourceSegmentIds();
	m_topRightView->clearHighlightedSourceSegmentIds();

	// 清除第二视图的所有数据（不显示原始多边形）
	m_topRightView->clearSelection();
	m_topRightView->clearFillResults();
	m_topRightView->clearSelfIntersectionResults();
	m_topRightView->clearOffsetResults();
	m_topRightView->clearDeselfIntersectionResults();
	m_topRightView->update();
}

void FourViewContainer::processSelfIntersection() {
	const auto& polygons = m_mainView->polygons();
	const auto& polylines = m_mainView->polylines();
	if (polygons.isEmpty() && polylines.isEmpty()) {
		return;
	}

	// 统计用户输入规模
	int inputEdges = 0;
	for (const auto& poly : polygons) inputEdges += poly.vertices.size();
	for (const auto& poly : polylines) inputEdges += (poly.vertices.size() >= 2 ? poly.vertices.size() - 1 : 0);

	// 预定义颜色调色板
	static std::vector<QColor> colorPalette = {
		QColor(255, 100, 100),   // 红色
		QColor(100, 200, 100),   // 绿色
		QColor(100, 100, 255),   // 蓝色
		QColor(255, 200, 100),   // 橙色
		QColor(200, 100, 255),   // 紫色
		QColor(100, 255, 200),  // 青色
		QColor(255, 150, 100),  // 橙红
		QColor(150, 100, 255),   // 紫蓝
	};

	// Step 1: 将所有多边形和多段线加入 tailor subject 集合，并为每条边分配唯一 ID
	tailor_visualization::BooleanOperations boolOp;
	int nextSegmentId = 0;
	for (const auto& poly : polygons) {
		auto arcs = polygonToArcs(poly, nextSegmentId);
		boolOp.AddSubjectPolygon(arcs);
	}
	// 多段线：构建零面积多边形（正反向边序列）并加入 subject 集合
	for (const auto& poly : polylines) {
		auto arcs = polylineToArcs(poly, nextSegmentId);
		boolOp.AddSubjectPolygon(arcs);
	}

	// Step 2: 执行 OnlySubjectPattern 获取非自交曲线
	int fillTypeIndex = m_fillTypeCombo->currentData().toInt();
	const tailor_visualization::IFillType* fillType = nullptr;
	switch (fillTypeIndex) {
	case 0: fillType = std::addressof(*new tailor_visualization::NonZeroFillTypeWrapper()); break;
	case 1: fillType = std::addressof(*new tailor_visualization::EvenOddFillTypeWrapper()); break;
	case 2: fillType = std::addressof(*new tailor_visualization::IgnoreFillTypeWrapper()); break;
	case 3: fillType = std::addressof(*new tailor_visualization::PositiveWindFillTypeWrapper()); break;  // 环绕数>0
	case 4: fillType = std::addressof(*new tailor_visualization::SpecificWindingFillTypeWrapper(1)); break;
	default: fillType = std::addressof(*new tailor_visualization::EvenOddFillTypeWrapper()); break;
	}

	// 读取连接类型（内角优先 vs 外角优先）
	using Drafting = typename tailor_visualization::ArcTailor::PatternDrafting;
	tailor_visualization::ConnectTypeOuterFirstWrapper<Drafting> outerFirst;
	tailor_visualization::ConnectTypeInnerFirstWrapper<Drafting> innerFirst;
	const tailor_visualization::IConnectType<Drafting>* connectType =
		(m_connectTypeCombo->currentData().toInt() == 1)
		? static_cast<const tailor_visualization::IConnectType<Drafting>*>(&innerFirst)
		: static_cast<const tailor_visualization::IConnectType<Drafting>*>(&outerFirst);

	// ==================== Stage 1: User Input -> Tailor Boolean Canonical Form ====================
	if (m_consoleTimeCheck->isChecked()) {
		std::cout << "\n========== Pipeline Performance Stats ==========" << std::endl;
		std::cout << "[Stage 1: Input -> Tailor Boolean] input: "
			<< polygons.size() << " polygons, "
			<< polylines.size() << " polylines, "
			<< inputEdges << " edges" << std::endl;
	}

	auto t1_start = std::chrono::high_resolution_clock::now();
	// 使用 WithHoles 版本，通过 polytree depth 获取内环信息
	auto resultWithHoles = boolOp.ExecuteOnlySubjectPatternWithHoles(fillType, connectType);
	auto t1_end = std::chrono::high_resolution_clock::now();

	// 分离弧段和内环标记
	std::vector<std::vector<tailor_visualization::Arc>> resultArcs;
	m_mergedFillIsHole.clear();
	for (auto& info : resultWithHoles) {
		resultArcs.push_back(std::move(info.vertices));
		m_mergedFillIsHole.push_back(info.isHole);
	}

	double t1_ms = std::chrono::duration_cast<std::chrono::microseconds>(t1_end - t1_start).count() / 1000.0;
	size_t t1_outputArcs = countTotalArcs(resultArcs);
	if (m_consoleTimeCheck->isChecked()) {
		std::cout << "  output: " << resultArcs.size() << " polygons, "
			<< t1_outputArcs << " arcs" << std::endl;
		std::cout << "  time: " << std::fixed << std::setprecision(2) << t1_ms << " ms" << std::endl;
	}
	// ==================== Stage 2: Curve Merge ====================
	size_t t2_beforeArcs = countTotalArcs(resultArcs);
	auto t2_start = std::chrono::high_resolution_clock::now();

	if constexpr (tailor_visualization::ENABLE_CURVE_MERGE) {
		tailor_visualization::MergeAdjacentCurvesBatch(resultArcs);
	}

	auto t2_end = std::chrono::high_resolution_clock::now();
	double t2_ms = std::chrono::duration_cast<std::chrono::microseconds>(t2_end - t2_start).count() / 1000.0;
	size_t t2_afterArcs = countTotalArcs(resultArcs);
	if (m_consoleTimeCheck->isChecked()) {
		std::cout << "[Stage 2: Curve Merge] input: " << resultArcs.size() << " polygons, "
			<< t2_beforeArcs << " arcs" << std::endl;
		std::cout << "  output: " << resultArcs.size() << " polygons, "
			<< t2_afterArcs << " arcs" << std::endl;
		std::cout << "  time: " << std::fixed << std::setprecision(2) << t2_ms << " ms" << std::endl;
	}
	// Step 2.6: 重新分配唯一的本地 segmentId，替换原始输入边 ID
	// 这样每条 fillResult 边都有唯一标识，高亮时能精确定位到打断后的具体边
	// 注意：sourceEdgeId 保持不变，作为关系链根节点用于全流水线溯源
	m_localToOriginalSegId.clear();
	int localSegId = 0;
	for (auto& polygonArcs : resultArcs) {
		for (auto& arc : polygonArcs) {
			int originalId = arc.Data().sourceEdgeId;
			m_localToOriginalSegId[localSegId] = originalId;
			arc.Data().segmentId = localSegId++;
		}
	}

	m_mergedFillArcs = resultArcs;  // 保存到中间数据供下个步骤使用

	// 转换为填充结果（第二视图用，不同多边形不同颜色）
	QVector<Sketch2DView::OffsetResultPolygon> fillResults;
	for (size_t i = 0; i < resultArcs.size(); ++i) {
		QColor color = colorPalette[i % colorPalette.size()];
		fillResults.append(arcsToPolygon(resultArcs[i], color));
	}
	// 转换为自交处理结果（橙色，用于参考）
	QVector<Sketch2DView::OffsetResultPolygon> selfIntersectionResults;
	for (const auto& arcs : resultArcs) {
		selfIntersectionResults.append(arcsToPolygon(arcs, QColor(255, 165, 0)));
	}

	// 设置第二视图：只显示填充结果
	m_topRightView->clearSelfIntersectionResults();
	m_topRightView->setFillResults(fillResults);
	m_topRightView->clearOffsetResults();
	m_topRightView->clearDeselfIntersectionResults();

	emit pipelineStepChanged(1);
}

void FourViewContainer::runFullPipeline() {
	// 先清除所有视图的原始数据
	synchronizeViews();
	// 执行流水线（自交处理 + 周期裁剪）
	processSelfIntersection();
	// 周期裁剪步骤（规范化后 → 裁剪到圆柱带内）
	if (!m_mergedFillArcs.empty()) {
		processPeriodicClip();
	}
}

// ==================== 周期裁剪步骤 ====================

void FourViewContainer::processPeriodicClip() {
	if (m_mergedFillArcs.empty()) {
		return;
	}

	// 动态颜色调色板
	static std::vector<QColor> s_colorPalette = {
		QColor(255, 100, 100),   // 红色
		QColor(100, 200, 100),   // 绿色
		QColor(100, 100, 255),   // 蓝色
		QColor(255, 200, 100),   // 橙色
		QColor(200, 100, 255),   // 紫色
		QColor(100, 255, 200),   // 青色
		QColor(255, 150, 100),   // 橙红
		QColor(150, 100, 255),   // 紫蓝
	};

	double bLeft = static_cast<double>(m_mainView->boundaryLeft());
	double bRight = static_cast<double>(m_mainView->boundaryRight());

	// Step 1: 周期裁剪到圆柱面内，得到各个分量
	auto clippedArcs = tailor_visualization::PeriodicClipper::ClipToStrip(
		m_mergedFillArcs, bLeft, bRight);

	// --- View 3: 周期裁剪后的各分量 ---
	QVector<Sketch2DView::OffsetResultPolygon> clippedResults;
	for (size_t i = 0; i < clippedArcs.size(); ++i) {
		QColor color = s_colorPalette[i % s_colorPalette.size()];
		clippedResults.append(arcsToPolygon(clippedArcs[i], color));
	}

    // --- View 4: 所有裁剪分量求并集（简单布尔并集） ---
    QVector<Sketch2DView::OffsetResultPolygon> mergedResults;
    QVector<Sketch2DView::OffsetResultPolygon> cylindricalResults;
    if (!clippedArcs.empty()) {
        tailor_visualization::BooleanOperations boolOp;
        for (const auto& arcs : clippedArcs) {
            boolOp.AddSubjectPolygon(arcs);
        }
        auto mergedAnnotated = boolOp.ExecuteOnlySubjectPatternAnnotated(
            static_cast<const tailor_visualization::IFillType*>(nullptr));

        // 保存 View 4 并集结果，供圆柱偏置流水线使用
        m_mergedAnnotated = mergedAnnotated;

        // --- 构建圆柱区域树（放在渲染之前，用于区域颜色分配）---
        m_cylindricalAreaTree = tailor_visualization::BuildCylindricalAreas(
            mergedAnnotated, bLeft, bRight);

        // 调试输出
        if (m_consoleTimeCheck->isChecked()) {
            int bandCount = 0, contCount = 0;
            for (const auto& area : m_cylindricalAreaTree) {
                if (area.isBand()) ++bandCount;
                else if (area.isContractibleArea()) ++contCount;
            }
            std::cout << "[CylindricalRegion] Built area tree: "
                      << bandCount << " bands, " << contCount
                      << " contractible, " << m_cylindricalAreaTree.size()
                      << " total top-level areas" << std::endl;
        }

        // ---- 颜色调色板 ----
        static const std::vector<QColor> kAreaColors = {
            QColor(255, 100, 100),   // 红色
            QColor(100, 200, 100),   // 绿色
            QColor(100, 100, 255),   // 蓝色
            QColor(255, 200, 100),   // 橙色
            QColor(200, 100, 255),   // 紫色
            QColor(100, 255, 200),   // 青色
            QColor(255, 150, 100),   // 橙红
            QColor(150, 100, 255),   // 紫蓝
        };

        // ---- 辅助函数：收集 sourceEdgeId 指纹 ----
        auto arcFp = [](const std::vector<tailor_visualization::Arc>& arcs) -> std::set<int> {
            std::set<int> ids;
            for (const auto& arc : arcs) {
                ids.insert(arc.Data().sourceEdgeId);
            }
            return ids;
        };

        auto collectFp = [](const tailor_visualization::CylindricalArea& area) -> std::set<int> {
            std::set<int> ids;
            std::function<void(const tailor_visualization::CylindricalArea&)> go;
            go = [&](const tailor_visualization::CylindricalArea& a) {
                for (const auto& loop : a.boundary) {
                    for (const auto& arc : loop.arcs) {
                        ids.insert(arc.Data().sourceEdgeId);
                    }
                }
                for (const auto& child : a.children) {
                    go(child);
                }
            };
            go(area);
            return ids;
        };

        // ---- 按 sourceEdgeId 指纹为顶层区域预分配颜色（同源同色） + 区域地址映射 ----
        int areaColorIdx = 0;
        std::map<std::set<int>, QColor> fpColorMap;
        std::map<std::set<int>, const tailor_visualization::CylindricalArea*> fpToArea;
        for (const auto& area : m_cylindricalAreaTree) {
            auto fp = collectFp(area);
            if (fpColorMap.find(fp) == fpColorMap.end()) {
                fpColorMap[fp] = kAreaColors[areaColorIdx++ % kAreaColors.size()];
                fpToArea[fp] = &area;
            }
        }

        // ---- 将每个 mergedAnnotated 多边形映射到区域颜色 + 区域指针（sourceEdgeId 重叠度匹配） ----
        std::vector<QColor> polyColors(mergedAnnotated.size());
        std::vector<const tailor_visualization::CylindricalArea*> polyToBestArea(mergedAnnotated.size(), nullptr);
        for (size_t pi = 0; pi < mergedAnnotated.size(); ++pi) {
            auto pFp = arcFp(mergedAnnotated[pi].arcs);
            size_t bestOverlap = 0;
            QColor bestColor = kAreaColors[pi % kAreaColors.size()];
            const tailor_visualization::CylindricalArea* bestArea = nullptr;
            for (const auto& [aFp, aColor] : fpColorMap) {
                size_t overlap = 0;
                for (int id : pFp) {
                    if (aFp.count(id)) ++overlap;
                }
                if (overlap > bestOverlap) {
                    bestOverlap = overlap;
                    bestColor = aColor;
                    bestArea = fpToArea[aFp];
                }
            }
            polyColors[pi] = bestColor;
            polyToBestArea[pi] = bestArea;
        }

        // ==== View 4: 原始布尔并集多边形 + 圆柱区域着色 ====
        // 直接使用 mergedAnnotated 的多边形形状进行填充，
        // 同一圆柱区域的多边形使用相同颜色，避免边界拆分导致的颜色分裂
        for (size_t i = 0; i < mergedAnnotated.size(); ++i) {
            mergedResults.append(arcsToPolygon(mergedAnnotated[i].arcs, polyColors[i]));
        }

        // ==== View 5: 原始多边形填充 + 区域边界描边 ====
        // 填充：与 View 4 相同的原始多边形（不再构建连接左右边界的假填充面）
        m_cylindricalResultFillCount = static_cast<int>(mergedAnnotated.size());
        for (size_t i = 0; i < mergedAnnotated.size(); ++i) {
            cylindricalResults.append(arcsToPolygon(mergedAnnotated[i].arcs, polyColors[i]));
        }

        // 边界描边：从区域树中提取各区域的边界弧段，用于可视化区域结构
        // 同时追踪每个区域对应的边缘线在 cylindricalResults 中的索引
        {
            // 从区域指纹找颜色
            auto areaToColor = [&](const tailor_visualization::CylindricalArea& area) -> QColor {
                auto fp = collectFp(area);
                auto it = fpColorMap.find(fp);
                return (it != fpColorMap.end()) ? it->second : kAreaColors[0];
            };

            std::function<void(const std::vector<tailor_visualization::CylindricalArea>&, int)> renderEdges;
            renderEdges = [&](const std::vector<tailor_visualization::CylindricalArea>& areas, int depth) {
                for (const auto& area : areas) {
                    QColor areaColor = areaToColor(area);
                    QVector<int>& areaIndices = m_areaHighlightIndices[&area]; // auto-create
                    std::vector<int>& loopEdgeCounts = m_areaLoopEdgeCounts[&area]; // auto-create

                    for (const auto& loop : area.boundary) {
                        if (loop.arcs.empty()) {
                            loopEdgeCounts.push_back(0);
                            continue;
                        }

                        // 整环生成一条 edge polygon（包含所有弧段），
                        // 确保选中边界项时完整高亮（包括左右边界线上的弧段）
                        loopEdgeCounts.push_back(1);

                        int edgeIdx = static_cast<int>(cylindricalResults.size());
                        areaIndices.append(edgeIdx);

                        Sketch2DView::OffsetResultPolygon edgePoly;
                        edgePoly.color = areaColor.darker(120);
                        edgePoly.isHole = (depth % 2 == 1);
                        edgePoly.isOpen = true;

                        int n = static_cast<int>(loop.arcs.size());
                        for (int j = 0; j < n; ++j) {
                            const auto& arc = loop.arcs[j];
                            Sketch2DView::PolygonVertex vertex;
                            vertex.point = QPointF(arc.Point0().x, arc.Point0().y);
                            vertex.bulge = arc.Bulge();
                            edgePoly.vertices.append(vertex);
                            edgePoly.edgeSegmentIds.append(arc.Data().segmentId);
                            edgePoly.edgeSourceEdgeIds.append(arc.Data().sourceEdgeId);
                            edgePoly.edgeTags.append(arc.Data().edgeTag);
                            edgePoly.edgeConvexJoinVertices.append(
                                arc.Data().edgeTag == 1
                                ? QPointF(arc.Data().convexJoinVertexX, arc.Data().convexJoinVertexY)
                                : QPointF());
                        }
                        {
                            const auto& lastArc = loop.arcs[n - 1];
                            Sketch2DView::PolygonVertex endVertex;
                            endVertex.point = QPointF(lastArc.Point1().x, lastArc.Point1().y);
                            endVertex.bulge = 0.0;
                            edgePoly.vertices.append(endVertex);
                            edgePoly.edgeSegmentIds.append(0);
                            edgePoly.edgeSourceEdgeIds.append(0);
                            edgePoly.edgeTags.append(0);
                            edgePoly.edgeConvexJoinVertices.append(QPointF());
                        }
                        cylindricalResults.append(std::move(edgePoly));
                    }
                    renderEdges(area.children, depth + 1);
                }
            };
            renderEdges(m_cylindricalAreaTree, 0);
        }

        // ---- 将填充多边形的索引也加入对应区域的 highlight 集合 ----
        for (size_t pi = 0; pi < mergedAnnotated.size(); ++pi) {
            if (polyToBestArea[pi]) {
                int fillIdx = static_cast<int>(pi); // fill polygons at indices 0..N-1
                m_areaHighlightIndices[polyToBestArea[pi]].prepend(fillIdx);
            }
        }
    }

    emit periodicClipResultReady(clippedResults, mergedResults);
    emit periodicCylindricalResultReady(cylindricalResults);
    emit regionTreeUpdated();
}

void FourViewContainer::refreshPeriodicClip() {
	// 边界线变更后重新裁剪（复用已缓存的规范化多边形 m_mergedFillArcs）
	processPeriodicClip();
}

void FourViewContainer::buildRegionTree(QTreeWidget* tree) const {
    tree->clear();
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);

    bool hasOriginal = !m_cylindricalAreaTree.empty();
    bool hasOffset = !m_offsetCylindricalAreaTree.empty();

    if (!hasOriginal && !hasOffset) {
        auto* placeholder = new QTreeWidgetItem(tree);
        placeholder->setText(0, "(无区域结果 — 请先绘制多边形并运行流水线)");
        placeholder->setForeground(0, QColor(128, 128, 128));
        return;
    }

    // 可切换的 map 指针，供 buildAreaNodes 通过引用捕获使用
    const std::map<const tailor_visualization::CylindricalArea*, QVector<int>>* highlightMap = nullptr;
    const std::map<const tailor_visualization::CylindricalArea*, std::vector<int>>* loopEdgeCounts = nullptr;

    // 辅助：为一个区域树构建子树（递归处理子区域）
    std::function<void(const std::vector<tailor_visualization::CylindricalArea>&,
        int, int, QTreeWidgetItem*)> buildAreaNodes;
    buildAreaNodes = [&](
        const std::vector<tailor_visualization::CylindricalArea>& areaTree,
        int fillCount, int depth,
        QTreeWidgetItem* parent) {

        for (size_t ai = 0; ai < areaTree.size(); ++ai) {
            const auto& area = areaTree[ai];

            QVector<int> allIndices;
            if (highlightMap) {
                auto it = highlightMap->find(&area);
                if (it != highlightMap->end()) allIndices = it->second;
            }

            QVector<int> edgeIndices;
            for (int idx : allIndices) {
                if (idx >= fillCount) edgeIndices.append(idx);
            }

            bool isChild = (depth > 0);
            QString areaType = area.isBand() ? "条带" : "可缩";
            QString areaLabel = isChild
                ? QString("子区域%1(%2) [%3]").arg(depth).arg(ai + 1).arg(areaType)
                : QString("区域%1 [%2]").arg(ai + 1).arg(areaType);
            auto* areaItem = new QTreeWidgetItem(parent);
            areaItem->setText(0, areaLabel);
            areaItem->setData(0, Qt::UserRole, QVariant::fromValue(allIndices));

            std::vector<int> loopCounts;
            if (loopEdgeCounts) {
                auto countsIt = loopEdgeCounts->find(&area);
                if (countsIt != loopEdgeCounts->end()) loopCounts = countsIt->second;
            }

            if (area.isBand()) {
                QVector<int> upperIndices, lowerIndices;
                int edgeOffset = 0;
                for (size_t li = 0; li < area.boundary.size(); ++li) {
                    int count = (li < loopCounts.size()) ? loopCounts[li] : 0;
                    QVector<int>& target =
                        area.boundary[li].leftToRight ? lowerIndices : upperIndices;
                    for (int e = 0;
                         e < count && (edgeOffset + e) < (int)edgeIndices.size();
                         ++e)
                        target.append(edgeIndices[edgeOffset + e]);
                    edgeOffset += count;
                }

                if (!upperIndices.isEmpty()) {
                    auto* item = new QTreeWidgetItem(areaItem);
                    item->setText(0, "上边界");
                    item->setData(0, Qt::UserRole,
                        QVariant::fromValue(upperIndices));
                }
                if (!lowerIndices.isEmpty()) {
                    auto* item = new QTreeWidgetItem(areaItem);
                    item->setText(0, "下边界");
                    item->setData(0, Qt::UserRole,
                        QVariant::fromValue(lowerIndices));
                }
            } else {
                if (!edgeIndices.isEmpty()) {
                    auto* item = new QTreeWidgetItem(areaItem);
                    item->setText(0, "边界");
                    item->setData(0, Qt::UserRole,
                        QVariant::fromValue(edgeIndices));
                }
            }

            // 递归子区域
            if (!area.children.empty()) {
                buildAreaNodes(area.children, fillCount, depth + 1, areaItem);
            }
        }
    };

    // ---- 原始区域 ----
    if (hasOriginal) {
        auto* origHeader = new QTreeWidgetItem(tree);
        origHeader->setText(0, "原始区域");
        origHeader->setForeground(0, QColor(60, 120, 200));
        QFont boldFont = origHeader->font(0);
        boldFont.setBold(true);
        origHeader->setFont(0, boldFont);
        // 不设置 UserRole，避免选中时误高亮
        origHeader->setFlags(origHeader->flags() & ~Qt::ItemIsSelectable);
        origHeader->setData(0, Qt::UserRole,
            QVariant::fromValue(QVector<int>()));

        highlightMap = &m_areaHighlightIndices;
        loopEdgeCounts = &m_areaLoopEdgeCounts;
        buildAreaNodes(m_cylindricalAreaTree,
            m_cylindricalResultFillCount, 0, origHeader);
    }

    // ---- 偏置区域 ----
    if (hasOffset) {
        auto* offsetHeader = new QTreeWidgetItem(tree);
        offsetHeader->setText(0, "偏置区域");
        offsetHeader->setForeground(0, QColor(60, 170, 60));
        QFont boldFont = offsetHeader->font(0);
        boldFont.setBold(true);
        offsetHeader->setFont(0, boldFont);
        offsetHeader->setFlags(offsetHeader->flags() & ~Qt::ItemIsSelectable);
        offsetHeader->setData(0, Qt::UserRole,
            QVariant::fromValue(QVector<int>()));

        highlightMap = &m_offsetHighlightIndices;
        loopEdgeCounts = &m_offsetLoopEdgeCounts;
        buildAreaNodes(m_offsetCylindricalAreaTree,
            m_offsetResultFillCount, 0, offsetHeader);
    }

    tree->expandAll();
}

void FourViewContainer::processCylindricalOffset(double distance) {
    using namespace tailor_visualization;
    using ArcType = Arc;
    using PointType = ArcPoint;

    if (m_cylindricalAreaTree.empty() || m_mergedAnnotated.empty()) {
        return;
    }

    double bLeft = static_cast<double>(m_mainView->boundaryLeft());
    double bRight = static_cast<double>(m_mainView->boundaryRight());
    double absDist = std::abs(distance);

    // ===== Step 1: 收集圆柱区域树中所有边界 loop =====
    std::vector<std::vector<ArcType>> boundaryLoops;
    std::function<void(const std::vector<CylindricalArea>&)> collectLoops;
    collectLoops = [&](const std::vector<CylindricalArea>& areas) {
        for (const auto& area : areas) {
            for (const auto& loop : area.boundary) {
                if (!loop.arcs.empty()) {
                    boundaryLoops.push_back(loop.arcs);
                }
            }
            collectLoops(area.children);
        }
    };
    collectLoops(m_cylindricalAreaTree);

    if (boundaryLoops.empty()) return;

    std::cout << "[CylOffset] Extracted " << boundaryLoops.size()
        << " boundary loops, distance=" << distance << std::endl;

    // ===== Step 2: 每条边界 → 零面积多边形 → 偏置(abs) → 去自交 =====
    std::vector<std::vector<ArcType>> allOffsetBoundaries;

    for (auto& loop : boundaryLoops) {
        size_t n = loop.size();
        if (n < 1) continue;

        // 构造零面积多边形：正向弧 + 反向弧
        // 多段线 ABC → ABCBA；闭合形状 ABCA → ABCACBA
        std::vector<ArcType> closedArcs;
        closedArcs.reserve(n * 2);

        // 正向
        for (const auto& arc : loop) {
            closedArcs.push_back(arc);
        }

        // 反向：从最后一条弧的终点往回走到起点
        for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
            const auto& arc = loop[i];
            PointType p0(arc.Point1().x, arc.Point1().y);
            PointType p1(arc.Point0().x, arc.Point0().y);
            double revBulge = -arc.Bulge();
            closedArcs.push_back(ArcType(p0, p1, revBulge, arc.Data()));
        }

        // 偏置（向外，取距离绝对值）
        auto offsetResult = tailor_offset::CurveOffseter<ArcType, double>::OffsetClosed(
            closedArcs, absDist, true);

        std::vector<ArcType> offsetPoly;
        for (const auto& resultArc : offsetResult) {
            offsetPoly.push_back(resultArc);
        }

        if (offsetPoly.empty()) continue;

        // 去自交
        BooleanOperations boolOp;
        boolOp.AddSubjectPolygon(offsetPoly);
        auto deselfAnnotated = boolOp.ExecuteOnlySubjectPatternAnnotated(
            static_cast<const IFillType*>(nullptr));

        for (auto& poly : deselfAnnotated) {
            if (!poly.arcs.empty()) {
                allOffsetBoundaries.push_back(std::move(poly.arcs));
            }
        }
    }

    // 转换偏置边界结果为 OffsetResultPolygon（View: 偏置边界 B）
    QVector<Sketch2DView::OffsetResultPolygon> offsetBoundaryResults;

    // 先叠加原始圆柱区域边界（只描边，不填充）
    {
        const QColor kOriginalBoundaryColor(150, 155, 165);
        for (const auto& loop : boundaryLoops) {
            size_t m = loop.size();
            if (m < 1) continue;
            Sketch2DView::OffsetResultPolygon edgePoly;
            edgePoly.color = kOriginalBoundaryColor;
            edgePoly.isHole = false;
            edgePoly.isOpen = true;
            for (size_t j = 0; j < m; ++j) {
                const auto& arc = loop[j];
                Sketch2DView::PolygonVertex vertex;
                vertex.point = QPointF(arc.Point0().x, arc.Point0().y);
                vertex.bulge = arc.Bulge();
                edgePoly.vertices.append(vertex);
                edgePoly.edgeSegmentIds.append(arc.Data().segmentId);
                edgePoly.edgeSourceEdgeIds.append(arc.Data().sourceEdgeId);
                edgePoly.edgeTags.append(arc.Data().edgeTag);
                edgePoly.edgeConvexJoinVertices.append(
                    arc.Data().edgeTag == 1
                    ? QPointF(arc.Data().convexJoinVertexX, arc.Data().convexJoinVertexY)
                    : QPointF());
            }
            {
                const auto& lastArc = loop.back();
                Sketch2DView::PolygonVertex endVertex;
                endVertex.point = QPointF(lastArc.Point1().x, lastArc.Point1().y);
                endVertex.bulge = 0.0;
                edgePoly.vertices.append(endVertex);
                edgePoly.edgeSegmentIds.append(0);
                edgePoly.edgeSourceEdgeIds.append(0);
                edgePoly.edgeTags.append(0);
                edgePoly.edgeConvexJoinVertices.append(QPointF());
            }
            offsetBoundaryResults.append(edgePoly);
        }
    }

    {
        const std::vector<QColor> kOffsetColors = {
            QColor(80, 180, 240), QColor(60, 160, 220),
            QColor(100, 200, 255), QColor(120, 180, 230)
        };
        for (size_t i = 0; i < allOffsetBoundaries.size(); ++i) {
            offsetBoundaryResults.append(
                arcsToPolygon(allOffsetBoundaries[i], kOffsetColors[i % kOffsetColors.size()]));
        }
    }

    if (allOffsetBoundaries.empty()) {
        emit cylindricalOffsetResultReady(offsetBoundaryResults, {}, {});
        return;
    }

    // ===== Step 3: 合并所有去自交后的偏置边界 → region B =====
    std::vector<AnnotatedPolygon> regionB;
    {
        BooleanOperations mergeOp;
        for (auto& arcs : allOffsetBoundaries) {
            mergeOp.AddSubjectPolygon(arcs);
        }
        regionB = mergeOp.ExecuteOnlySubjectPatternAnnotated(
            static_cast<const IFillType*>(nullptr));
    }

    std::cout << "[CylOffset] regionB merged: " << regionB.size()
        << " annotated polygons" << std::endl;

    // ===== Step 3.5: regionB → 周期裁剪到条带 → 再合并 =====
    std::vector<AnnotatedPolygon> regionB_processed;
    if (!regionB.empty()) {
        std::vector<std::vector<ArcType>> regionB_raw;
        for (auto& poly : regionB) {
            regionB_raw.push_back(std::move(poly.arcs));
        }
        auto clippedB = PeriodicClipper::ClipToStrip(regionB_raw, bLeft, bRight);
        std::cout << "[CylOffset] regionB after periodic clip: " << clippedB.size()
            << " polygons" << std::endl;
        if (!clippedB.empty()) {
            BooleanOperations mergeOpB;
            for (auto& arcs : clippedB) {
                mergeOpB.AddSubjectPolygon(arcs);
            }
            regionB_processed = mergeOpB.ExecuteOnlySubjectPatternAnnotated(
                static_cast<const IFillType*>(nullptr));
        }
    }

    std::cout << "[CylOffset] regionB_processed after clip+merge: "
        << regionB_processed.size() << " annotated polygons" << std::endl;

    // ===== Step 4: C = A ± B_processed（距离 > 0 则并集，< 0 则差集）=====
    std::vector<std::vector<ArcType>> C_raw;
    {
        if (distance > 0) {
            // A + B: Subject=A, Clip=B_processed
            BooleanOperations boolOp2;
            for (auto& poly : m_mergedAnnotated) {
                boolOp2.AddSubjectPolygon(poly.arcs);
            }
            for (auto& polyB : regionB_processed) {
                boolOp2.AddClipPolygon(polyB.arcs);
            }
            C_raw = boolOp2.Execute(BooleanOperation::Union,
                static_cast<const IFillType*>(nullptr),
                static_cast<const IFillType*>(nullptr));
        } else if (distance < 0) {
            // A - B: Subject=B_processed (偏置区域), Clip=A (原区域)
            // 因为 B 是向外偏置的膨胀区域，B - A 得到环形扩张带
            // 后续管线（周期裁剪→合并→圆柱区域）会处理成正确的缩小效果
            BooleanOperations boolOp2;
            for (auto& polyB : regionB_processed) {
                boolOp2.AddSubjectPolygon(polyB.arcs);
            }
            for (auto& poly : m_mergedAnnotated) {
                boolOp2.AddClipPolygon(poly.arcs);
            }
            C_raw = boolOp2.Execute(BooleanOperation::Difference,
                static_cast<const IFillType*>(nullptr),
                static_cast<const IFillType*>(nullptr));
        }
        // distance == 0 → C_raw 为空（保持不变）
    }

    std::cout << "[CylOffset] Boolean result (C): " << C_raw.size()
        << " polygons (A " << (distance > 0 ? "+" : (distance < 0 ? "-" : "="))
        << " B)" << std::endl;

    // 转换布尔结果为 OffsetResultPolygon（View: A±B）
    QVector<Sketch2DView::OffsetResultPolygon> booleanResults;

    // 先叠加原始圆柱区域边界（只描边，不填充）
    {
        const QColor kOriginalBoundaryColor(150, 155, 165);
        for (const auto& loop : boundaryLoops) {
            size_t m = loop.size();
            if (m < 2) continue;
            Sketch2DView::OffsetResultPolygon edgePoly;
            edgePoly.color = kOriginalBoundaryColor;
            edgePoly.isHole = false;
            edgePoly.isOpen = true;
            for (size_t j = 0; j < m; ++j) {
                const auto& arc = loop[j];
                Sketch2DView::PolygonVertex vertex;
                vertex.point = QPointF(arc.Point0().x, arc.Point0().y);
                vertex.bulge = arc.Bulge();
                edgePoly.vertices.append(vertex);
                edgePoly.edgeSegmentIds.append(arc.Data().segmentId);
                edgePoly.edgeSourceEdgeIds.append(arc.Data().sourceEdgeId);
                edgePoly.edgeTags.append(arc.Data().edgeTag);
                edgePoly.edgeConvexJoinVertices.append(
                    arc.Data().edgeTag == 1
                    ? QPointF(arc.Data().convexJoinVertexX, arc.Data().convexJoinVertexY)
                    : QPointF());
            }
            {
                const auto& lastArc = loop.back();
                Sketch2DView::PolygonVertex endVertex;
                endVertex.point = QPointF(lastArc.Point1().x, lastArc.Point1().y);
                endVertex.bulge = 0.0;
                edgePoly.vertices.append(endVertex);
                edgePoly.edgeSegmentIds.append(0);
                edgePoly.edgeSourceEdgeIds.append(0);
                edgePoly.edgeTags.append(0);
                edgePoly.edgeConvexJoinVertices.append(QPointF());
            }
            booleanResults.append(edgePoly);
        }
    }

    {
        const std::vector<QColor> kBoolColors = {
            QColor(255, 200, 60), QColor(240, 180, 40),
            QColor(230, 160, 30), QColor(220, 140, 20)
        };
        for (size_t i = 0; i < C_raw.size(); ++i) {
            booleanResults.append(
                arcsToPolygon(C_raw[i], kBoolColors[i % kBoolColors.size()]));
        }
    }

    // ===== Step 5: C_raw → 周期裁剪 → 合并 → 圆柱区域 =====
    QVector<Sketch2DView::OffsetResultPolygon> finalResults;
    if (!C_raw.empty()) {
        auto clipped = PeriodicClipper::ClipToStrip(C_raw, bLeft, bRight);

        std::vector<AnnotatedPolygon> finalAnnotated;
        if (!clipped.empty()) {
            BooleanOperations mergeOp3;
            for (auto& arcs : clipped) {
                mergeOp3.AddSubjectPolygon(arcs);
            }
            finalAnnotated = mergeOp3.ExecuteOnlySubjectPatternAnnotated(
                static_cast<const IFillType*>(nullptr));
        }

        if (!finalAnnotated.empty()) {
            m_offsetCylindricalAreaTree =
                BuildCylindricalAreas(finalAnnotated, bLeft, bRight);

            m_offsetResultFillCount = static_cast<int>(finalAnnotated.size());
            m_offsetHighlightIndices.clear();
            m_offsetLoopEdgeCounts.clear();

            // 填充区域
            const std::vector<QColor> kFinalFillColors = {
                QColor(140, 220, 90), QColor(120, 200, 70),
                QColor(160, 240, 110), QColor(100, 180, 50)
            };
            for (size_t i = 0; i < finalAnnotated.size(); ++i) {
                finalResults.append(
                    arcsToPolygon(finalAnnotated[i].arcs,
                        kFinalFillColors[i % kFinalFillColors.size()]));
            }

            // ---- 填充多边形与区域指纹匹配 ----
            auto offsetArcFp = [](const std::vector<tailor_visualization::Arc>& arcs)
                -> std::set<int> {
                std::set<int> fp;
                for (const auto& arc : arcs) {
                    int sid = arc.Data().sourceEdgeId;
                    if (sid != 0) fp.insert(sid);
                }
                return fp;
            };
            auto offsetCollectFp = [&](const CylindricalArea& area) -> std::set<int> {
                std::set<int> ids;
                std::function<void(const CylindricalArea&)> go =
                    [&](const CylindricalArea& a) {
                    for (const auto& loop : a.boundary) {
                        for (const auto& arc : loop.arcs) {
                            int sid = arc.Data().sourceEdgeId;
                            if (sid != 0) ids.insert(sid);
                        }
                    }
                    for (const auto& child : a.children) go(child);
                };
                go(area);
                return ids;
            };

            // 顶层区域指纹→颜色→地址映射
            int offColorIdx = 0;
            const std::vector<QColor> kOffAreaColors = {
                QColor(100, 170, 60), QColor(80, 130, 40),
                QColor(120, 190, 80), QColor(60, 100, 30)
            };
            std::map<std::set<int>, QColor> offFpColorMap;
            std::map<std::set<int>, const CylindricalArea*> offFpToArea;
            for (const auto& area : m_offsetCylindricalAreaTree) {
                auto fp = offsetCollectFp(area);
                if (offFpColorMap.find(fp) == offFpColorMap.end()) {
                    offFpColorMap[fp] =
                        kOffAreaColors[offColorIdx++ % kOffAreaColors.size()];
                    offFpToArea[fp] = &area;
                }
            }

            // 匹配每个填充多边形到区域
            std::vector<const CylindricalArea*> offPolyToArea(
                finalAnnotated.size(), nullptr);
            for (size_t pi = 0; pi < finalAnnotated.size(); ++pi) {
                auto pFp = offsetArcFp(finalAnnotated[pi].arcs);
                size_t bestOverlap = 0;
                for (const auto& [aFp, aColor] : offFpColorMap) {
                    size_t overlap = 0;
                    for (int id : pFp) {
                        if (aFp.count(id)) ++overlap;
                    }
                    if (overlap > bestOverlap) {
                        bestOverlap = overlap;
                        offPolyToArea[pi] = offFpToArea[aFp];
                    }
                }
            }

            // 将填充索引记录到对应区域
            for (size_t pi = 0; pi < finalAnnotated.size(); ++pi) {
                if (offPolyToArea[pi]) {
                    m_offsetHighlightIndices[offPolyToArea[pi]].prepend(
                        static_cast<int>(pi));
                }
            }

            // 边界描边：从圆柱区域树提取，同时追踪高亮索引
            std::function<void(const std::vector<CylindricalArea>&, int)> renderEdges;
            renderEdges = [&](const std::vector<CylindricalArea>& areas, int depth) {
                for (const auto& area : areas) {
                    auto areaFp = offsetCollectFp(area);
                    auto colorIt = offFpColorMap.find(areaFp);
                    QColor areaColor = (colorIt != offFpColorMap.end())
                        ? colorIt->second : kOffAreaColors[0];

                    QVector<int>& areaHighlights =
                        m_offsetHighlightIndices[&area]; // auto-create
                    std::vector<int>& loopEdgeCounts =
                        m_offsetLoopEdgeCounts[&area];   // auto-create

                    for (const auto& loop : area.boundary) {
                        if (loop.arcs.empty()) {
                            loopEdgeCounts.push_back(0);
                            continue;
                        }

                        loopEdgeCounts.push_back(1);
                        int edgeIdx = static_cast<int>(finalResults.size());
                        areaHighlights.append(edgeIdx);

                        Sketch2DView::OffsetResultPolygon edgePoly;
                        edgePoly.color = areaColor.darker(120);
                        edgePoly.isHole = (depth % 2 == 1);
                        edgePoly.isOpen = true;

                        for (const auto& arc : loop.arcs) {
                            Sketch2DView::PolygonVertex vertex;
                            vertex.point = QPointF(arc.Point0().x, arc.Point0().y);
                            vertex.bulge = arc.Bulge();
                            edgePoly.vertices.append(vertex);
                            edgePoly.edgeSegmentIds.append(arc.Data().segmentId);
                            edgePoly.edgeSourceEdgeIds.append(arc.Data().sourceEdgeId);
                            edgePoly.edgeTags.append(arc.Data().edgeTag);
                            edgePoly.edgeConvexJoinVertices.append(
                                arc.Data().edgeTag == 1
                                ? QPointF(arc.Data().convexJoinVertexX,
                                    arc.Data().convexJoinVertexY)
                                : QPointF());
                        }
                        {
                            int n0 = static_cast<int>(loop.arcs.size());
                            const auto& lastArc = loop.arcs[n0 - 1];
                            Sketch2DView::PolygonVertex endVertex;
                            endVertex.point = QPointF(
                                lastArc.Point1().x, lastArc.Point1().y);
                            endVertex.bulge = 0.0;
                            edgePoly.vertices.append(endVertex);
                            edgePoly.edgeSegmentIds.append(0);
                            edgePoly.edgeSourceEdgeIds.append(0);
                            edgePoly.edgeTags.append(0);
                            edgePoly.edgeConvexJoinVertices.append(QPointF());
                        }
                        finalResults.append(std::move(edgePoly));
                    }
                    renderEdges(area.children, depth + 1);
                }
            };
            renderEdges(m_offsetCylindricalAreaTree, 0);
        } else {
            m_offsetCylindricalAreaTree.clear();
            m_offsetResultFillCount = 0;
            m_offsetHighlightIndices.clear();
            m_offsetLoopEdgeCounts.clear();
        }
    }

    std::cout << "[CylOffset] Final: " << finalResults.size()
        << " entries" << std::endl;

    emit cylindricalOffsetResultReady(
        offsetBoundaryResults, booleanResults, finalResults);
    emit regionTreeUpdated();
}