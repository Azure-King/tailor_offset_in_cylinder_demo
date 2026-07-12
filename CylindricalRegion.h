#pragma once

#include <vector>
#include <functional>
#include <cmath>
#include <limits>
#include "BooleanOperations.h"

namespace tailor_visualization {

// ============================================================================
// CylindricalLoop: 圆柱面展开带上的一个环（或路径）
// ============================================================================
//
// isContractible == true  → 可缩环：条带内部的一个闭合环，不触碰左右边界
// isContractible == false → 不可缩环：跨越条带的开放路径，首尾端点分别位于
//                            左右边界（或右/左边界）上
//
// 对于不可缩环，leftToRight 表示方向：
//   - leftToRight == true  → 从左边界到右边界（通常是 band 的下边界）
//   - leftToRight == false → 从右边界到左边界（通常是 band 的上边界）
//
// 所有 Arc 的 x 坐标必须在 [boundaryLeft, boundaryRight] 范围内。
//
struct CylindricalLoop {
    std::vector<Arc> arcs;
    bool isContractible = true;
    bool leftToRight = true;   // 仅 isContractible==false 时有意义

    /// 计算 Y 范围（考虑圆弧 bulge 的极值点）
    std::pair<double, double> yRange() const {
        if (arcs.empty()) return { 0.0, 0.0 };
        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();

        for (const auto& arc : arcs) {
            double ay = arc.Point0().y;
            double by = arc.Point1().y;
            yMin = std::min(yMin, std::min(ay, by));
            yMax = std::max(yMax, std::max(ay, by));

            double bulge = arc.Bulge();
            if (std::abs(bulge) > 1e-10) {
                double ax = arc.Point0().x;
                double b2 = 0.5 * (1.0 / bulge - bulge);
                double cx = 0.5 * (ax + arc.Point1().x - b2 * (by - ay));
                double cy = 0.5 * (ay + by + b2 * (arc.Point1().x - ax));
                double Rc = std::sqrt((ax - cx) * (ax - cx) + (ay - cy) * (ay - cy));
                yMin = std::min(yMin, cy - Rc);
                yMax = std::max(yMax, cy + Rc);
            }
        }
        return { yMin, yMax };
    }

    /// 计算 X 范围（考虑圆弧 bulge 的极值点）
    std::pair<double, double> xRange() const {
        if (arcs.empty()) return { 0.0, 0.0 };
        double xMin = std::numeric_limits<double>::max();
        double xMax = std::numeric_limits<double>::lowest();

        for (const auto& arc : arcs) {
            double ax = arc.Point0().x;
            double bx = arc.Point1().x;
            double ay = arc.Point0().y;
            double by = arc.Point1().y;
            xMin = std::min(xMin, std::min(ax, bx));
            xMax = std::max(xMax, std::max(ax, bx));

            double bulge = arc.Bulge();
            if (std::abs(bulge) > 1e-10) {
                double b2 = 0.5 * (1.0 / bulge - bulge);
                double cx = 0.5 * (ax + bx - b2 * (by - ay));
                double cy = 0.5 * (ay + by + b2 * (bx - ax));
                double Rc = std::sqrt((ax - cx) * (ax - cx) + (ay - cy) * (ay - cy));
                xMin = std::min(xMin, cx - Rc);
                xMax = std::max(xMax, cx + Rc);
            }
        }
        return { xMin, xMax };
    }

    /// 检查有效性
    bool isValid() const { return !arcs.empty(); }

    /// 获取起点坐标
    ArcPoint startPoint() const {
        return arcs.empty() ? ArcPoint{} : arcs.front().Point0();
    }

