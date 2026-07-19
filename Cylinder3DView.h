#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVector3D>
#include <QMatrix4x4>
#include <QColor>
#include <QPointF>
#include <QSet>
#include <vector>

class Cylinder3DView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    /// 多边形的顶点（含 bulge 信息），用于传递到 3D 视图
    struct Vertex2D {
        QPointF point;
        double bulge = 0.0;
    };

    /// 一个多边形的描述（顶点 + 颜色）
    struct Polygon2D {
        std::vector<Vertex2D> vertices;
        QColor color;
        bool isOpen = false;
    };

    explicit Cylinder3DView(QWidget* parent = nullptr);
    ~Cylinder3DView() override;

    void setCylinderRadius(float radius);
    void setCylinderHeight(float height);
    float cylinderRadius() const { return m_radius; }
    float cylinderHeight() const { return m_height; }

    /// 设置要贴到圆柱面上的合并多边形数据（来自周期裁剪 View 7）
    void setMergedPolygons(const std::vector<Polygon2D>& polygons);
    void clearMergedPolygons();

    /// 设置偏置后的圆柱区域边界叠加显示（在源区域之上只描边）
    void setOffsetBoundaryPolygons(const std::vector<Polygon2D>& polygons);
    void clearOffsetBoundaryPolygons();

    /// 设置高亮边索引（对应 m_mergedPolygons 中的多边形，其轮廓将被高亮绘制）
    void setHighlightedEdgeIndices(const QSet<int>& indices);
    void clearHighlightedEdgeIndices();

    /// 设置偏置边界高亮索引（对应 m_offsetBoundaryPolygons 中的多边形）
    void setHighlightedOffsetBoundaryIndices(const QSet<int>& indices);
    void clearHighlightedOffsetBoundaryIndices();

    // ---- 相机状态访问与联动 ----
    float rotationY() const { return m_rotationY; }
    float targetCenterY() const { return m_targetCenter.y(); }
    float distance() const { return m_distance; }
    float polygonYOffset() const { return m_polygonYOffset; }
    void setRotationY(float yaw);
    void setTargetCenterY(float centerY);
    void setDistance(float dist);

signals:
    /// 相机参数变更（由用户交互触发），用于视图联动
    void cameraChanged(float rotationY, float targetCenterY, float distance);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void generateCylinderGeometry();
    void generateGridGeometry();
    void generatePolygonTexture();
    void setupShaders();
    void cleanupGL();

    /// 计算弧段的精确 Y 范围（考虑 bulge，不只是端点）
    static void computeArcYBounds(const Vertex2D& v0, const Vertex2D& v1,
                                  double& outYMin, double& outYMax);

    // Cylinder parameters
    float m_radius = 100.0f;
    float m_height = 100.0f;
    int m_segments = 64;
    int m_arcTessellation = 32;  // segments per arc when tessellating

    // Geometry
    struct Geometry {
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vbo;
        QOpenGLBuffer nbo;
        QOpenGLBuffer ubo;    // UV for cylinder side
        int vertexCount = 0;
    };

    Geometry m_cylinderGeo;
    Geometry m_gridGeo;
    Geometry m_axisGeo;

    // Shaders
    QOpenGLShaderProgram* m_cylinderShader = nullptr;
    QOpenGLShaderProgram* m_lineShader = nullptr;

    // Texture for polygon overlay
    QOpenGLTexture* m_polyTexture = nullptr;
    bool m_textureDirty = false;

    // Merged polygon data (from View 7)
    std::vector<Polygon2D> m_mergedPolygons;
    bool m_hasMergedPolygons = false;
    float m_polygonYOffset = 0.0f;  // Y offset to center polygons on cylinder

    // Offset boundary data (from View 11, overlay on top of merged polygons)
    std::vector<Polygon2D> m_offsetBoundaryPolygons;
    bool m_hasOffsetBoundaries = false;
    // 每项 m_offsetBoundaryPolygons[i] 在 finalResults 中的全局索引
    std::vector<int> m_offsetBoundarySourceIndices;

    // Highlighted edge indices (for tree node selection)
    QSet<int> m_highlightedEdgeIndices;

    // Highlighted offset boundary indices (for offset tree node selection)
    QSet<int> m_highlightedOffsetBoundaryIndices;

    // Camera state
    float m_rotationX = 0.0f;    // pitch (degrees), locked to side view
    float m_rotationY = -45.0f;  // yaw (degrees)
    float m_distance = 500.0f;   // camera distance from origin
    QVector3D m_targetCenter;    // orbit center in world space (supports vertical pan)

    // Mouse interaction
    QPoint m_lastMousePos;
    bool m_isDragging = false;      // Left button: rotate
    bool m_isPanning = false;       // Middle button: pan
    bool m_isZooming = false;       // Right button: zoom

    // Matrices
    QMatrix4x4 m_projection;
    QMatrix4x4 m_view;
    QMatrix4x4 m_model;
};
