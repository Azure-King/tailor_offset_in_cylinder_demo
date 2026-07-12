#pragma once

#include <vector>
#include <functional>
#include <string>
#include <memory>
#include <variant>
#include <iostream>
#include <tailor_arc_or_segment.h>
#include <tailor.h>
#include <pattern.h>
#include <qrgba64.h>

namespace tailor_visualization {

    /**
     * @brief Arc 用户数据结构，包含颜色、线段ID标记和溯源信息
     * 
     * 关系链设计：
     * - sourceEdgeId: 原始输入边的全局索引，在 polygonToArcs 中设置后永不修改。
     *   用于从流水线任一步骤的输出直接追溯到用户最初绘制的边，绕过布尔运算导致的 ID 变化。
     * - segmentId:  当前阶段的合并标记，在流水线各阶段可能被重新分配。
     *   用于 MergeAdjacentCurves 时将同一原始边分割产生的相邻小弧段合并。
     * - edgeTag: 偏置边的类型标记（0=OffsetEdge, 1=JoinConvex, 2=JoinConcave），
     *   由偏置器回调设置。MergeAdjacentCurves 会检查 edgeTag 防止跨类型合并。
     */
    struct ArcUserData {
        QRgba64 color;
        int sourceEdgeId = -1; // 原始输入边的全局索引（关系链根节点，永不修改）
        int segmentId = -1;    // 当前阶段的合并标记（可在各阶段重新分配）
        int edgeTag = 0;       // 0=OffsetEdge, 1=JoinConvex, 2=JoinConcave
        // 凸点连接弧对应的原始多边形顶点坐标（仅在 edgeTag==1 时有效）
        double convexJoinVertexX = 0.0;
        double convexJoinVertexY = 0.0;

        ArcUserData() : color(), sourceEdgeId(-1), segmentId(-1), edgeTag(0) {}
        ArcUserData(QRgba64 c, int id) : color(c), sourceEdgeId(id), segmentId(id), edgeTag(0) {}
    };

    // 调试开关：设为 false 禁用相邻曲线合并
    constexpr bool ENABLE_CURVE_MERGE = true;

    // 类型别名定义 
    using ArcPoint = tailor::Point<double>;
    using Arc = tailor::ArcSegment<ArcPoint, double, ArcUserData>;
    using ArcUtils = tailor::ArcSegmentUtils<Arc>;

    /**
     * @brief 运行时可调精度核心
     * 
     * 替代编译期固定精度的 PrecisionCore<N>，通过静态变量 s_precision
     * 在运行时控制 epsilon 值。满足 tailor 的 PrecisionConcept。
     * 修改 s_precision 后，所有依赖 Precision 的计算自动使用新精度。
     * 
     * 精度重试序列: 10 → 9 → 11 → 8 → 12 → 7
     * 对应 epsilon: 1e-10 → 1e-9 → 1e-11 → 1e-8 → 1e-12 → 1e-7
     * 
     * @note 非线程安全，假设单线程使用场景。
     * 
     *       多线程实现思路（需要时按以下步骤改造）：
     *       1. 将 s_precision 改为 thread_local 静态变量：
     *          static thread_local inline size_t s_precision = 10;
     *          优点：零开销，每个线程独立精度，无需锁。
     *          代价：thread_local 在 Windows DLL 中有已知问题，
     *                若动态链接 tailor 则需谨慎。
     *       
     *       2. 备选方案——将精度嵌入 RuntimePrecisionCore 模板参数，
     *          回到编译期方案 PrecisionCore<N>，在 RetryTailorExecute
     *          中用 std::variant + std::visit 分发不同精度类型。优点是
     *          完全避免全局状态，缺点是需要 N 份模板实例化（代码膨胀）。
     *       
     *       3. 若必须保持静态变量方案且多线程共享 precision，
     *          使用 std::atomic<size_t> + compare_exchange，但需注意
     *          读取-修改-恢复的 ABA 问题（线程 A 设 9、线程 B 设 11
     *          后线程 A 恢复 10 会覆盖 B 的期望值）。不建议此方案。
     */
    struct RuntimePrecisionCore {
        static inline size_t s_precision = 10;