    /// 获取终点坐标
    ArcPoint endPoint() const {
        return arcs.empty() ? ArcPoint{} : arcs.back().Point1();
    }
};


// ============================================================================
// CylindricalArea: 圆柱面上的一个区域
// ============================================================================
//
// 类似 PolyTree 的嵌套结构。每个节点有一个边界和若干子区域（孔洞/嵌套区域）。
//
// 两种形态：
//   1. 可缩区域 (contractible area)：boundary 包含 1 个可缩环
//      表示圆柱面上不环绕轴线的区域（如一个点周围的小圆盘）
//
//   2. 条带区域 (band area)：boundary 包含 2 个不可缩环
//      boundary[0] = 上方路径 (right→left, leftToRight==false)
//      boundary[1] = 下方路径 (left→right, leftToRight==true)
//      表示环绕圆柱轴线的环形带区域
//
// 嵌套规则：
//   - 偶数层：外层区域 (outer)
//   - 奇数层：内层区域/孔洞 (hole)
//   - Band areas 始终位于顶层（不可嵌套在其他 band 内）
//   - Contractible areas 可作为 band 的子节点（孔洞），也可独立存在
//   - Contractible areas 之间也可以嵌套（标准的 PolyTree 包含关系）
//
class CylindricalArea {
public:
    /// 边界环
    /// - Contractible: 所有 loop 的 isContractible == true
    /// - Band:         所有 loop 的 isContractible == false，
    ///                 包含 upper (leftToRight=false) 和 lower (leftToRight=true)
    std::vector<CylindricalLoop> boundary;

    /// 子区域（孔洞或嵌套外环）
    std::vector<CylindricalArea> children;

    // ---- 类型判断 ----

    bool isBand() const {
        if (boundary.empty()) return false;
        bool hasUpper = false, hasLower = false;
        for (const auto& l : boundary) {
            if (l.isContractible) return false;
            if (!l.leftToRight) hasUpper = true;
            else                hasLower = true;
        }
        return hasUpper && hasLower;
    }

    bool isContractibleArea() const {
        if (boundary.empty()) return false;
        for (const auto& l : boundary)
            if (!l.isContractible) return false;
        return true;
    }

    bool isValid() const {
        if (boundary.empty()) return false;
        for (const auto& l : boundary)
            if (!l.isValid()) return false;
        return isContractibleArea() || isBand();
    }

    // ---- 空间查询 ----

    /// 获取 Y 范围（整个区域的包围盒）
    std::pair<double, double> yRange() const;

    /// 判断点 (x, y) 是否在此区域内（unwrap 坐标）
    /// 对于 band 区域：点在上下边界之间
    /// 对于 contractible 区域：用 winding number 判断
    /// @param boundaryLeft, boundaryRight 条带边界
    bool containsPoint(double x, double y,
                       double boundaryLeft, double boundaryRight,
                       double eps = 1e-9) const;

    // ---- 遍历 ----

    /// 递归遍历所有区域（包括自身和子区域），回调返回 false 则停止
    using AreaVisitor = std::function<bool(const CylindricalArea&, int depth)>;
    void traverse(const AreaVisitor& visitor, int depth = 0) const;

    /// 获取总区域数（包括自身和所有嵌套子区域）
    int totalCount() const;
};


// ============================================================================
// Builder 函数：从 ClipToStrip 输出构建 CylindricalArea 树
// ============================================================================

/// 从标注后的多边形列表构建圆柱区域树
///
/// @param polygons       ClipToStrip + union 后带 BoundaryType 标注的多边形集合
///                       每个多边形标注了每条边的上/下边界类型以及内/外环属性
/// @param boundaryLeft   条带左边界 X 坐标
/// @param boundaryRight  条带右边界 X 坐标
/// @param eps            判断顶点是否在边界上的容差
/// @return 顶层 CylindricalArea 列表（band areas 和 contractible areas）
std::vector<CylindricalArea> BuildCylindricalAreas(
    const std::vector<AnnotatedPolygon>& polygons,
    double boundaryLeft, double boundaryRight,
    double eps = 1e-9);


// ============================================================================
// 辅助：将 CylindricalArea 树展平为渲染用的多边形列表
// ============================================================================

/// 展平后的多边形描述（用于传递给现有渲染管线）
struct FlattenedRegion {
    std::vector<Arc> arcs;   // 闭合或开放的弧段序列
    QRgba64 color;
    bool isHole = false;
    int depth = 0;           // 嵌套深度
    bool isContractible = true;
};

/// 遍历 CylindricalArea 树，将每个 loop 导出为 FlattenedRegion
void FlattenCylindricalAreas(
    const std::vector<CylindricalArea>& areas,
    std::vector<FlattenedRegion>& out,
    int depth = 0);

} // namespace tailor_visualization
