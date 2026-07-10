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

    // Camera state
    float m_rotationX = 30.0f;   // pitch (degrees), looking down
    float m_rotationY = -45.0f;  // yaw (degrees)
    float m_distance = 500.0f;   // camera distance from origin
    QPointF m_targetCenter;      // orbit center in world space

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