        static double ValueEpsilon() { return Epsilon(s_precision); }
        static double PointEpsilon() { return Epsilon(s_precision); }
        static double AngleEpsilon() { return Epsilon(s_precision); }

    private:
        static double Epsilon(size_t preci) {
            double result = 1.0;
            while (preci) { result /= 10.0; --preci; }
            return result;
        }
    };

    // 使用运行时精度替代编译期固定精度 PrecisionCore<10>
    using ArcTailor = tailor::Tailor<Arc, tailor::ArcAnalysis<Arc, tailor::ArcSegmentAnalyserCore<Arc, RuntimePrecisionCore>>>;
    // Note: Drafting type is defined in FourViewContainer.h for caching purposes
    // using Drafting = typename ArcTailor::PatternDrafting;

    /// 精度重试序列: 默认精度10，依次尝试 9, 11, 8, 12, 7
    static constexpr size_t kTailorPrecisions[] = {10, 9, 11, 8, 12, 7};

    /**
     * @brief 带精度回退的 tailor 执行 + pattern 处理
     * 
     * 将 tailor.Execute() 和 pattern.Stitch() 绑定在同一 lambda 中。
     * 若任意步骤抛出异常（包括 tailor 或 pattern），自动切换精度重试。
     * 
     * 精度序列: 10 → 9 → 11 → 8 → 12 → 7
     * 全部失败时输出 \033[31m 红色错误信息，返回默认构造值（流水线断裂）。
     * 
     * @tparam Func 可调用对象，签名为 `RetType func()`，函数体内完成
     *              tailor 添加多边形 + Execute + Pattern 操作，并返回结果
     * @param func 封装了 tailor + pattern 完整流程的可调用对象
     * @return func 的返回值；若全部重试失败则返回 RetType{}
     */
    template<typename Func>
    auto RetryTailorExecute(Func&& func) -> decltype(func()) {
        using ResultType = decltype(func());
        std::string lastError;
        for (size_t p : kTailorPrecisions) {
            RuntimePrecisionCore::s_precision = p;
            try {
                return func();
            } catch (const std::exception& e) {
                lastError = e.what();
                std::cerr << "\033[31m[TAILOR] precision=" << p << " failed: " << lastError << "\033[0m" << std::endl;
            } catch (...) {
                lastError = "unknown exception";
                std::cerr << "\033[31m[TAILOR] precision=" << p << " failed: unknown exception\033[0m" << std::endl;
            }
        }
        std::cerr << "\033[31m[TAILOR FATAL] All precision retries (10,9,11,8,12,7) exhausted! "
                  << "Pipeline broken. Last error: " << lastError << "\033[0m" << std::endl;
        return ResultType{};
    }

    // 前向声明
    template<typename Drafting>
    class IConnectType;

    /**
     * @brief 检查两个弧段是否可以合并（几何兼容）
     * 要求：同为直线或同为圆弧（且圆心相同、方向相同）
     */
    inline bool CanMergeTwoArcs(const Arc& a, const Arc& b) {
        using std::abs;
        bool aIsArc = abs(a.Bulge()) > 0;
        bool bIsArc = abs(b.Bulge()) > 0;
        if (aIsArc != bIsArc) return false;
        // 同为直线：可合并
        if (!aIsArc) return true;
        // 同为圆弧：检查圆心是否一致
        tailor::ArcSegmentTraits<Arc> traits;
        auto cA = traits.Center(a);
        auto cB = traits.Center(b);
        tailor::PointUtils<ArcPoint> pUtils;
        return pUtils.IsSamePosition(cA, cB, 1e-8);
    }

    /**
     * @brief 合并两个几何兼容的相邻弧段
     * @param a 前一个弧段
     * @param b 后一个弧段（a.Point1() == b.Point0()）
     * @return 合并后的弧段，从 a.Point0() 到 b.Point1()，保持相同的弧属性
     */
    inline Arc MergeTwoArcs(const Arc& a, const Arc& b) {
        tailor::ArcSegmentTraits<Arc> traits;
        return traits.Construct(a.Point0(), b.Point1(), a);
    }

