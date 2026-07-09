#pragma once

#include <vector>
#include "BooleanOperations.h"

namespace tailor_visualization {

/**
 * @brief 周期裁剪工具：将多边形裁剪到圆柱展开带 [boundaryLeft, boundaryRight]
 * 
 * 原理：圆柱面展开后是周期性带状区域。对于落在带外的多边形部分，
 * 通过沿 X 轴平移 stripWidth 的整数倍，将其移入带内后再求交集。
 * 
 * 步骤：
 * 1. 确定多边形跨越的周期范围
 * 2. 对每个周期，平移多边形使其与带区重合
 * 3. 用带区矩形求交集
 * 4. 收集所有交集结果
 */
class PeriodicClipper {
public:
    using Arc = tailor_visualization::Arc;
    using ArcPoint = tailor_visualization::ArcPoint;

    /**
     * @brief 将多边形裁剪到圆柱展开带内
     * @param inputPolygons  去自交规范化后的多边形集合
     * @param boundaryLeft   左侧边界 X 坐标
     * @param boundaryRight  右侧边界 X 坐标
     * @param fillTypeIndex  FillType 索引 (0=NonZero, 1=EvenOdd, 2=Ignore)
     * @param connectTypeIndex ConnectType 索引 (0=OuterFirst, 1=InnerFirst)
     * @return 裁剪后的多边形集合（已平移至带内）
     */
    static std::vector<std::vector<Arc>> ClipToStrip(
        const std::vector<std::vector<Arc>>& inputPolygons,
        double boundaryLeft,
        double boundaryRight,
        int fillTypeIndex = 1,
        int connectTypeIndex = 1);

    /**
     * @brief 创建矩形多边形（作为裁剪区域）
     * @param left   左边界
     * @param right  右边界
     * @param bottom 下边界
     * @param top    上边界
     * @return 矩形的弧段数组（顺时针封闭）
     */
    static std::vector<Arc> CreateRectPolygon(
        double left, double right, double bottom, double top);

    /**
     * @brief 沿 X 轴平移多边形
     * @param poly 输入多边形弧段数组
     * @param dx   X 轴偏移量
     * @return 平移后的弧段数组
     */
    static std::vector<Arc> ShiftPolygonX(
        const std::vector<Arc>& poly, double dx);

private:
    /**
     * @brief 计算多边形集合的包围盒
     */
    static void ComputeBounds(
        const std::vector<std::vector<Arc>>& polygons,
        double& minX, double& maxX,
        double& minY, double& maxY);
};

} // namespace tailor_visualization
