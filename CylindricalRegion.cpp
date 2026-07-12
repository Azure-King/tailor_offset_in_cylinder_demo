#include "CylindricalRegion.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
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

/// 计算 contractible area 的总面积（所有边界 loop 面积之和）
static double contractibleAreaSize(const CylindricalArea& area) {
    double total = 0.0;
    for (const auto& loop : area.boundary) {
        total += std::abs(signedArea(loop.arcs));
    }
    return total;
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
// 曲线组 (CurveGroup): 从多边形边界边切开的一段连续非边界曲线
// ============================================================================

/// 曲线组的终点信息
struct SideY {
    int side;   // 0=none(内部), 1=left, 2=right
    double y;

    bool operator<(const SideY& o) const {
        if (side != o.side) return side < o.side;
        return y < o.y;
    }
    bool operator==(const SideY& o) const {
        return side == o.side && std::abs(y - o.y) < 1e-12;
    }
    bool operator!=(const SideY& o) const { return !(*this == o); }
};

/// 用于 map 查找的整数键：将 y 乘以 1e7 后取整，消除浮点精度误差
static constexpr double kIndexScale = 1e7;

struct SideYInt {
    int side;
    int64_t yInt;

    bool operator<(const SideYInt& o) const {
        if (side != o.side) return side < o.side;
        return yInt < o.yInt;
    }
};

inline SideYInt toSideYInt(const SideY& p) {
    return { p.side, static_cast<int64_t>(std::round(p.y * kIndexScale)) };
}

/// 圆柱面上两个位置是否等价：(bLeft, y) ≡ (bRight, y)
inline bool cylEquivalent(const SideY& a, const SideY& b) {
    if (a.side == 0 || b.side == 0) return false;
    if (a.side != b.side) {
        // 对侧同 Y → 圆柱面上同一点
        return std::abs(a.y - b.y) < 1e-12;
    }
    // 同侧同 Y → 同一点
    return a.side == b.side && std::abs(a.y - b.y) < 1e-12;
}

/// 判断两个 side 是否相同（物理上相同的边界线）
inline bool sameSide(int a, int b) { return a == b; }

/// 曲线组：一段切开口的非边界路径
struct CurveGroup {
    std::vector<Arc> arcs;
    bool isOuter;         // true=外环, false=内环(hole)，直接从 PolyTree depth 获取

    bool startIsUpper;    // 起点方向：Upper?（从 BoundaryType 获取）
    bool endIsUpper;      // 终点方向：Upper?（从 BoundaryType 获取）
    SideY startPos;       // 起点位置
    SideY endPos;         // 终点位置
    bool used = false;    // 是否已被合并
};

/// 从标注多边形中分割出曲线组
/// 步骤：标记边界边 → 连续非边界段为一组
/// 方向从 edgeTypes（BoundaryType）获取；内外环从 poly.isOuter 获取
static std::vector<CurveGroup> splitIntoCurveGroups(
    const AnnotatedPolygon& poly,
    double left, double right, double eps) {

    const size_t n = poly.arcs.size();
    if (n < 2) return {};

    // 标记纯边界段
    std::vector<bool> isBdr(n);
    for (size_t i = 0; i < n; ++i) {
        isBdr[i] = isPurelyBoundaryEdge(poly.arcs[i], left, right, eps);
    }

    // 找到非边界弧段的连续运行（run）
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

    // 处理首尾相接（闭合多边形）
    if (runs.size() >= 2
        && runs[0].start == 0
        && runs.back().start + runs.back().len == n) {
        runs[0].start = runs.back().start;
        runs[0].len += runs.back().len;
        runs.pop_back();
    }

    // 将每个 run 转换为 CurveGroup
    std::vector<CurveGroup> groups;
    for (const auto& run : runs) {
        CurveGroup g;
        g.isOuter = poly.isOuter;  // 从 PolyTree depth 获取，不用 signedArea
        g.arcs.reserve(run.len);
        for (size_t j = 0; j < run.len; ++j) {
            size_t idx = (run.start + j) % n;
            g.arcs.push_back(poly.arcs[idx]);
        }

        // 计算起点位置
        double sx = g.arcs.front().Point0().x;
        double sy = g.arcs.front().Point0().y;
        if (onLeftBoundary(sx, left, eps))
            g.startPos = { 1, sy };
        else if (onRightBoundary(sx, right, eps))
            g.startPos = { 2, sy };
        else
            g.startPos = { 0, sy };

        // 计算终点位置
        double ex = g.arcs.back().Point1().x;
        double ey = g.arcs.back().Point1().y;
        if (onLeftBoundary(ex, left, eps))
            g.endPos = { 1, ey };
        else if (onRightBoundary(ex, right, eps))
            g.endPos = { 2, ey };
        else
            g.endPos = { 0, ey };

        // 从 BoundaryType 判定方向（不用几何计算）
        // 起点方向 ← 第一条边，终点方向 ← 最后一条边
        auto edgeTypeToUpper = [](tailor::BoundaryType bt) -> bool {
            bool hasU = tailor::HasUpperBoundary(bt);
            bool hasL = tailor::HasLowerBoundary(bt);
            if (hasU && !hasL)  return true;
            if (hasL && !hasU)  return false;
            // 共轭边界或 Inside/Outside → 默认 Upper
            return true;
        };

        size_t firstIdx = run.start % n;
        size_t lastIdx  = (run.start + run.len - 1) % n;
        g.startIsUpper = edgeTypeToUpper(poly.edgeTypes[firstIdx]);
        g.endIsUpper   = edgeTypeToUpper(poly.edgeTypes[lastIdx]);

        groups.push_back(std::move(g));
    }

    return groups;
}


// ============================================================================
// 合并规则和环绕数判定
// ============================================================================

/// 连接规则：判断两个曲线组是否可以首尾相连
/// endGroup 的尾部 连接 startGroup 的首部
static bool canConnect(const CurveGroup& endGroup, const CurveGroup& startGroup) {
    // 必须是同级别：外环连外环，内环连内环
    if (endGroup.isOuter != startGroup.isOuter) return false;

    // 位置必须等价（圆柱面上同一点）
    if (!cylEquivalent(endGroup.endPos, startGroup.startPos)) return false;

    bool diffSide = !sameSide(endGroup.endPos.side, startGroup.startPos.side);

    if (diffSide) {
        // 不同边界线上：上连上，下连下
        return (endGroup.endIsUpper && startGroup.startIsUpper) ||
               (!endGroup.endIsUpper && !startGroup.startIsUpper);
    } else {
        // 同一边界线上：上连下，下连上
        return (endGroup.endIsUpper && !startGroup.startIsUpper) ||
               (!endGroup.endIsUpper && startGroup.startIsUpper);
    }
}

/// 用环绕数判定环是否为可缩环
/// 遍历本环上所有边：如果起始点在右边界的母线上，
///   如果起点方向为Upper，环绕数+1，否则环绕数-1。
///
/// @return 0=可缩环, +1=上不可缩环, -1=下不可缩环
static int computeWinding(const std::vector<CurveGroup*>& loop,
                          double right, double eps) {
    int winding = 0;
    for (auto* g : loop) {
        if (g->startPos.side == 1 || g->startPos.side == 2) {  // 起点在圆柱边界上
            if (g->startIsUpper)
                winding += 1;
            else
                winding -= 1;
        }
    }
    return winding;
}

/// 从曲线组构成的环创建 CylindricalArea
/// @param winding 环绕数: 0=可缩, +1=上band, -1=下band
static CylindricalArea buildAreaFromLoop(std::vector<CurveGroup>& loop,
                                          int winding,
                                          double left, double right, double eps) {
    CylindricalArea area;

    if (winding == 0) {
        // 可缩区域：每个 CurveGroup 单独作为一个 loop
        // 过滤掉左右边界上的竖线弧段（母线处融合，不应显示）
        for (auto& g : loop) {
            CylindricalLoop contractLoop;
            contractLoop.isContractible = true;
            for (auto& arc : g.arcs) {
                if (!isPurelyBoundaryEdge(arc, left, right, eps))
                    contractLoop.arcs.push_back(std::move(arc));
            }
            if (!contractLoop.arcs.empty())
                area.boundary.push_back(std::move(contractLoop));
        }
    } else {
        // Band区域：分离上下边界
        std::vector<Arc> upperArcs, lowerArcs;

        for (auto& g : loop) {
            if (g.startIsUpper) {
                for (auto& arc : g.arcs)
                    upperArcs.push_back(std::move(arc));
            } else {
                for (auto& arc : g.arcs)
                    lowerArcs.push_back(std::move(arc));
            }
        }

        if (!upperArcs.empty() && !lowerArcs.empty()) {
            CylindricalLoop upperLoop;
            upperLoop.isContractible = false;
            upperLoop.leftToRight = false;  // right→left = upper
            upperLoop.arcs = std::move(upperArcs);

            CylindricalLoop lowerLoop;
            lowerLoop.isContractible = false;
            lowerLoop.leftToRight = true;   // left→right = lower
            lowerLoop.arcs = std::move(lowerArcs);

            if (winding == -1) {
                // 下不可缩环：交换上下边界
                area.boundary.push_back(std::move(lowerLoop));
                area.boundary.push_back(std::move(upperLoop));
            } else {
                // +1 上不可缩环
                area.boundary.push_back(std::move(upperLoop));
                area.boundary.push_back(std::move(lowerLoop));
            }
        } else {
            // 退化：只有一种方向 → 作为可缩环
            CylindricalLoop contractLoop;
            contractLoop.isContractible = true;
            auto& src = upperArcs.empty() ? lowerArcs : upperArcs;
            contractLoop.arcs = std::move(src);
            area.boundary.push_back(std::move(contractLoop));
        }
    }

    return area;
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
    const std::vector<AnnotatedPolygon>& polygons,
    double boundaryLeft, double boundaryRight,
    double eps) {

    if (polygons.empty()) return {};

    // ====================================================================
    // Step 1: 找出所有周期并集后在两个边缘线上的边
    //         并按此将贴着分割线的多边形分割成曲线组
    // ====================================================================
    std::vector<CurveGroup> allGroups;       // 所有从边界多边形切出的曲线组
    std::vector<CylindricalArea> contractibleAreas; // 内部多边形（不触边）

    for (const auto& poly : polygons) {
        if (poly.arcs.empty()) continue;

        if (!touchesBoundary(poly.arcs, boundaryLeft, boundaryRight, eps)) {
            // 内部多边形：直接作为完整的可缩区域
            CylindricalLoop loop;
            loop.arcs = poly.arcs;
            loop.isContractible = true;

            CylindricalArea area;
            area.boundary.push_back(std::move(loop));
            contractibleAreas.push_back(std::move(area));
        } else {
            // 边界多边形：分割成曲线组（从 poly.isOuter 获取内外环，从 edgeTypes 获取方向）
            auto groups = splitIntoCurveGroups(
                poly, boundaryLeft, boundaryRight, eps);
            for (auto& g : groups) {
                allGroups.push_back(std::move(g));
            }
        }
    }

    if (allGroups.empty()) {
        // 没有边界多边形 → 只需要构建嵌套树
        std::vector<CylindricalArea> result;
        std::sort(contractibleAreas.begin(), contractibleAreas.end(),
            [](const CylindricalArea& a, const CylindricalArea& b) {
                double aArea = contractibleAreaSize(a);
                double bArea = contractibleAreaSize(b);
                return aArea > bArea;
            });
        std::vector<bool> assigned(contractibleAreas.size(), false);
        for (size_t i = 0; i < contractibleAreas.size(); ++i) {
            if (assigned[i]) continue;
            for (size_t j = i + 1; j < contractibleAreas.size(); ++j) {
                if (assigned[j]) continue;
                if (contractibleInsideContractible(contractibleAreas[j],
                    contractibleAreas[i], boundaryLeft, boundaryRight, eps)) {
                    contractibleAreas[i].children.push_back(
                        std::move(contractibleAreas[j]));
                    assigned[j] = true;
                }
            }
            result.push_back(std::move(contractibleAreas[i]));
        }
        return result;
    }

    // ====================================================================
    // Step 2: 统计曲线组的首尾点，按规则构建连接图
    // ====================================================================

    // 构建端点索引: SideYInt → 以该点为起点的曲线组索引
    // 使用整数键消除 double 精度误差导致 map 查找失败的问题
    std::map<SideYInt, std::vector<size_t>> startIndex;
    for (size_t i = 0; i < allGroups.size(); ++i) {
        if (allGroups[i].startPos.side != 0) {
            startIndex[toSideYInt(allGroups[i].startPos)].push_back(i);
        }
    }

    // 为每个曲线组找后继
    // nextGroup[i] = 后继曲线组索引，-1 表示无后继
    std::vector<int> nextGroup(allGroups.size(), -1);

    for (size_t i = 0; i < allGroups.size(); ++i) {
        const auto& eg = allGroups[i];
        if (eg.endPos.side == 0) continue;  // 终点在内部 → 不需要连接

        SideYInt matchKey = toSideYInt(eg.endPos);

        // 尝试对侧匹配: (side, y) → (3-side, y)
        {
            SideYInt oppositeKey = { 3 - eg.endPos.side, matchKey.yInt };
            auto it = startIndex.find(oppositeKey);
            if (it != startIndex.end()) {
                for (size_t cand : it->second) {
                    if (allGroups[cand].used) continue;
                    if (canConnect(eg, allGroups[cand])) {
                        nextGroup[i] = static_cast<int>(cand);
                        break;
                    }
                }
            }
        }

        // 若对侧无匹配，尝试同侧匹配
        if (nextGroup[i] < 0) {
            auto it = startIndex.find(matchKey);
            if (it != startIndex.end()) {
                for (size_t cand : it->second) {
                    if (allGroups[cand].used) continue;
                    if (canConnect(eg, allGroups[cand])) {
                        nextGroup[i] = static_cast<int>(cand);
                        break;
                    }
                }
            }
        }
    }

    // ====================================================================
    // Step 3: 沿连接图遍历形成环，分离可缩环和不可缩环
    //   - 可缩环 (winding==0): 直接构建 contractible area
    //   - 不可缩环 (winding!=0): 暂存，计算每个环的最大 Y 值，后续配对
    // ====================================================================
    struct NonContractLoop {
        std::vector<CurveGroup> groups;
        int winding;   // +1=上边界, -1=下边界
        double maxY;   // 该环上所有顶点的最大 Y 值（圆柱母线方向）
    };
    std::vector<NonContractLoop> ncLoops;

    for (size_t i = 0; i < allGroups.size(); ++i) {
        if (allGroups[i].used) continue;

        // 沿 nextGroup 链遍历，收集环的曲线组
        std::vector<CurveGroup*> loopPtrs;
        int cur = static_cast<int>(i);
        int loopStart = cur;

        do {
            allGroups[cur].used = true;
            loopPtrs.push_back(&allGroups[cur]);
            cur = nextGroup[cur];
        } while (cur >= 0 && cur != loopStart && !allGroups[cur].used);

        if (loopPtrs.empty()) continue;

        // 用环绕数判定环类型: 0=可缩, +1=上不可缩, -1=下不可缩
        int winding = computeWinding(loopPtrs, boundaryRight, eps);

        if (winding == 0) {
            // 可缩环 → 直接构建 contractible area
            std::vector<CurveGroup> loopCopy;
            loopCopy.reserve(loopPtrs.size());
            for (auto* gp : loopPtrs)
                loopCopy.push_back(std::move(*gp));
            contractibleAreas.push_back(buildAreaFromLoop(loopCopy, 0,
                boundaryLeft, boundaryRight, eps));
        } else {
            // 不可缩环 → 暂存，后续按 Y 值配对
            NonContractLoop ncl;
            ncl.winding = winding;
            ncl.groups.reserve(loopPtrs.size());
            for (auto* gp : loopPtrs)
                ncl.groups.push_back(std::move(*gp));

            // 计算该不可缩环上所有顶点的最大 Y 值（圆柱母线方向）
            ncl.maxY = -std::numeric_limits<double>::max();
            for (const auto& g : ncl.groups) {
                for (const auto& arc : g.arcs) {
                    ncl.maxY = std::max(ncl.maxY, arc.Point0().y);
                    ncl.maxY = std::max(ncl.maxY, arc.Point1().y);
                }
            }
            ncLoops.push_back(std::move(ncl));
        }
    }

    // ====================================================================
    // Step 4: 按最大 Y 值降序排列不可缩环，相邻配对形成条带区域
    //   排序后索引 [0,1] 为第1个band, [2,3] 为第2个band, ...
    //   每对中: 高Y环=上边界(right→left), 低Y环=下边界(left→right)
    // ====================================================================
    std::sort(ncLoops.begin(), ncLoops.end(),
        [](const NonContractLoop& a, const NonContractLoop& b) {
            return a.maxY > b.maxY;  // 降序：高Y在前
        });

    std::vector<CylindricalArea> bandAreas;

    for (size_t i = 0; i + 1 < ncLoops.size(); i += 2) {
        auto& upperNc = ncLoops[i];      // 较高 Y → 上边界
        auto& lowerNc = ncLoops[i + 1];  // 较低 Y → 下边界

        CylindricalArea area;

        // 上边界 (right→left, leftToRight=false)，每个 CurveGroup 单独为一个 loop
        // 过滤掉左右边界上的竖线弧段
        for (auto& g : upperNc.groups) {
            CylindricalLoop loop;
            loop.isContractible = false;
            loop.leftToRight = false;
            for (auto& arc : g.arcs) {
                if (!isPurelyBoundaryEdge(arc, boundaryLeft, boundaryRight, eps))
                    loop.arcs.push_back(std::move(arc));
            }
            if (!loop.arcs.empty())
                area.boundary.push_back(std::move(loop));
        }

        // 下边界 (left→right, leftToRight=true)，每个 CurveGroup 单独为一个 loop
        // 过滤掉左右边界上的竖线弧段
        for (auto& g : lowerNc.groups) {
            CylindricalLoop loop;
            loop.isContractible = false;
            loop.leftToRight = true;
            for (auto& arc : g.arcs) {
                if (!isPurelyBoundaryEdge(arc, boundaryLeft, boundaryRight, eps))
                    loop.arcs.push_back(std::move(arc));
            }
            if (!loop.arcs.empty())
                area.boundary.push_back(std::move(loop));
        }

        // 检查该 band 是否包含 contractible area，将其嵌套为子区域
        for (size_t ci = 0; ci < contractibleAreas.size(); ++ci) {
            if (!contractibleAreas[ci].isContractibleArea()) continue;
            if (contractibleInsideBand(contractibleAreas[ci], area,
                                       boundaryLeft, boundaryRight, eps)) {
                area.children.push_back(std::move(contractibleAreas[ci]));
                contractibleAreas[ci].boundary.clear(); // 标记已分配
            }
        }

        bandAreas.push_back(std::move(area));
    }

    // 清理已被分配到 band 内的 contractible areas
    contractibleAreas.erase(
        std::remove_if(contractibleAreas.begin(), contractibleAreas.end(),
            [](const CylindricalArea& a) { return !a.isValid(); }),
        contractibleAreas.end());

    // ====================================================================
    // Step 5: 构建最终嵌套树
    // ====================================================================
    std::vector<CylindricalArea> result;

    // 5a: 未被 band 包含的 contractible → 顶层
    for (auto& ca : contractibleAreas) {
        if (!ca.isValid()) continue;
        result.push_back(std::move(ca));
    }

    // 5b: 顶层 contractible 相互嵌套
    if (result.size() > 1) {
        std::sort(result.begin(), result.end(),
            [](const CylindricalArea& a, const CylindricalArea& b) {
                double aArea = contractibleAreaSize(a);
                double bArea = contractibleAreaSize(b);
                return aArea > bArea;
            });

        std::vector<bool> assigned(result.size(), false);
        std::vector<CylindricalArea> nested;

        for (size_t i = 0; i < result.size(); ++i) {
            if (assigned[i]) continue;
            if (!result[i].isContractibleArea()) {
                nested.push_back(std::move(result[i]));
                continue;
            }
            for (size_t j = i + 1; j < result.size(); ++j) {
                if (assigned[j]) continue;
                if (!result[j].isContractibleArea()) continue;
                if (contractibleInsideContractible(result[j], result[i],
                                                   boundaryLeft, boundaryRight, eps)) {
                    result[i].children.push_back(std::move(result[j]));
                    assigned[j] = true;
                }
            }
            nested.push_back(std::move(result[i]));
        }
        result = std::move(nested);
    }

    // 5c: band 内部 children 嵌套 + 加入最终结果
    for (auto& ba : bandAreas) {
        if (ba.children.size() > 1) {
            std::sort(ba.children.begin(), ba.children.end(),
                [](const CylindricalArea& a, const CylindricalArea& b) {
                    double aArea = contractibleAreaSize(a);
                    double bArea = contractibleAreaSize(b);
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

        result.push_back(std::move(ba));
    }

    return result;
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