    /**
     * @brief 合并多边形中相邻的同ID且同类型的弧段
     * 将具有相同 segmentId（>=0）且相同 edgeTag 且几何兼容的连续弧段合并为单个弧段。
     * 用于消除布尔运算中单调分割产生的小线段。
     * 
     * 重要：edgeTag 必须匹配，防止偏置阶段产生的 OffsetEdge(segId=X, tag=0)
     * 和 JoinConvex(segId=X, tag=1) 被错误合并，导致凸点连接弧的溯源标记丢失。
     * 
     * @param arcs 输入弧段数组（构成一个闭合多边形）
     * @return 合并后的弧段数组
     */
    inline std::vector<Arc> MergeAdjacentCurves(const std::vector<Arc>& arcs) {
        if (arcs.size() < 2) return arcs;

        // 退化检测：合并后弧段首尾重合 -> 拒绝合并
        // 解决多段线正反向曲线（A→B 和 B→A）被错误合并成 A→A 零长度弧段的问题
        auto WouldBeDegenerate = [](const Arc& a, const Arc& b) {
            using std::abs;
            auto dx = a.Point0().x - b.Point1().x;
            auto dy = a.Point0().y - b.Point1().y;
            return abs(dx) < 1e-9 && abs(dy) < 1e-9;
        };

        std::vector<Arc> result;
        Arc current = arcs[0];

        for (size_t i = 1; i < arcs.size(); ++i) {
            const auto& next = arcs[i];
            int curId = current.Data().segmentId;
            int nextId = next.Data().segmentId;
            int curTag = current.Data().edgeTag;
            int nextTag = next.Data().edgeTag;

            // 相同有效ID、相同edgeTag且几何兼容 -> 合并
            if (curId >= 0 && curId == nextId && curTag == nextTag
                && CanMergeTwoArcs(current, next)
                && !WouldBeDegenerate(current, next)) {
                current = MergeTwoArcs(current, next);
            } else {
                result.push_back(current);
                current = next;
            }
        }
        result.push_back(current);

        // 闭合多边形：检查首尾是否也应合并
        if (result.size() >= 2) {
            auto& first = result[0];
            auto& last = result.back();
            int firstId = first.Data().segmentId;
            int lastId = last.Data().segmentId;
            int firstTag = first.Data().edgeTag;
            int lastTag = last.Data().edgeTag;
            if (firstId >= 0 && firstId == lastId && firstTag == lastTag
                && CanMergeTwoArcs(last, first)
                && !WouldBeDegenerate(last, first)) {
                // 将尾部合并到头部（last→first），保持多边形闭合
                result[0] = MergeTwoArcs(last, first);
                result.pop_back();
            }
        }

        return result;
    }

    /**
     * @brief 批量合并多个多边形中的相邻同ID弧段
     */
    inline void MergeAdjacentCurvesBatch(std::vector<std::vector<Arc>>& polygons) {
        for (auto& poly : polygons) {
            poly = MergeAdjacentCurves(poly);
        }
    }

    /**
     * @brief 带有内环标记的多边形结果
     */
    struct PolygonWithHoleInfo {
        std::vector<Arc> vertices;  // 多边形的边
        bool isHole = false;        // 是否为内环（洞）
    };

    /**
     * @brief 带 BoundaryType 标注和 PolyTree 层级信息的多边形
     * 用于 CylindricalRegion 构建时直接从 pattern 结果获取上/下边界和内/外环信息
     */
    struct AnnotatedPolygon {
        std::vector<Arc> arcs;
        std::vector<tailor::BoundaryType> edgeTypes;  // 每条边在布尔运算结果中的边界类型
        bool isOuter;  // true=外环 (PolyTree depth%2==0), false=内环/孔洞
    };

    template<typename Drafting>
    class ConnectTypeOuterFirstWrapper;

    template<typename Drafting>
    class ConnectTypeInnerFirstWrapper;

    // 布尔操作类型
    enum class BooleanOperation {
        Union,      // 并集
        Intersection, // 交集
        Difference,  // 差集
        XOR          // 异或
    };

    /**
     * @brief FillType 抽象接口，用于运行时选择不同的填充规则
     */
    class IFillType {
    public:
        virtual ~IFillType() = default;
        virtual tailor::BoundaryType operator()(const tailor::EdgeFillStatus& status) const = 0;
    };

