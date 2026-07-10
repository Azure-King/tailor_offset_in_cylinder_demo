#include "CylindricalRegion.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>

namespace tailor_visualization {

// ============================================================================
// 内部辅助函数
// ============================================================================

/// 判断点是否在左边界上
static bool onLeftBoundary(double x, double left, double eps) {
    return std::abs(x - left) < eps;
}

/// 判断点是否在右边界上
static bool onRightBoundary(double x, double right, double eps) {
    return std::abs(x - right) < eps;
}

/// 判断一条边是否完全在左边界或右边界上（竖直边界段）
/// 这类边是裁剪矩形产生的，不构成非收缩环的真正几何内容
static bool isPurelyBoundaryEdge(const Arc& arc, double left, double right, double eps) {
    double x0 = arc.Point0().x;
    double x1 = arc.Point1().x;
    bool onLeft = onLeftBoundary(x0, left, eps) && onLeftBoundary(x1, left, eps);
    bool onRight = onRightBoundary(x0, right, eps) && onRightBoundary(x1, right, eps);
    return onLeft || onRight;
}

/// 判断多边形是否触碰边界（至少一个顶点在左右边界上）
static bool touchesBoundary(const std::vector<Arc>& poly,
                            double left, double right, double eps) {
    for (const auto& arc : poly) {
        double x0 = arc.Point0().x;
        double x1 = arc.Point1().x;
        if (onLeftBoundary(x0, left, eps) || onLeftBoundary(x1, left, eps) ||
            onRightBoundary(x0, right, eps) || onRightBoundary(x1, right, eps)) {
            return true;
        }
    }
    return false;
}

/// 环的方向：计算带符号面积（shoelace formula，含 bulge 修正）
static double signedArea(const std::vector<Arc>& arcs) {
    if (arcs.empty()) return 0.0;
    double area = 0.0;
    for (const auto& arc : arcs) {
        double x0 = arc.Point0().x, y0 = arc.Point0().y;
        double x1 = arc.Point1().x, y1 = arc.Point1().y;
        area += x0 * y1 - x1 * y0;

        // bulge 贡献的扇形面积近似
        double bulge = arc.Bulge();
        if (std::abs(bulge) > 1e-10) {
            double b2 = 0.5 * (1.0 / bulge - bulge);
            double cx = 0.5 * (x0 + x1 - b2 * (y1 - y0));
            double cy = 0.5 * (y0 + y1 + b2 * (x1 - x0));
            double dx0 = x0 - cx, dy0 = y0 - cy;
            double dx1 = x1 - cx, dy1 = y1 - cy;
            double R2 = dx0 * dx0 + dy0 * dy0;
            double angle = 4.0 * std::atan(std::abs(bulge));
            int sign = (bulge > 0) ? 1 : -1;
            area += sign * angle * R2;  // 扇形面积 = 0.5 * angle * R^2, 但 shoelace 需要 *2
        }
    }
    return area * 0.5;
}

// ============================================================================
// CylindricalArea 成员函数实现
// ============================================================================

std::pair<double, double> CylindricalArea::yRange() const {
    if (boundary.empty()) return { 0.0, 0.0 };
    double yMin = std::numeric_limits<double>::max();
    double yMax = std::numeric_limits<double>::lowest();
    for (const auto& loop : boundary) {
        auto [lyMin, lyMax] = loop.yRange();
        yMin = std::min(yMin, lyMin);
        yMax = std::max(yMax, lyMax);
    }
    if (yMin > yMax) return { 0.0, 0.0 };
    return { yMin, yMax };
}

bool CylindricalArea::containsPoint(double x, double y,
                                     double boundaryLeft, double boundaryRight,
                                     double eps) const {
    if (!isValid()) return false;

    // X 必须在条带内
    if (x < boundaryLeft - eps || x > boundaryRight + eps) return false;

    if (isContractibleArea()) {
        // ---- 可缩区域：标准 winding number ----
        const auto& arcs = boundary[0].arcs;
        int winding = 0;
        for (const auto& arc : arcs) {
            double x0 = arc.Point0().x, y0 = arc.Point0().y;
            double x1 = arc.Point1().x, y1 = arc.Point1().y;

            // 射线法：从 (x, y) 向右发射水平射线，统计穿越次数
            if (y0 <= y && y1 > y) {
                // 向上穿越
                double cross = (x1 - x0) * (y - y0) - (x - x0) * (y1 - y0);
                if (cross > 0) ++winding;
            } else if (y1 <= y && y0 > y) {
                // 向下穿越
                double cross = (x1 - x0) * (y - y0) - (x - x0) * (y1 - y0);
                if (cross < 0) --winding;
            }
        }
        return winding != 0;
    }

    if (isBand()) {
        // ---- 条带区域：点在上下边界之间 ----
        // 找出上边界（right→left）和下边界（left→right）
        const CylindricalLoop* upper = nullptr;
        const CylindricalLoop* lower = nullptr;

        for (const auto& loop : boundary) {
            if (loop.leftToRight) {
                lower = &loop;   // left→right = 下边界
            } else {
                upper = &loop;   // right→left = 上边界
            }
        }

        if (!upper || !lower) return false;

        // 在条带内的每个 x 位置检查 y 是否在上下边界之间
        // 近似方法：用上下边界的 Y 范围做粗判
        auto [upperYMin, upperYMax] = upper->yRange();
        auto [lowerYMin, lowerYMax] = lower->yRange();

        // 如果点在下边界下方或上边界上方，肯定不在区域内
        if (y < lowerYMin - eps || y > upperYMax + eps) return false;

        // 更精确的判断：沿该 x 处水平发射射线，检查与上下边界的交点
        // 简化处理：用 Y 中值做代理
        double upperYAtX = (upperYMin + upperYMax) * 0.5;
        double lowerYAtX = (lowerYMin + lowerYMax) * 0.5;

        // 确保 upper 的 Y 大于 lower
        if (upperYAtX < lowerYAtX) std::swap(upperYAtX, lowerYAtX);

        return (y >= lowerYAtX - eps && y <= upperYAtX + eps);
    }

    return false;
}

void CylindricalArea::traverse(const AreaVisitor& visitor, int depth) const {
    if (!visitor(*this, depth)) return;
    for (const auto& child : children) {
        child.traverse(visitor, depth + 1);
    }
}

int CylindricalArea::totalCount() const {
    int count = 1;
    for (const auto& child : children) {
        count += child.totalCount();
    }
    return count;
}


// ============================================================================
// 从条带多边形提取非收缩路径
// ============================================================================

/// 描述一段非边界路径的连接信息
struct PathConnection {
    size_t startIdx;    // 在原始多边形中的起始弧段索引
    size_t length;      // 弧段数量
    bool startsOnLeft;  // 起点在左边界
    bool endsOnLeft;    // 终点在左边界

