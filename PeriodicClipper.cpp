#include "PeriodicClipper.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <iostream>

namespace tailor_visualization {

void PeriodicClipper::ComputeBounds(
    const std::vector<std::vector<Arc>>& polygons,
    double& minX, double& maxX,
    double& minY, double& maxY)
{
    minX = std::numeric_limits<double>::max();
    maxX = std::numeric_limits<double>::lowest();
    minY = std::numeric_limits<double>::max();
    maxY = std::numeric_limits<double>::lowest();

    for (const auto& poly : polygons) {
        for (const auto& arc : poly) {
            auto p0 = arc.Point0();
            auto p1 = arc.Point1();
            minX = std::min(minX, std::min(p0.x, p1.x));
            maxX = std::max(maxX, std::max(p0.x, p1.x));
            minY = std::min(minY, std::min(p0.y, p1.y));
            maxY = std::max(maxY, std::max(p0.y, p1.y));
        }
    }
}

std::vector<PeriodicClipper::Arc> PeriodicClipper::CreateRectPolygon(
    double left, double right, double bottom, double top)
{
    // 顺时针矩形: (left,bottom) → (right,bottom) → (right,top) → (left,top) → 闭合
    std::vector<Arc> rect;
    int segId = 0;

    rect.push_back(Arc(
        ArcPoint{left, bottom}, ArcPoint{right, bottom},
        0.0, ArcUserData(QRgba64(), segId++)));

    rect.push_back(Arc(
        ArcPoint{right, bottom}, ArcPoint{right, top},
        0.0, ArcUserData(QRgba64(), segId++)));

    rect.push_back(Arc(
        ArcPoint{right, top}, ArcPoint{left, top},
        0.0, ArcUserData(QRgba64(), segId++)));

    rect.push_back(Arc(
        ArcPoint{left, top}, ArcPoint{left, bottom},
        0.0, ArcUserData(QRgba64(), segId++)));

    return rect;
}

std::vector<PeriodicClipper::Arc> PeriodicClipper::ShiftPolygonX(
    const std::vector<Arc>& poly, double dx)
{
    std::vector<Arc> result;
    result.reserve(poly.size());
    for (const auto& arc : poly) {
        auto p0 = arc.Point0();
        auto p1 = arc.Point1();
        Arc shifted(
            ArcPoint{p0.x + dx, p0.y},
            ArcPoint{p1.x + dx, p1.y},
            arc.Bulge(),
            arc.Data());
        result.push_back(std::move(shifted));
    }
    return result;
}

std::vector<std::vector<PeriodicClipper::Arc>> PeriodicClipper::ClipToStrip(
    const std::vector<std::vector<Arc>>& inputPolygons,
    double boundaryLeft,
    double boundaryRight,
    int fillTypeIndex,
    int connectTypeIndex)
{
    if (inputPolygons.empty()) {
        return {};
    }

    double stripWidth = boundaryRight - boundaryLeft;
    if (stripWidth <= 0.0) {
        std::cerr << "[PeriodicClipper] Invalid strip width: " << stripWidth << std::endl;
        return {};
    }

    // Step 1: 计算所有多边形的包围盒
    double minX, maxX, minY, maxY;
    ComputeBounds(inputPolygons, minX, maxX, minY, maxY);

    // 对 Y 加一些 padding 确保裁剪矩形完全覆盖
    double yPadding = std::max(1.0, (maxY - minY) * 0.1);
    double clipBottom = minY - yPadding;
    double clipTop = maxY + yPadding;

    // Step 2: 确定需要处理的周期范围
    // period k: 平移 -k*stripWidth 后多边形与 [boundaryLeft, boundaryRight] 重叠
    int periodStart = static_cast<int>(std::floor((minX - boundaryRight) / stripWidth));
    int periodEnd   = static_cast<int>(std::floor((maxX - boundaryLeft) / stripWidth));

    // 确保至少覆盖一个周期（即使多边形完全在带内）
    if (periodStart > periodEnd) {
        periodStart = 0;
        periodEnd = 0;
    }

    // Step 3: 创建裁剪矩形
    auto clipRect = CreateRectPolygon(boundaryLeft, boundaryRight, clipBottom, clipTop);

    // Step 4: 准备 FillType 和 ConnectType
    const IFillType* fillType = nullptr;
    EvenOddFillTypeWrapper evenOdd;
    NonZeroFillTypeWrapper nonZero;
    IgnoreFillTypeWrapper ignore;
    PositiveWindFillTypeWrapper positive;
    SpecificWindingFillTypeWrapper winding1(1);

    switch (fillTypeIndex) {
    case 0: fillType = &nonZero; break;
    case 1: fillType = &evenOdd; break;
    case 2: fillType = &ignore; break;
    case 3: fillType = &positive; break;
    case 4: fillType = &winding1; break;
    default: fillType = &evenOdd; break;
    }

    using Drafting = typename ArcTailor::PatternDrafting;
    ConnectTypeOuterFirstWrapper<Drafting> outerFirst;
    ConnectTypeInnerFirstWrapper<Drafting> innerFirst;
    const IConnectType<Drafting>* connectType =
        (connectTypeIndex == 1)
        ? static_cast<const IConnectType<Drafting>*>(&innerFirst)
        : static_cast<const IConnectType<Drafting>*>(&outerFirst);

    // Step 5: 对每个周期，平移多边形并执行布尔交集
    std::vector<std::vector<Arc>> allResults;

    // 限制最大周期数，防止极端情况
    const int maxPeriods = 20;
    int periodCount = periodEnd - periodStart + 1;
    if (periodCount > maxPeriods) {
        std::cerr << "[PeriodicClipper] Too many periods (" << periodCount
                  << "), clamping to " << maxPeriods << std::endl;
        periodEnd = periodStart + maxPeriods - 1;
    }

    for (int k = periodStart; k <= periodEnd; ++k) {
        double shiftAmount = -static_cast<double>(k) * stripWidth;

        BooleanOperations boolOp;
        boolOp.AddClipPolygon(clipRect);

        bool hasData = false;
        for (const auto& poly : inputPolygons) {
            auto shifted = ShiftPolygonX(poly, shiftAmount);
            if (!shifted.empty()) {
                boolOp.AddSubjectPolygon(shifted);
                hasData = true;
            }
        }

        if (!hasData) continue;

        // 执行布尔交集
        auto results = RetryTailorExecute([&]() {
            return boolOp.Execute(
                BooleanOperation::Intersection,
                fillType,   // clipFillType
                fillType,   // subjectFillType
                connectType);
        });

        for (auto& arcs : results) {
            if (!arcs.empty()) {
                allResults.push_back(std::move(arcs));
            }
        }
    }

    return allResults;
}

} // namespace tailor_visualization