    /**
     * @brief NonZeroFillType 的具体实现
     */
    class NonZeroFillTypeWrapper : public IFillType {
    public:
        ~NonZeroFillTypeWrapper() override = default;
        tailor::BoundaryType operator()(const tailor::EdgeFillStatus& status) const override {
            tailor::NonZeroFillType filler;
            return filler(status);
        }
    };

    /**
     * @brief EvenOddFillType 的具体实现
     */
    class EvenOddFillTypeWrapper : public IFillType {
    public:
        ~EvenOddFillTypeWrapper() override = default;
        tailor::BoundaryType operator()(const tailor::EdgeFillStatus& status) const override {
            tailor::EvenOddFillType filler;
            return filler(status);
        }
    };

    /**
     * @brief IgnoreFillType 的具体实现
     */
    class IgnoreFillTypeWrapper : public IFillType {
    public:
        ~IgnoreFillTypeWrapper() override = default;
        tailor::BoundaryType operator()(const tailor::EdgeFillStatus& status) const override {
            tailor::IgnoreFillType filler;
            return filler(status);
        }
    };

    /**
     * @brief 指定环绕数的条件
     */
    template<int TargetWinding>
    class SpecificWindingCondition {
    public:
        constexpr bool operator()(tailor::Int wind) const {
            return wind == TargetWinding;
        }
    };

    /**
     * @brief 指定环绕数的 FillType (模板版本，用于 Pattern)
     */
    template<int TargetWinding>
    class SpecificWindingFillType {
    public:
        static_assert(TargetWinding != 0, "Target winding cannot be 0 (must start from outside)");

        tailor::BoundaryType operator()(const tailor::EdgeFillStatus& status) const {
            auto x = static_cast<tailor::Int>(status.positive) - static_cast<tailor::Int>(status.negitive);

            bool succ0 = status.wind == TargetWinding;
            bool succ1 = (status.wind + x) == TargetWinding;

            if (succ0 && succ1) {
                return tailor::BoundaryType::Inside;
            } else if (succ0) {
                return tailor::BoundaryType::UpperBoundary;
            } else if (succ1) {
                return tailor::BoundaryType::LowerBoundary;
            } else {
                return tailor::BoundaryType::Outside;
            }
        }
    };

    /**
     * @brief 指定环绕数的 FillType 具体实现 (运行时包装器)
     */
    class SpecificWindingFillTypeWrapper : public IFillType {
    public:
        explicit SpecificWindingFillTypeWrapper(int targetWinding) : targetWinding_(targetWinding) {}

        ~SpecificWindingFillTypeWrapper() override = default;

        tailor::BoundaryType operator()(const tailor::EdgeFillStatus& status) const override {
            auto x = static_cast<tailor::Int>(status.positive) - static_cast<tailor::Int>(status.negitive);

            bool succ0 = status.wind == targetWinding_;
            bool succ1 = (status.wind + x) == targetWinding_;

            if (succ0 && succ1) {
                return tailor::BoundaryType::Inside;
            } else if (succ0) {
                return tailor::BoundaryType::UpperBoundary;
            } else if (succ1) {
                return tailor::BoundaryType::LowerBoundary;
            } else {
                return tailor::BoundaryType::Outside;
            }
        }

        int getTargetWinding() const { return targetWinding_; }

    private:
        int targetWinding_;
    };

    /**
     * @brief 环绕数大于0的 FillType (模板版本，用于 Pattern)
     */
    using PositiveWindFillType = tailor::ConditionFillType<tailor::GtSpecifiedWindCondition<0>>;

    /**
     * @brief 环绕数大于0的 FillType 具体实现 (运行时包装器)
     */
    class PositiveWindFillTypeWrapper : public IFillType {
    public:
        PositiveWindFillTypeWrapper() = default;
        ~PositiveWindFillTypeWrapper() override = default;

        tailor::BoundaryType operator()(const tailor::EdgeFillStatus& status) const override {
            PositiveWindFillType filler;
            return filler(status);
        }
    };

    /**
     * @brief FillType variant，支持所有可能的FillType组合
     * 用于编译时类型分发到tailor的Pattern模板
     */
    struct FillTypeVariant {
        template<typename T>
        FillTypeVariant(T&& value) : variant(std::forward<T>(value)) {}