    double startY() const;  // 需要 arcs 引用，在 lambda 中定义
    double endY() const;
};

/// 从触碰边界的多边形中提取非边界路径段
///
/// 步骤：
///   1. 标记哪些弧段是纯边界段（整条边在 x=left 或 x=right 上）
///   2. 提取连续的非边界弧段组（run）
///   3. 根据每组首尾端点的边界位置确定方向
///
/// @return 提取到的 CylindricalLoop 列表（均为 isContractible==false）
///
static std::vector<CylindricalLoop> extractNonBoundaryPaths(
    const std::vector<Arc>& poly,
    double left, double right, double eps) {

    const size_t n = poly.size();
    if (n < 2) return {};

    // Step 1: 标记纯边界段
    std::vector<bool> isBdr(n);
    for (size_t i = 0; i < n; ++i) {
        isBdr[i] = isPurelyBoundaryEdge(poly[i], left, right, eps);
    }

    // Step 2: 找到非边界弧段的连续运行（run）
    struct Run { size_t start; size_t len; };
    std::vector<Run> runs;

    for (size_t i = 0; i < n; ) {
        if (!isBdr[i]) {
            size_t start = i;
            size_t len = 0;
            while (i < n && !isBdr[i]) { ++len; ++i; }
            runs.push_back({ start, len });
        } else {
            ++i;
        }
    }

    // 处理首尾相接的情况（多边形闭合，尾部非边界段与头部非边界段相连）
    if (runs.size() >= 2
        && runs[0].start == 0
        && runs.back().start + runs.back().len == n) {
        // 合并首尾
        runs[0].start = runs.back().start;
        runs[0].len += runs.back().len;
        runs.pop_back();
    }

    // Step 3: 将每个 run 转换为 CylindricalLoop
    std::vector<CylindricalLoop> paths;
    for (const auto& run : runs) {
        CylindricalLoop loop;
        loop.isContractible = false;
        loop.arcs.reserve(run.len);
        for (size_t j = 0; j < run.len; ++j) {
            loop.arcs.push_back(poly[(run.start + j) % n]);
        }

        // 确定方向
        double startX = loop.startPoint().x;
        double endX = loop.endPoint().x;

        bool startOnLeft = onLeftBoundary(startX, left, eps);
        bool endOnLeft = onLeftBoundary(endX, left, eps);
        bool startOnRight = onRightBoundary(startX, right, eps);
        bool endOnRight = onRightBoundary(endX, right, eps);

        if (startOnLeft && endOnRight) {
            loop.leftToRight = true;   // left→right: 下边界
        } else if (startOnRight && endOnLeft) {
            loop.leftToRight = false;  // right→left: 上边界
        } else if (startOnLeft && endOnLeft) {
            // 两端都在左边界 → 特殊情况（触碰同一边界）
            // 按 Y 判定：高 Y → 视为从右来（上边界），低 Y → 视为从右去（下边界）
            loop.leftToRight = (loop.startPoint().y < loop.endPoint().y);
        } else if (startOnRight && endOnRight) {
            // 两端都在右边界
            loop.leftToRight = (loop.startPoint().y > loop.endPoint().y);
        } else {
            // 未触碰边界的段 → 可能是内部的洞，标记为可缩
            loop.isContractible = true;
        }

        paths.push_back(std::move(loop));
    }

    return paths;
}



// ============================================================================
// 通过几何位置匹配左右边界上的端点
// ============================================================================

/// 从非收缩路径集合中构建条带区域
///
/// 当有多个非边界段时，需要按 Y 坐标将左右边界上的端点配对。
/// 配对规则：Y 坐标最接近的左右端点配对。
///
/// @param paths 从多边形提取的非边界路径
/// @return 条带区域列表
///
static std::vector<CylindricalArea> buildBandAreas(
    std::vector<CylindricalLoop>& paths,
    double left, double right, double eps) {

    std::vector<CylindricalArea> areas;

    // 分离可缩环（未触碰边界的段）
    std::vector<CylindricalLoop> contractibleLoops;
    std::vector<CylindricalLoop> nonContractPaths;

    for (auto& p : paths) {
        if (p.isContractible) {
            contractibleLoops.push_back(std::move(p));
        } else {
            nonContractPaths.push_back(std::move(p));
        }
    }

    // 将可缩环包装为 contractible areas
    for (auto& cl : contractibleLoops) {
        CylindricalArea area;
        area.boundary.push_back(std::move(cl));
        areas.push_back(std::move(area));
    }

    if (nonContractPaths.empty()) return areas;

    // ---- 情况 1：恰好 2 段 → 一个条带区域 ----
    if (nonContractPaths.size() == 2) {
        // 确保 boundary[0] 是上边界 (right→left)，boundary[1] 是下边界 (left→right)
        CylindricalLoop& a = nonContractPaths[0];
        CylindricalLoop& b = nonContractPaths[1];

        CylindricalArea band;
        if (!a.leftToRight && b.leftToRight) {
            // a = upper (right→left), b = lower (left→right)
            band.boundary.push_back(std::move(a));
            band.boundary.push_back(std::move(b));
        } else if (!b.leftToRight && a.leftToRight) {
            // b = upper, a = lower
            band.boundary.push_back(std::move(b));
            band.boundary.push_back(std::move(a));
        } else {
            // 方向一致 → 用 Y 排序
            auto aY = a.yRange();
            auto bY = b.yRange();
            // Y 值大的作为上边界 (right→left 方向)
            bool aIsUpper = aY.first > bY.first;
            if (!aIsUpper) std::swap(a, b);
            // 强制方向
            a.leftToRight = false;  // upper: right→left
            b.leftToRight = true;   // lower: left→right
            band.boundary.push_back(std::move(a));
            band.boundary.push_back(std::move(b));
        }
        areas.push_back(std::move(band));
        return areas;
    }

    // ---- 情况 2：多于 2 段 → 分离上/下边界，按类别配对 ----
    //
    // 核心规则：上边界路径 (right→left) 只能与下边界路径 (left→right) 配对。
    // 每个 band 由一条上边界 + 一条下边界组成。
    //
    // 先按端点位置确定每条路径的类别，然后按 Y 排序后一一配对。
    //
    std::vector<CylindricalLoop> upperPaths;  // right→left
    std::vector<CylindricalLoop> lowerPaths;  // left→right
    std::vector<CylindricalLoop> unknown;     // 无法确定方向的

    for (auto& p : nonContractPaths) {
        if (p.arcs.empty()) continue;
        double sx = p.startPoint().x;
        double ex = p.endPoint().x;

        if (onRightBoundary(sx, right, eps) && onLeftBoundary(ex, left, eps)) {
            // right→left: 上边界
            p.leftToRight = false;
            upperPaths.push_back(std::move(p));
        } else if (onLeftBoundary(sx, left, eps) && onRightBoundary(ex, right, eps)) {
            // left→right: 下边界
            p.leftToRight = true;
            lowerPaths.push_back(std::move(p));
        } else {
            // 无法确定方向（两端都在同侧或内部）
            // 用 Y 陡度判断：如果起点 Y > 终点 Y，则为上边界
            if (onLeftBoundary(sx, left, eps) && onLeftBoundary(ex, left, eps)) {
                // 两端都在左边界 → 按 Y 判定
                p.leftToRight = (p.startPoint().y < p.endPoint().y);
                if (p.leftToRight) lowerPaths.push_back(std::move(p));
                else upperPaths.push_back(std::move(p));
            } else if (onRightBoundary(sx, right, eps) && onRightBoundary(ex, right, eps)) {
                // 两端都在右边界
                p.leftToRight = (p.startPoint().y > p.endPoint().y);
                if (p.leftToRight) lowerPaths.push_back(std::move(p));
                else upperPaths.push_back(std::move(p));
            } else {
                unknown.push_back(std::move(p));
            }
        }
    }

    // 处理无法确定方向的路径 → 当作可缩环
    for (auto& p : unknown) {
        p.isContractible = true;
        CylindricalArea area;
        area.boundary.push_back(std::move(p));
        areas.push_back(std::move(area));
    }

    // 按 Y 范围中点排序，使配对的上下边界在 Y 轴上对齐
    auto sortByYMid = [](std::vector<CylindricalLoop>& paths) {
        std::sort(paths.begin(), paths.end(),
            [](const CylindricalLoop& a, const CylindricalLoop& b) {
                double aMid = (a.yRange().first + a.yRange().second) * 0.5;
                double bMid = (b.yRange().first + b.yRange().second) * 0.5;
                return aMid < bMid;
            });
    };
    sortByYMid(upperPaths);
    sortByYMid(lowerPaths);

    // 上边界与下边界按 Y 顺序一一配对，组成 band 区域
    size_t pairCount = std::min(upperPaths.size(), lowerPaths.size());
    for (size_t i = 0; i < pairCount; ++i) {
        CylindricalArea band;
        band.boundary.push_back(std::move(upperPaths[i]));  // boundary[0] = 上边界
        band.boundary.push_back(std::move(lowerPaths[i]));  // boundary[1] = 下边界
        if (band.isValid()) {
            areas.push_back(std::move(band));
        }
    }

    // 剩余未配对的路径 → 当作可缩环（退化情况）
    for (size_t i = pairCount; i < upperPaths.size(); ++i) {
        upperPaths[i].isContractible = true;
        CylindricalArea area;
        area.boundary.push_back(std::move(upperPaths[i]));
        areas.push_back(std::move(area));
    }
    for (size_t i = pairCount; i < lowerPaths.size(); ++i) {
        lowerPaths[i].isContractible = true;
        CylindricalArea area;
        area.boundary.push_back(std::move(lowerPaths[i]));
        areas.push_back(std::move(area));
    }

    return areas;
}


// ============================================================================
// Builder 函数
// ============================================================================

/// 判断一个 contractible area 是否在 band area 内部
static bool contractibleInsideBand(
    const CylindricalArea& contractible,
    const CylindricalArea& band,
    double left, double right, double eps) {

    if (!contractible.isContractibleArea() || !band.isBand()) return false;
    if (contractible.boundary[0].arcs.empty()) return false;

    // 取 contractible 区域的第一个顶点作为采样点
    double x = contractible.boundary[0].arcs[0].Point0().x;
    double y = contractible.boundary[0].arcs[0].Point0().y;

    return band.containsPoint(x, y, left, right, eps);
}

/// 判断一个 contractible area 是否在另一个 contractible area 内部
static bool contractibleInsideContractible(
    const CylindricalArea& inner,
    const CylindricalArea& outer,
    double left, double right, double eps) {

    if (!inner.isContractibleArea() || !outer.isContractibleArea()) return false;
    if (inner.boundary[0].arcs.empty()) return false;

    double x = inner.boundary[0].arcs[0].Point0().x;
    double y = inner.boundary[0].arcs[0].Point0().y;

    return outer.containsPoint(x, y, left, right, eps);
}

std::vector<CylindricalArea> BuildCylindricalAreas(
    const std::vector<std::vector<Arc>>& polygons,
    double boundaryLeft, double boundaryRight,
    double eps) {

    std::vector<CylindricalArea> result;

    if (polygons.empty()) return result;

    // ====================================================================
    // Step 1: 分类多边形 → 内部多边形 vs 边界多边形
    // ====================================================================
    std::vector<const std::vector<Arc>*> interiorPolys;
    std::vector<const std::vector<Arc>*> boundaryPolys;

    for (const auto& poly : polygons) {
        if (poly.empty()) continue;
        if (touchesBoundary(poly, boundaryLeft, boundaryRight, eps)) {
            boundaryPolys.push_back(&poly);
        } else {
            interiorPolys.push_back(&poly);
        }
    }

    // ====================================================================
    // Step 2: 处理边界多边形 → 提取非边界路径 → 构建 band areas
    // ====================================================================
    std::vector<CylindricalArea> bandAreas;
    std::vector<CylindricalArea> orphanContractibles;  // 从边界多边形中提取出的可缩环

    for (const auto* polyPtr : boundaryPolys) {
        const auto& poly = *polyPtr;

        // 提取非边界路径段
        auto paths = extractNonBoundaryPaths(poly, boundaryLeft, boundaryRight, eps);

        if (paths.empty()) {
            // 整个多边形都在边界上（退化情况）→ 跳过
            continue;
        }

        // 分离可缩和非可缩路径
        bool hasNonContract = false;
        for (const auto& p : paths) {
            if (!p.isContractible) { hasNonContract = true; break; }
        }

        if (!hasNonContract) {
            // 所有路径都不触碰边界 → 都是可缩环
            for (auto& p : paths) {
                CylindricalArea area;
                area.boundary.push_back(std::move(p));
                orphanContractibles.push_back(std::move(area));
            }
            continue;
        }

        // 构建 band areas
        auto bands = buildBandAreas(paths, boundaryLeft, boundaryRight, eps);
        for (auto& area : bands) {
            if (area.isBand()) {
                bandAreas.push_back(std::move(area));
            } else if (area.isContractibleArea()) {
                orphanContractibles.push_back(std::move(area));
            }
        }
    }

    // ====================================================================
    // Step 3: 处理内部多边形 → 直接作为 contractible areas
    // ====================================================================
    std::vector<CylindricalArea> contractibleAreas;
    for (const auto* polyPtr : interiorPolys) {
        CylindricalLoop loop;
        loop.arcs = *polyPtr;
        loop.isContractible = true;

        CylindricalArea area;
        area.boundary.push_back(std::move(loop));
        contractibleAreas.push_back(std::move(area));
    }

    // 合并 orphan contractibles
    contractibleAreas.insert(contractibleAreas.end(),
        std::make_move_iterator(orphanContractibles.begin()),
        std::make_move_iterator(orphanContractibles.end()));

    // ====================================================================
    // Step 4: 构建嵌套树
    // ====================================================================

    // 4a: 将 contractible areas 分配到 band areas 内部
    std::vector<bool> contractibleAssigned(contractibleAreas.size(), false);

    for (size_t ci = 0; ci < contractibleAreas.size(); ++ci) {
        for (auto& ba : bandAreas) {
            if (contractibleInsideBand(contractibleAreas[ci], ba,
                                       boundaryLeft, boundaryRight, eps)) {
                ba.children.push_back(std::move(contractibleAreas[ci]));
                contractibleAssigned[ci] = true;
                break;
            }
        }
    }

    // 4b: 未被分配到 band 的 contractible areas → 顶层
    for (size_t ci = 0; ci < contractibleAreas.size(); ++ci) {
        if (!contractibleAssigned[ci]) {
            result.push_back(std::move(contractibleAreas[ci]));
        }
    }

    // 4c: 将 contractible areas 互相嵌套（标准 PolyTree 包含关系）
    //     使用类似于 PolyTree 的做法：偶数层=outer, 奇数层=hole
    //     简化：先展平所有顶层 contractible，再重新建立包含树
    //
    //     注意：此步骤处理的是顶层 contractible areas 之间的包含关系。
    //           bandAreas 内部的 contractible 不参与此步骤。
    //
    //     实现：按面积从大到小排序，大者包含小者。

    // 对顶层 contractible areas 按包围盒面积排序
    std::sort(result.begin(), result.end(),
        [](const CylindricalArea& a, const CylindricalArea& b) {
            auto [aYMin, aYMax] = a.yRange();
            auto [bYMin, bYMax] = b.yRange();
            double aH = aYMax - aYMin;
            double bH = bYMax - bYMin;
            if (aH != bH) return aH > bH;  // 大的在前
            // 用 signed area 做次级排序
            double aArea = std::abs(signedArea(a.boundary[0].arcs));
            double bArea = std::abs(signedArea(b.boundary[0].arcs));
            return aArea > bArea;
        });

    // 构建包含树
    std::vector<bool> assigned(result.size(), false);
    std::vector<CylindricalArea> finalResult;

    for (size_t i = 0; i < result.size(); ++i) {
        if (assigned[i]) continue;
        if (!result[i].isContractibleArea()) {
            finalResult.push_back(std::move(result[i]));
            continue;
        }

        // 尝试将较小的 contractible areas 嵌套到 result[i] 中
        for (size_t j = i + 1; j < result.size(); ++j) {
            if (assigned[j]) continue;
            if (!result[j].isContractibleArea()) continue;

            if (contractibleInsideContractible(result[j], result[i],
                                               boundaryLeft, boundaryRight, eps)) {
                result[i].children.push_back(std::move(result[j]));
                assigned[j] = true;
            }
        }
        finalResult.push_back(std::move(result[i]));
    }

    // 4d: 对 band areas 内部的 contractible children 处理嵌套，并加入最终结果
    for (auto& ba : bandAreas) {
        if (ba.children.size() > 1) {
            // 按面积排序
            std::sort(ba.children.begin(), ba.children.end(),
                [](const CylindricalArea& a, const CylindricalArea& b) {
                    double aArea = std::abs(signedArea(a.boundary[0].arcs));
                    double bArea = std::abs(signedArea(b.boundary[0].arcs));
                    return aArea > bArea;
                });

            std::vector<bool> childAssigned(ba.children.size(), false);
            std::vector<CylindricalArea> nestedChildren;

            for (size_t i = 0; i < ba.children.size(); ++i) {
                if (childAssigned[i]) continue;
                for (size_t j = i + 1; j < ba.children.size(); ++j) {
                    if (childAssigned[j]) continue;
                    if (contractibleInsideContractible(ba.children[j], ba.children[i],
                                                       boundaryLeft, boundaryRight, eps)) {
                        ba.children[i].children.push_back(std::move(ba.children[j]));
                        childAssigned[j] = true;
                    }
                }
                nestedChildren.push_back(std::move(ba.children[i]));
            }
            ba.children = std::move(nestedChildren);
        }

        finalResult.push_back(std::move(ba));
    }

    return finalResult;
}


// ============================================================================
// FlattenCylindricalAreas: 树展平
// ============================================================================

void FlattenCylindricalAreas(
    const std::vector<CylindricalArea>& areas,
    std::vector<FlattenedRegion>& out,
    int depth) {

    // 预定义颜色调色板
    static const std::vector<QRgba64> kColors = {
        QRgba64::fromArgb32(0xFFFF6464),  // 红色
        QRgba64::fromArgb32(0xFF64C864),  // 绿色
        QRgba64::fromArgb32(0xFF6464FF),  // 蓝色
        QRgba64::fromArgb32(0xFFFFC864),  // 橙色
        QRgba64::fromArgb32(0xFFC864FF),  // 紫色
        QRgba64::fromArgb32(0xFF64FFC8),  // 青色
        QRgba64::fromArgb32(0xFFFF9664),  // 橙红
        QRgba64::fromArgb32(0xFF9664FF),  // 紫蓝
    };

    for (const auto& area : areas) {
        // 为每个 loop 导出
        for (size_t li = 0; li < area.boundary.size(); ++li) {
            const auto& loop = area.boundary[li];
            FlattenedRegion region;
            region.arcs = loop.arcs;
            region.depth = depth;
            region.isContractible = loop.isContractible;

            // 偶数层 = outer, 奇数层 = hole
            region.isHole = (depth % 2 == 1);

            region.color = kColors[(depth + li) % kColors.size()];
            out.push_back(std::move(region));
        }

        // 递归子区域
        FlattenCylindricalAreas(area.children, out, depth + 1);
    }
}

} // namespace tailor_visualization