        std::variant <
            tailor::NonZeroFillType,
            tailor::EvenOddFillType,
            tailor::IgnoreFillType,
            PositiveWindFillType,
            SpecificWindingFillType<1>,
            SpecificWindingFillType<2>,
            SpecificWindingFillType<3>,
            SpecificWindingFillType<4>,
            SpecificWindingFillType<5>,
            SpecificWindingFillType<-1>,
            SpecificWindingFillType<-2>,
            SpecificWindingFillType<-3>,
            SpecificWindingFillType<-4>,
            SpecificWindingFillType<-5>
        > variant;
    };

    /**
     * @brief 将IFillType指针转换为FillTypeVariant
     * 通过运行时分发将虚接口转换为编译时类型
     */
    inline FillTypeVariant ToFillTypeVariant(const IFillType* fillType) {
        if (dynamic_cast<const NonZeroFillTypeWrapper*>(fillType)) {
            return FillTypeVariant(tailor::NonZeroFillType{});
        } else if (dynamic_cast<const EvenOddFillTypeWrapper*>(fillType)) {
            return FillTypeVariant(tailor::EvenOddFillType{});
        } else if (dynamic_cast<const IgnoreFillTypeWrapper*>(fillType)) {
            return FillTypeVariant(tailor::IgnoreFillType{});
        } else if (dynamic_cast<const PositiveWindFillTypeWrapper*>(fillType)) {
            return FillTypeVariant(PositiveWindFillType{});
        } else if (const auto* specific = dynamic_cast<const SpecificWindingFillTypeWrapper*>(fillType)) {
            int winding = specific->getTargetWinding();
            switch (winding) {
            case 1: return FillTypeVariant(SpecificWindingFillType<1>{});
            case 2: return FillTypeVariant(SpecificWindingFillType<2>{});
            case 3: return FillTypeVariant(SpecificWindingFillType<3>{});
            case 4: return FillTypeVariant(SpecificWindingFillType<4>{});
            case 5: return FillTypeVariant(SpecificWindingFillType<5>{});
            case -1: return FillTypeVariant(SpecificWindingFillType<-1>{});
            case -2: return FillTypeVariant(SpecificWindingFillType<-2>{});
            case -3: return FillTypeVariant(SpecificWindingFillType<-3>{});
            case -4: return FillTypeVariant(SpecificWindingFillType<-4>{});
            case -5: return FillTypeVariant(SpecificWindingFillType<-5>{});
            default: return FillTypeVariant(tailor::NonZeroFillType{}); // 默认回退
            }
        }
        return FillTypeVariant(tailor::NonZeroFillType{});
    }

    /**
     * @brief ConnectType 抽象接口，用于运行时选择不同的连接方式
     */
    template<typename Drafting>
    class IConnectType {
    public:
        virtual ~IConnectType() = default;
        virtual std::vector<tailor::Polygon<tailor::PolyEdgeInfo>> Connect(
            const Drafting& drafting,
            std::vector<tailor::BoundaryType> types) const = 0;
    };

    /**
     * @brief ConnectTypeOuterFirst 的具体实现
     */
    template<typename Drafting>
    class ConnectTypeOuterFirstWrapper : public IConnectType<Drafting> {
    public:
        std::vector<tailor::Polygon<tailor::PolyEdgeInfo>> Connect(
            const Drafting& drafting,
            std::vector<tailor::BoundaryType> types) const override {
            tailor::ConnectTypeOuterFirst connector;
            return connector.Connect(drafting, std::move(types));
        }
    };

    /**
     * @brief ConnectTypeInnerFirst 的具体实现
     */
    template<typename Drafting>
    class ConnectTypeInnerFirstWrapper : public IConnectType<Drafting> {
    public:
        std::vector<tailor::Polygon<tailor::PolyEdgeInfo>> Connect(
            const Drafting& drafting,
            std::vector<tailor::BoundaryType> types) const override {
            tailor::ConnectTypeInnerFirst connector;
            return connector.Connect(drafting, std::move(types));
        }
    };

    /**
     * @brief 布尔运算封装类
     * 提供路径多边形的布尔运算功能，支持并集、交集、差集和异或操作
     */
    class BooleanOperations {
        using AA = tailor::ArcAnalysis<Arc, tailor::ArcSegmentAnalyserCore<Arc, RuntimePrecisionCore>>;
    public:
        // 控制台输出开关（由外部 UI 控制）
        static bool s_consolePolygonOutput;

        /**
         * @brief 构造函数
         */
        BooleanOperations();

        /**
         * @brief 从弧段数组添加多边形
         * @param arcs 弧段数组
         */
        void AddPolygonFromArcs(const std::vector<Arc>& arcs);

        /**
         * @brief 添加 Clip 多边形（裁剪多边形）
         * @param arcs 弧段数组
         */
        void AddClipPolygon(const std::vector<Arc>& arcs);

        /**
         * @brief 添加 Subject 多边形（被裁剪多边形）
         * @param arcs 弧段数组
         */
        void AddSubjectPolygon(const std::vector<Arc>& arcs);

        /**
         * @brief 批量添加多个 Clip 多边形
         * @param polygons 多边形集合
         */
        void AddClipPolygons(const std::vector<std::vector<Arc>>& polygons);

        /**
         * @brief 批量添加多个 Subject 多边形
         * @param polygons 多边形集合
         */
        void AddSubjectPolygons(const std::vector<std::vector<Arc>>& polygons);

        /**
         * @brief 获取 Clip 多边形数量
         * @return Clip 多边形数量
         */
        size_t GetClipPolygonCount() const { return clipPolygons_.size(); }

        /**
         * @brief 获取 Subject 多边形数量
         * @return Subject 多边形数量
         */
        size_t GetSubjectPolygonCount() const { return subjectPolygons_.size(); }

        /**
         * @brief 获取添加的多边形数量（向后兼容）
         * @return 多边形数量
         */
        size_t GetPolygonCount() const { return GetClipPolygonCount() + GetSubjectPolygonCount(); }

        /**
         * @brief 执行布尔运算
         * @param operation 布尔操作类型
         * @param clipFillType Clip集合的填充规则指针
         * @param subjectFillType Subject集合的填充规则指针
         * @return 结果多边形集合
         */
        std::vector<std::vector<Arc>> Execute(
            BooleanOperation operation,
            const IFillType* clipFillType,
            const IFillType* subjectFillType);

        /**
         * @brief 执行布尔运算（向后兼容，使用单一fillType）
         * @param operation 布尔操作类型
         * @param fillType 填充类型：0=NonZero, 1=EvenOdd, 2=Ignore（默认 EvenOdd）
         * @return 结果多边形集合
         */
        std::vector<std::vector<Arc>> Execute(BooleanOperation operation, int fillType = 1);

        /**
         * @brief 执行布尔运算，支持 ConnectType
         * @param operation 布尔操作类型
         * @param clipFillType Clip集合的填充规则指针
         * @param subjectFillType Subject集合的填充规则指针
         * @param connectType 连接类型指针（可选）
         * @return 结果多边形集合
         */
        std::vector<std::vector<Arc>> Execute(
            BooleanOperation operation,
            const IFillType* clipFillType,
            const IFillType* subjectFillType,
            const IConnectType<tailor::Tailor<Arc, AA>::PatternDrafting>* connectType);

        /**
         * @brief 执行布尔运算，获取带内环信息的结果
         * @param operation 布尔操作类型
         * @param clipFillType Clip集合的填充规则指针
         * @param subjectFillType Subject集合的填充规则指针
         * @param connectType 连接类型指针（可选）
         * @return 带内环标记的结果多边形集合
         */
        std::vector<PolygonWithHoleInfo> ExecuteWithHoles(
            BooleanOperation operation,
            const IFillType* clipFillType,
            const IFillType* subjectFillType,
            const IConnectType<tailor::Tailor<Arc, AA>::PatternDrafting>* connectType = nullptr);

        /**
         * @brief 执行 OnlyClipPattern，获取 Clip 多边形（非自交）
         * @param fillType 填充类型指针，支持指定环绕数
         * @return Clip 多边形集合
         */
        std::vector<std::vector<Arc>> ExecuteOnlyClipPattern(const IFillType* fillType = nullptr);

        /**
         * @brief 执行 OnlyClipPattern，获取 Clip 多边形（非自交），支持 ConnectType
         * @param fillType 填充类型指针，支持指定环绕数
         * @param connectType 连接类型指针
         * @return Clip 多边形集合
         */
        std::vector<std::vector<Arc>> ExecuteOnlyClipPattern(
            const IFillType* fillType,
            const IConnectType<tailor::Tailor<Arc, AA>::PatternDrafting>* connectType);

        /**
         * @brief 执行 OnlyClipPattern，获取带内环信息的 Clip 多边形
         * @param fillType 填充类型指针，支持指定环绕数
         * @param connectType 连接类型指针
         * @return 带内环标记的 Clip 多边形集合
         */
        std::vector<PolygonWithHoleInfo> ExecuteOnlyClipPatternWithHoles(
            const IFillType* fillType = nullptr,
            const IConnectType<tailor::Tailor<Arc, AA>::PatternDrafting>* connectType = nullptr);

        /**
         * @brief 执行 OnlySubjectPattern，获取 Subject 多边形（非自交）
         * @param fillType 填充类型指针，支持指定环绕数
         * @return Subject 多边形集合
         */
        std::vector<std::vector<Arc>> ExecuteOnlySubjectPattern(const IFillType* fillType = nullptr);

        /**
         * @brief 执行 OnlySubjectPattern，获取 Subject 多边形（非自交），支持 ConnectType
         * @param fillType 填充类型指针，支持指定环绕数
         * @param connectType 连接类型指针
         * @return Subject 多边形集合
         */
        std::vector<std::vector<Arc>> ExecuteOnlySubjectPattern(
            const IFillType* fillType,
            const IConnectType<tailor::Tailor<Arc, AA>::PatternDrafting>* connectType);

        /**
         * @brief 执行 OnlySubjectPattern，获取带内环信息的 Subject 多边形
         * @param fillType 填充类型指针，支持指定环绕数
         * @param connectType 连接类型指针
         * @return 带内环标记的 Subject 多边形集合
         */
        std::vector<PolygonWithHoleInfo> ExecuteOnlySubjectPatternWithHoles(
            const IFillType* fillType = nullptr,
            const IConnectType<tailor::Tailor<Arc, AA>::PatternDrafting>* connectType = nullptr);

        /**
         * @brief 执行 OnlySubjectPattern，保留 BoundaryType 和 PolyTree 层级信息
         * 用于 CylindricalRegion 构建，避免自己计算上/下边界和内/外环。
         * @param fillType 填充类型指针
         * @return 标注后的多边形集合
         */
        std::vector<AnnotatedPolygon> ExecuteOnlySubjectPatternAnnotated(
            const IFillType* fillType = nullptr);

        /**
         * @brief 创建并返回 Drafting（用于缓存）
         * @return Drafting 对象
         */
        typename ArcTailor::PatternDrafting CreateDrafting();

        /**
         * @brief 清空所有多边形
         */
        void Clear();

        /**
         * @brief 将弧段分割为单调弧段
         * @param arcs 输入弧段
         * @return 分割后的弧段
         */
        std::vector<Arc> SplitToMonotonic(const std::vector<Arc>& arcs);

        /**
         * @brief 递归遍历多边形树
         * @param tree 多边形树
         * @param callback 处理多边形的回调
         */
        template<typename T>
        void ForEachPolyTree(const tailor::PolyTree<T>& tree,
            const std::function<void(const typename tailor::PolyTree<T>::PolygonType&)>& callback) const;

        /**
         * @brief 递归遍历多边形树，带层级信息
         * @param tree 多边形树
         * @param depth 当前层级
         * @param callback 处理多边形的回调，传递多边形和层级
         */
        template<typename T>
        void ForEachPolyTreeWithDepth(const tailor::PolyTree<T>& tree,
            int depth,
            const std::function<void(const typename tailor::PolyTree<T>::PolygonType&, int)>& callback) const;

        /**
         * @brief 计算多边形的带符号面积（用于判断方向）
         * @param arcs 多边形的弧段
         * @return 带符号面积，负值表示顺时针（内环）
         */
        double CalculateSignedArea(const std::vector<Arc>& arcs) const;

    private:
        /**
         * @brief 使用 drafting 执行指定的布尔运算
         * @param drafting 裁剪结果
         * @param operation 布尔操作类型
         * @param fillType 填充类型：0=NonZero, 1=EvenOdd, 2=Ignore
         * @return 结果多边形集合
         */
        std::vector<std::vector<Arc>> ExecuteWithDrafting(
            const typename ArcTailor::PatternDrafting& drafting,
            BooleanOperation operation, int fillType = 1);

        /**
         * @brief 使用 drafting 和指定的填充规则执行布尔运算
         * @param drafting 裁剪结果
         * @param operation 布尔操作类型
         * @param clipFillType Clip集合的填充规则指针
         * @param subjectFillType Subject集合的填充规则指针
         * @return 结果多边形集合
         */
        std::vector<std::vector<Arc>> ExecuteWithFillTypes(
            const typename ArcTailor::PatternDrafting& drafting,
            BooleanOperation operation,
            const IFillType* clipFillType,
            const IFillType* subjectFillType);

        /**
         * @brief 使用 drafting 和指定的填充规则、连接方式执行布尔运算
         * @param drafting 裁剪结果
         * @param operation 布尔操作类型
         * @param clipFillType Clip集合的填充规则指针
         * @param subjectFillType Subject集合的填充规则指针
         * @param connectType 连接方式指针
         * @return 结果多边形集合
         */
        std::vector<std::vector<Arc>> ExecuteWithFillTypesAndConnectType(
            const typename ArcTailor::PatternDrafting& drafting,
            BooleanOperation operation,
            const IFillType* clipFillType,
            const IFillType* subjectFillType,
            const IConnectType<tailor::Tailor<Arc, AA>::PatternDrafting>* connectType);

    private:
        /**
         * @brief 处理指定环绕数的布尔运算（运行时实现）
         */
        std::vector<std::vector<Arc>> ExecuteWithSpecificWinding(
            const typename ArcTailor::PatternDrafting& drafting,
            const IFillType* clipFillType,
            const IFillType* subjectFillType,
            BooleanOperation operation,
            const std::function<void(const tailor::Polygon<tailor::PolyEdgeInfo>&)>& fun);

        /**
         * @brief 执行并集操作
         * @param drafting 裁剪结果
         * @param fillType 填充类型：0=NonZero, 1=EvenOdd, 2=Ignore
         * @return 结果多边形集合
         */
        std::vector<std::vector<Arc>> ExecuteUnion(const typename ArcTailor::PatternDrafting& drafting, int fillType = 1);

        /**
         * @brief 执行交集操作
         * @param drafting 裁剪结果
         * @param fillType 填充类型：0=NonZero, 1=EvenOdd, 2=Ignore
         * @return 结果多边形集合
         */
        std::vector<std::vector<Arc>> ExecuteIntersection(const typename ArcTailor::PatternDrafting& drafting, int fillType = 1);

        /**
         * @brief 执行差集操作
         * @param drafting 裁剪结果
         * @param fillType 填充类型：0=NonZero, 1=EvenOdd, 2=Ignore
         * @return 结果多边形集合
         */
        std::vector<std::vector<Arc>> ExecuteDifference(const typename ArcTailor::PatternDrafting& drafting, int fillType = 1);

        /**
         * @brief 执行异或操作
         * @param drafting 裁剪结果
         * @param fillType 填充类型：0=NonZero, 1=EvenOdd, 2=Ignore
         * @return 结果多边形集合
         */
        std::vector<std::vector<Arc>> ExecuteXOR(const typename ArcTailor::PatternDrafting& drafting, int fillType = 1);

        // 数据成员
        AA arcAnalysis_;
        std::vector<std::vector<Arc>> clipPolygons_;     // Clip 集合（裁剪多边形）
        std::vector<std::vector<Arc>> subjectPolygons_;   // Subject 集合（被裁剪多边形）

        // 向后兼容：polygons_ 指向 clipPolygons_
        std::vector<std::vector<Arc>>& polygons_ = clipPolygons_;
    };

} // namespace tailor_visualization
