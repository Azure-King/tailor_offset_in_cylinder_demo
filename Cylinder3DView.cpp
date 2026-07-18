#include "Cylinder3DView.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Cylinder3DView::Cylinder3DView(QWidget* parent)
    : QOpenGLWidget(parent), m_targetCenter(0.0f, 0.0f) {
    setMinimumSize(300, 200);
    setMouseTracking(false);
}

Cylinder3DView::~Cylinder3DView() {
    makeCurrent();
    cleanupGL();
    doneCurrent();
}

void Cylinder3DView::setCylinderRadius(float radius) {
    m_radius = radius;
    if (isValid()) {
        makeCurrent();
        generateCylinderGeometry();
        if (m_hasMergedPolygons) {
            generatePolygonTexture();
        }
        doneCurrent();
        update();
    }
}

void Cylinder3DView::setCylinderHeight(float height) {
    m_height = height;
    if (isValid()) {
        makeCurrent();
        generateCylinderGeometry();
        generateGridGeometry();
        if (m_hasMergedPolygons) {
            generatePolygonTexture();
        }
        doneCurrent();
        update();
    }
}

// ==================== 弧段 Y 范围计算（考虑 bulge） ====================

void Cylinder3DView::computeArcYBounds(const Vertex2D& v0, const Vertex2D& v1,
                                       double& outYMin, double& outYMax) {
    double ay = v0.point.y();
    double by = v1.point.y();
    double bulge = v0.bulge;  // bulge is stored on the start vertex (v0)

    outYMin = std::min(ay, by);
    outYMax = std::max(ay, by);

    if (std::abs(bulge) < 1e-10) {
        return;  // straight line, endpoints are the bounds
    }

    // Arc center and radius — same formulas as tessellateArc
    double ax = v0.point.x(), bx = v1.point.x();
    double b = 0.5 * (1.0 / bulge - bulge);
    double cx = 0.5 * (ax + bx - b * (by - ay));
    double cy = 0.5 * (ay + by + b * (bx - ax));
    double Rc = std::sqrt((ax - cx) * (ax - cx) + (ay - cy) * (ay - cy));

    // Start angle, sweep angle, and direction — same as tessellateArc
    double startAngle = std::atan2(ay - cy, ax - cx);
    double sweepAngle = 4.0 * std::atan(std::abs(bulge));
    int dir = (bulge > 0.0) ? 1 : -1;

    // Analytically check if the arc passes through the top (angle=π/2)
    // or bottom (angle=-π/2) of the circle.
    // Returns true if targetAngle lies on the arc from startAngle to
    // startAngle + dir*sweepAngle (winding in direction dir).
    auto angleOnArc = [&](double target) -> bool {
        // Signed angular distance from startAngle to target following dir
        double dist = dir * (target - startAngle);
        // Normalize to [0, 2π)
        dist = std::fmod(dist, 2.0 * M_PI);
        if (dist < 0.0) dist += 2.0 * M_PI;
        return dist <= sweepAngle + 1e-12;
    };

    // Circle top: y = cy + Rc at angle π/2
    if (angleOnArc(M_PI / 2.0)) {
        outYMax = cy + Rc;
    }
    // Circle bottom: y = cy - Rc at angle -π/2 (equivalently 3π/2)
    if (angleOnArc(-M_PI / 2.0)) {
        outYMin = cy - Rc;
    }
}

// ==================== 多边形数据设置 & 自动调整高度 ====================

void Cylinder3DView::setMergedPolygons(
    const std::vector<Polygon2D>& polygons) {
    m_mergedPolygons = polygons;
    m_hasMergedPolygons = !polygons.empty();
    m_highlightedEdgeIndices.clear();  // 新数据到达时清除旧的高亮

    if (!m_hasMergedPolygons) {
        update();
        return;
    }

    // Step 1: 计算多边形的精确 Y 范围（考虑 arc bulge）
    double yMin =  std::numeric_limits<double>::max();
    double yMax = -std::numeric_limits<double>::max();

    for (const auto& poly : m_mergedPolygons) {
        int n = static_cast<int>(poly.vertices.size());
        if (n < 2) continue;
        for (int i = 0; i < n; ++i) {
            const auto& v0 = poly.vertices[i];
            const auto& v1 = poly.vertices[(i + 1) % n];
            double eYMin, eYMax;
            computeArcYBounds(v0, v1, eYMin, eYMax);
            yMin = std::min(yMin, eYMin);
            yMax = std::max(yMax, eYMax);
        }
    }

    if (yMax <= yMin) {
        update();
        return;
    }

    // Step 2: 自动设置圆柱高度（20% margin）
    double margin = (yMax - yMin) * 0.2;
    double newHeight = (yMax - yMin) + 2.0 * margin;
    m_polygonYOffset = static_cast<float>(-(yMin + yMax) / 2.0);

    // Only update geometry if OpenGL context is valid
    if (isValid()) {
        makeCurrent();
        m_height = static_cast<float>(newHeight);
        generateCylinderGeometry();
        generateGridGeometry();
        generatePolygonTexture();
        doneCurrent();
        update();
    } else {
        m_height = static_cast<float>(newHeight);
        m_textureDirty = true;  // 延迟到 initializeGL 时生成
    }
}

void Cylinder3DView::clearMergedPolygons() {
    m_mergedPolygons.clear();
    m_hasMergedPolygons = false;
    m_highlightedEdgeIndices.clear();

    if (m_polyTexture) {
        delete m_polyTexture;
        m_polyTexture = nullptr;
    }
    update();
}

void Cylinder3DView::setHighlightedEdgeIndices(const QSet<int>& indices) {
    m_highlightedEdgeIndices = indices;
    if (isValid() && m_hasMergedPolygons) {
        makeCurrent();
        generatePolygonTexture();
        doneCurrent();
        update();
    }
}

void Cylinder3DView::clearHighlightedEdgeIndices() {
    m_highlightedEdgeIndices.clear();
    if (isValid() && m_hasMergedPolygons) {
        makeCurrent();
        generatePolygonTexture();
        doneCurrent();
        update();
    }
}

void Cylinder3DView::initializeGL() {
    initializeOpenGLFunctions();

    glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_MULTISAMPLE);

    setupShaders();
    generateCylinderGeometry();
    generateGridGeometry();

    // 如果在 setMergedPolygons 时 GL 尚未初始化，现在补生成纹理
    if (m_hasMergedPolygons && m_textureDirty) {
        generatePolygonTexture();
    }
    m_textureDirty = false;
}

void Cylinder3DView::setupShaders() {
    // ============ 3D shading shader (with texture overlay) ============
    m_cylinderShader = new QOpenGLShaderProgram(this);

    const char* vertexShaderSrc = R"(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        layout(location = 1) in vec3 aNormal;
        layout(location = 2) in vec2 aTexCoord;

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;

        out vec3 FragPos;
        out vec3 Normal;
        out vec2 TexCoord;

        void main() {
            FragPos = vec3(model * vec4(aPos, 1.0));
            Normal = mat3(transpose(inverse(model))) * aNormal;
            TexCoord = aTexCoord;
            gl_Position = projection * view * model * vec4(aPos, 1.0);
        }
    )";

    const char* fragmentShaderSrc = R"(
        #version 330 core
        out vec4 FragColor;

        in vec3 FragPos;
        in vec3 Normal;
        in vec2 TexCoord;

        uniform sampler2D overlayTex;
        uniform bool hasOverlay;
        uniform vec3 lightPos;
        uniform vec3 fillLightPos;
        uniform vec3 lightColor;
        uniform vec3 objectColor;
        uniform vec3 viewPos;

        void main() {
            vec3 norm = normalize(Normal);
            vec3 viewDir = normalize(viewPos - FragPos);

            // === Key light (main directional) ===
            float ambientStrength = 0.15;
            vec3 ambient = ambientStrength * lightColor;

            vec3 lightDir = normalize(lightPos - FragPos);
            float diff = max(dot(norm, lightDir), 0.0);
            vec3 diffuse = diff * lightColor;

            vec3 reflectDir = reflect(-lightDir, norm);
            float specularStrength = 0.3;
            float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
            vec3 specular = specularStrength * spec * lightColor;

            vec3 keyLight = ambient + diffuse + specular;

            // === Fill light (soft, from opposite side) ===
            float fillAmbient = 0.1;
            vec3 fillDir = normalize(fillLightPos - FragPos);
            float fillDiff = max(dot(norm, fillDir), 0.0);
            vec3 fill = (fillAmbient + fillDiff * 0.35) * lightColor;

            vec3 shaded = (keyLight + fill) * objectColor;

            // === Texture overlay (only on cylinder side, not caps) ===
            if (hasOverlay && abs(norm.y) < 0.9) {
                vec4 texColor = texture(overlayTex, TexCoord);
                if (texColor.a > 0.01) {
                    shaded = mix(shaded, texColor.rgb, texColor.a * 0.9);
                }
            }

            FragColor = vec4(shaded, 1.0);
        }
    )";

    m_cylinderShader->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSrc);
    m_cylinderShader->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSrc);
    m_cylinderShader->link();

    // ============ Line/Lattice shader (for grid & axes) ============
    m_lineShader = new QOpenGLShaderProgram(this);

    const char* lineVertSrc = R"(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        void main() {
            gl_Position = projection * view * model * vec4(aPos, 1.0);
        }
    )";

    const char* lineFragSrc = R"(
        #version 330 core
        out vec4 FragColor;
        uniform vec3 lineColor;
        void main() {
            FragColor = vec4(lineColor, 1.0);
        }
    )";

    m_lineShader->addShaderFromSourceCode(QOpenGLShader::Vertex, lineVertSrc);
    m_lineShader->addShaderFromSourceCode(QOpenGLShader::Fragment, lineFragSrc);
    m_lineShader->link();
}

void Cylinder3DView::generateCylinderGeometry() {
    if (!isValid()) return;

    // Destroy old VAO/VBOs
    if (m_cylinderGeo.vao.isCreated()) {
        m_cylinderGeo.vao.destroy();
        m_cylinderGeo.vbo.destroy();
        m_cylinderGeo.nbo.destroy();
        m_cylinderGeo.ubo.destroy();
    }

    float halfH = m_height / 2.0f;
    int segs = m_segments;

    std::vector<float> vertices;
    std::vector<float> normals;
    std::vector<float> uvs;

    auto addVertex = [&](float x, float y, float z, float nx, float ny, float nz,
                          float u, float v) {
        vertices.push_back(x);
        vertices.push_back(y);
        vertices.push_back(z);
        normals.push_back(nx);
        normals.push_back(ny);
        normals.push_back(nz);
        uvs.push_back(u);
        uvs.push_back(v);
    };

    // === Side surface (CCW winding viewed from outside) ===
    // Start from theta=-π (seam, x=-R) so U=0 aligns with texture's x=-πR
    for (int i = 0; i < segs; ++i) {
        float theta0 = -static_cast<float>(M_PI) + 2.0f * static_cast<float>(M_PI) * i / segs;
        float theta1 = -static_cast<float>(M_PI) + 2.0f * static_cast<float>(M_PI) * (i + 1) / segs;

        float cos0 = std::cos(theta0), sin0 = std::sin(theta0);
        float cos1 = std::cos(theta1), sin1 = std::sin(theta1);

        float x0 = m_radius * cos0, z0 = m_radius * sin0;
        float x1 = m_radius * cos1, z1 = m_radius * sin1;

        float u0 = static_cast<float>(i) / segs;
        float u1 = static_cast<float>(i + 1) / segs;

        // Triangle 1: bottom0, top0, top1
        addVertex(x0, -halfH, z0, cos0, 0.0f, sin0, u0, 0.0f);
        addVertex(x0,  halfH, z0, cos0, 0.0f, sin0, u0, 1.0f);
        addVertex(x1,  halfH, z1, cos1, 0.0f, sin1, u1, 1.0f);

        // Triangle 2: bottom0, top1, bottom1
        addVertex(x0, -halfH, z0, cos0, 0.0f, sin0, u0, 0.0f);
        addVertex(x1,  halfH, z1, cos1, 0.0f, sin1, u1, 1.0f);
        addVertex(x1, -halfH, z1, cos1, 0.0f, sin1, u1, 0.0f);
    }

    // === Top cap (CCW, sits directly on side surface seam) ===
    for (int i = 0; i < segs; ++i) {
        float theta0 = -static_cast<float>(M_PI) + 2.0f * static_cast<float>(M_PI) * i / segs;
        float theta1 = -static_cast<float>(M_PI) + 2.0f * static_cast<float>(M_PI) * (i + 1) / segs;

        addVertex(0.0f, halfH, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f);
        addVertex(m_radius * std::cos(theta1), halfH, m_radius * std::sin(theta1), 0.0f, 1.0f, 0.0f, 0.5f, 0.5f);
        addVertex(m_radius * std::cos(theta0), halfH, m_radius * std::sin(theta0), 0.0f, 1.0f, 0.0f, 0.5f, 0.5f);
    }

    // === Bottom cap (CCW) ===
    for (int i = 0; i < segs; ++i) {
        float theta0 = -static_cast<float>(M_PI) + 2.0f * static_cast<float>(M_PI) * i / segs;
        float theta1 = -static_cast<float>(M_PI) + 2.0f * static_cast<float>(M_PI) * (i + 1) / segs;

        addVertex(0.0f, -halfH, 0.0f, 0.0f, -1.0f, 0.0f, 0.5f, 0.5f);
        addVertex(m_radius * std::cos(theta0), -halfH, m_radius * std::sin(theta0), 0.0f, -1.0f, 0.0f, 0.5f, 0.5f);
        addVertex(m_radius * std::cos(theta1), -halfH, m_radius * std::sin(theta1), 0.0f, -1.0f, 0.0f, 0.5f, 0.5f);
    }

    m_cylinderGeo.vertexCount = static_cast<int>(vertices.size() / 3);

    m_cylinderGeo.vao.create();
    m_cylinderGeo.vao.bind();

    // VBO for positions
    m_cylinderGeo.vbo.create();
    m_cylinderGeo.vbo.bind();
    m_cylinderGeo.vbo.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    // VBO for normals
    m_cylinderGeo.nbo.create();
    m_cylinderGeo.nbo.bind();
    m_cylinderGeo.nbo.allocate(normals.data(), static_cast<int>(normals.size() * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    // VBO for UVs
    m_cylinderGeo.ubo.create();
    m_cylinderGeo.ubo.bind();
    m_cylinderGeo.ubo.allocate(uvs.data(), static_cast<int>(uvs.size() * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    m_cylinderGeo.vao.release();
}

void Cylinder3DView::generateGridGeometry() {
    if (!isValid()) return;

    const float gridSize = 400.0f;
    const float gridStep = 25.0f;
    const float yLevel = -m_height / 2.0f;  // grid on the bottom plane

    std::vector<float> gridVerts;

    // Grid lines parallel to X axis
    for (float z = -gridSize; z <= gridSize; z += gridStep) {
        gridVerts.push_back(-gridSize); gridVerts.push_back(yLevel); gridVerts.push_back(z);
        gridVerts.push_back( gridSize); gridVerts.push_back(yLevel); gridVerts.push_back(z);
    }
    // Grid lines parallel to Z axis
    for (float x = -gridSize; x <= gridSize; x += gridStep) {
        gridVerts.push_back(x); gridVerts.push_back(yLevel); gridVerts.push_back(-gridSize);
        gridVerts.push_back(x); gridVerts.push_back(yLevel); gridVerts.push_back( gridSize);
    }

    int gridVertexCount = static_cast<int>(gridVerts.size() / 3);

    // Destroy old
    if (m_gridGeo.vao.isCreated()) {
        m_gridGeo.vao.destroy();
        m_gridGeo.vbo.destroy();
    }

    m_gridGeo.vertexCount = gridVertexCount;
    m_gridGeo.vao.create();
    m_gridGeo.vao.bind();
    m_gridGeo.vbo.create();
    m_gridGeo.vbo.bind();
    m_gridGeo.vbo.allocate(gridVerts.data(), static_cast<int>(gridVerts.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    m_gridGeo.vao.release();

    // === Axis lines ===
    std::vector<float> axisVerts = {
        // X axis (red)
        0.0f, yLevel, 0.0f,  200.0f, yLevel, 0.0f,
        // Y axis (green)
        0.0f, yLevel, 0.0f,  0.0f, yLevel + 200.0f, 0.0f,
        // Z axis (blue)
        0.0f, yLevel, 0.0f,  0.0f, yLevel, 200.0f,
    };

    int axisVertexCount = static_cast<int>(axisVerts.size() / 3);

    if (m_axisGeo.vao.isCreated()) {
        m_axisGeo.vao.destroy();
        m_axisGeo.vbo.destroy();
    }

    m_axisGeo.vertexCount = axisVertexCount;
    m_axisGeo.vao.create();
    m_axisGeo.vao.bind();
    m_axisGeo.vbo.create();
    m_axisGeo.vbo.bind();
    m_axisGeo.vbo.allocate(axisVerts.data(), static_cast<int>(axisVerts.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    m_axisGeo.vao.release();
}

void Cylinder3DView::cleanupGL() {
    if (m_cylinderGeo.vao.isCreated()) {
        m_cylinderGeo.vao.destroy();
        m_cylinderGeo.vbo.destroy();
        m_cylinderGeo.nbo.destroy();
        m_cylinderGeo.ubo.destroy();
    }
    if (m_gridGeo.vao.isCreated()) {
        m_gridGeo.vao.destroy();
        m_gridGeo.vbo.destroy();
    }
    if (m_axisGeo.vao.isCreated()) {
        m_axisGeo.vao.destroy();
        m_axisGeo.vbo.destroy();
    }
    delete m_polyTexture;
    m_polyTexture = nullptr;
    delete m_cylinderShader;
    delete m_lineShader;
    m_cylinderShader = nullptr;
    m_lineShader = nullptr;
}

// ==================== 多边形纹理生成（贴到圆柱表面） ====================

void Cylinder3DView::generatePolygonTexture() {
    if (!isValid() || !m_hasMergedPolygons) return;

    float R = m_radius;
    float halfH = m_height / 2.0f;
    float yOffset = m_polygonYOffset;
    int tess = m_arcTessellation;

    // Step 0: 从多边形数据中提取实际 X 范围，计算居中偏移量。
    // 周期裁剪后的多边形 X 范围 = [boundaryLeft, boundaryRight]，
    // 而圆柱的 unwrapped X 范围总是 [-πR, πR] 中心在原点。
    // 当边界不对称时两者中心不一致，需要将多边形居中。
    double xDataMin =  std::numeric_limits<double>::max();
    double xDataMax = -std::numeric_limits<double>::max();
    for (const auto& poly : m_mergedPolygons) {
        for (const auto& v : poly.vertices) {
            double vx = v.point.x();
            if (vx < xDataMin) xDataMin = vx;
            if (vx > xDataMax) xDataMax = vx;
        }
    }
    double xCenter = (xDataMin + xDataMax) * 0.5;

    // 圆柱带范围：X ∈ [-πR, πR]，Y ∈ [-halfH, halfH]
    double xMin = -M_PI * R;
    double xMax =  M_PI * R;
    double yMin = -halfH;
    double yMax =  halfH;

    // 纹理尺寸
    const int texW = 2048;
    double aspect = (xMax - xMin) / std::max(yMax - yMin, 1.0);
    int texH = std::max(4, static_cast<int>(texW / aspect));
    // 限制纹理高度避免过大
    texH = std::min(texH, 4096);

    // 世界坐标 → 纹理像素坐标
    // 先居中多边形 X 坐标（使数据中心对齐圆柱中心），再进行水平翻转
    auto worldToPixel = [&](double x, double y) -> QPointF {
        double xc = x - xCenter;  // 居中
        double u = 1.0 - (xc - xMin) / (xMax - xMin);
        double v = (y - yMin) / (yMax - yMin);
        return QPointF(u * texW, v * texH);
    };

    // 弧段 tessellation
    auto tessellateArc = [&](const Vertex2D& v0, const Vertex2D& v1) -> std::vector<QPointF> {
        std::vector<QPointF> pts;
        double bulge = v0.bulge;  // v0 is the start vertex of edge v0→v1
        double ax = v0.point.x(), ay = v0.point.y();
        double bx = v1.point.x(), by = v1.point.y();

        if (std::abs(bulge) < 1e-10 || tess <= 0) {
            pts.push_back(v0.point);
            pts.push_back(v1.point);
            return pts;
        }

        double b = 0.5 * (1.0 / bulge - bulge);
        double cx = 0.5 * (ax + bx - b * (by - ay));
        double cy = 0.5 * (ay + by + b * (bx - ax));
        double Rc = std::sqrt((ax - cx) * (ax - cx) + (ay - cy) * (ay - cy));

        double startAngle = std::atan2(ay - cy, ax - cx);
        double sweepAngle = 4.0 * std::atan(std::abs(bulge));
        int dir = (bulge > 0.0) ? 1 : -1;

        pts.push_back(v0.point);
        for (int i = 1; i < tess; ++i) {
            double angle = startAngle + dir * (sweepAngle * i / tess);
            pts.push_back(QPointF(cx + Rc * std::cos(angle),
                                  cy + Rc * std::sin(angle)));
        }
        pts.push_back(v1.point);
        return pts;
    };

    // 创建 QImage 并用 QPainter 绘制多边形
    QImage img(texW, texH, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Lambda: build QPainterPath for a single polygon
    auto buildPathForPolygon = [&](const Polygon2D& poly) -> QPainterPath {
        int n = static_cast<int>(poly.vertices.size());
        QPainterPath path;
        if (n < 2) return path;

        bool firstPoint = true;
        for (int i = 0; i < n; ++i) {
            const auto& v0 = poly.vertices[i];
            int next = (i + 1) % n;
            const auto& v1 = poly.vertices[next];

            double wy0 = v0.point.y() + yOffset;
            double wy1 = v1.point.y() + yOffset;

            auto pts = tessellateArc(v0, v1);
            for (size_t j = 0; j < pts.size(); ++j) {
                QPointF tp;
                if (j == 0 && i != 0) {
                    continue;
                }
                double py = pts[j].y() + yOffset;
                tp = worldToPixel(pts[j].x(), py);
                if (firstPoint) {
                    path.moveTo(tp);
                    firstPoint = false;
                } else {
                    path.lineTo(tp);
                }
            }
        }
        path.closeSubpath();
        return path;
    };

    // 第一遍：绘制所有多边形的填充和普通边框
    for (int idx = 0; idx < (int)m_mergedPolygons.size(); ++idx) {
        const auto& poly = m_mergedPolygons[idx];
        if (poly.vertices.size() < 2) continue;
        if (poly.isOpen) continue;

        QColor fillColor = poly.color.isValid() ? poly.color : QColor(200, 200, 200);
        fillColor.setAlpha(220);

        QPainterPath path = buildPathForPolygon(poly);

        // 填充多边形
        painter.fillPath(path, fillColor);

        // 普通边框
        QPen pen(Qt::white, 1.5);
        painter.setPen(pen);
        painter.drawPath(path);
    }

    // 第二遍：对有高亮索引的多边形绘制发光边框（不覆盖填充，只叠加描边）
    if (!m_highlightedEdgeIndices.empty()) {
        // 高亮描边辅助：用发光笔绘制一个 QPainterPath
        auto drawGlow = [&](const QPainterPath& path) {
            // 外层光晕（半透明宽线）
            painter.setPen(QPen(QColor(255, 180, 0, 80), 30.0));
            painter.drawPath(path);
            // 中层光晕
            painter.setPen(QPen(QColor(255, 200, 0, 180), 15.0));
            painter.drawPath(path);
            // 核心高亮线（不透明，醒目）
            painter.setPen(QPen(QColor(255, 230, 30, 255), 5.0));
            painter.drawPath(path);
        };

        for (int polyIdx : m_highlightedEdgeIndices) {
            if (polyIdx < 0 || polyIdx >= (int)m_mergedPolygons.size()) continue;
            const auto& poly = m_mergedPolygons[polyIdx];
            if (poly.vertices.size() < 2) continue;

            if (poly.isOpen) {
                // 开放路径：逐段边绘制（避免闭合造成跨条带直线）
                // 对于 N 个弧段的开放路径，vertices 有 N+1 个顶点
                int n = static_cast<int>(poly.vertices.size());
                for (int j = 0; j < n - 1; ++j) {
                    const auto& v0 = poly.vertices[j];
                    const auto& v1 = poly.vertices[j + 1];
                    auto pts = tessellateArc(v0, v1);

                    QPainterPath edgePath;
                    for (size_t pi = 0; pi < pts.size(); ++pi) {
                        QPointF tp = worldToPixel(pts[pi].x(), pts[pi].y() + yOffset);
                        if (pi == 0) edgePath.moveTo(tp);
                        else edgePath.lineTo(tp);
                    }
                    drawGlow(edgePath);
                }
            } else {
                // 闭合路径：绘制完整轮廓
                QPainterPath path = buildPathForPolygon(poly);
                drawGlow(path);
            }
        }
    }
    painter.end();

    // 创建/更新 OpenGL 纹理
    if (m_polyTexture) {
        m_polyTexture->destroy();
        delete m_polyTexture;
    }

    m_polyTexture = new QOpenGLTexture(QOpenGLTexture::Target2D);
    m_polyTexture->setData(img);
    // 使用 Linear（无 mipmap）避免细线（如高亮边）在远处因 mipmap 平均而消失
    m_polyTexture->setMinificationFilter(QOpenGLTexture::Linear);
    m_polyTexture->setMagnificationFilter(QOpenGLTexture::Linear);
    m_polyTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
}

void Cylinder3DView::resizeGL(int w, int h) {
    float aspect = static_cast<float>(w) / std::max(h, 1);
    m_projection.setToIdentity();
    m_projection.perspective(45.0f, aspect, 10.0f, 5000.0f);
}

void Cylinder3DView::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Build view matrix from orbit parameters
    float rx = m_rotationX * static_cast<float>(M_PI) / 180.0f;
    float ry = m_rotationY * static_cast<float>(M_PI) / 180.0f;

    QVector3D eye(
        m_distance * std::cos(rx) * std::sin(ry),
        m_distance * std::sin(rx),
        m_distance * std::cos(rx) * std::cos(ry)
    );

    QVector3D center(m_targetCenter.x(), 0.0f, m_targetCenter.y());
    QVector3D up(0.0f, 1.0f, 0.0f);

    m_view.setToIdentity();
    m_view.lookAt(eye, center, up);

    // ============ Draw grid ============
    m_lineShader->bind();
    m_lineShader->setUniformValue("projection", m_projection);
    m_lineShader->setUniformValue("view", m_view);
    m_model.setToIdentity();
    m_lineShader->setUniformValue("model", m_model);
    m_lineShader->setUniformValue("lineColor", QVector3D(0.25f, 0.25f, 0.3f));

    m_gridGeo.vao.bind();
    glDrawArrays(GL_LINES, 0, m_gridGeo.vertexCount);
    m_gridGeo.vao.release();

    // ============ Draw axes ============
    // X axis - red
    m_axisGeo.vao.bind();
    m_lineShader->setUniformValue("lineColor", QVector3D(0.9f, 0.2f, 0.2f));
    glDrawArrays(GL_LINES, 0, 2);

    // Y axis - green
    m_lineShader->setUniformValue("lineColor", QVector3D(0.2f, 0.9f, 0.2f));
    glDrawArrays(GL_LINES, 2, 2);

    // Z axis - blue
    m_lineShader->setUniformValue("lineColor", QVector3D(0.2f, 0.4f, 0.9f));
    glDrawArrays(GL_LINES, 4, 2);
    m_axisGeo.vao.release();
    m_lineShader->release();

    // ============ Draw cylinder ============
    m_cylinderShader->bind();
    m_cylinderShader->setUniformValue("projection", m_projection);
    m_cylinderShader->setUniformValue("view", m_view);
    m_model.setToIdentity();
    m_cylinderShader->setUniformValue("model", m_model);

    // Place lights relative to camera so they rotate naturally with view
    QVector3D forward = (center - eye).normalized();
    QVector3D right = QVector3D::crossProduct(forward, up).normalized();
    QVector3D camUp = QVector3D::crossProduct(right, forward).normalized();

    // Key light: upper-right of camera
    QVector3D keyLightPos = center + right * 400.0f + camUp * 300.0f + forward * 100.0f;
    // Fill light: lower-left of camera (opposite side, softer)
    QVector3D fillLightPos = center - right * 250.0f - camUp * 150.0f + forward * 50.0f;

    m_cylinderShader->setUniformValue("lightPos", keyLightPos);
    m_cylinderShader->setUniformValue("fillLightPos", fillLightPos);
    m_cylinderShader->setUniformValue("lightColor", QVector3D(1.0f, 1.0f, 0.95f));
    m_cylinderShader->setUniformValue("objectColor", QVector3D(0.35f, 0.55f, 0.8f));
    m_cylinderShader->setUniformValue("viewPos", eye);

    // Bind polygon overlay texture
    bool hasOverlay = (m_polyTexture != nullptr && m_polyTexture->isCreated());
    m_cylinderShader->setUniformValue("hasOverlay", hasOverlay);
    if (hasOverlay) {
        glActiveTexture(GL_TEXTURE0);
        m_polyTexture->bind();
        m_cylinderShader->setUniformValue("overlayTex", 0);
    }

    m_cylinderGeo.vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, m_cylinderGeo.vertexCount);
    m_cylinderGeo.vao.release();

    if (hasOverlay) {
        m_polyTexture->release();
    }
    m_cylinderShader->release();

    // Overlay HUD text
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QColor(200, 200, 200));
    painter.setFont(QFont("Segoe UI", 9));
    painter.drawText(10, height() - 10,
        QString("Radius: %1  Height: %2  Segments: %3").arg(m_radius, 0, 'f', 1).arg(m_height, 0, 'f', 1).arg(m_segments));
    painter.end();
}

void Cylinder3DView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else if (event->button() == Qt::MiddleButton) {
        m_isPanning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::SizeAllCursor);
        event->accept();
    } else if (event->button() == Qt::RightButton) {
        m_isZooming = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::SizeVerCursor);
        event->accept();
    } else {
        QOpenGLWidget::mousePressEvent(event);
    }
}

void Cylinder3DView::mouseMoveEvent(QMouseEvent* event) {
    QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();

    if (m_isDragging) {
        // 旋转：鼠标右拖 → 物体右侧转过来
        // 鼠标上拖 → 物体顶部向你倾斜
        m_rotationY -= delta.x() * 0.4f;
        m_rotationX += delta.y() * 0.4f;

        // Clamp pitch to avoid flipping
        m_rotationX = std::clamp(m_rotationX, -89.0f, 89.0f);

        update();
        event->accept();
    } else if (m_isPanning) {
        // 中键平移：沿屏幕 XY 方向移动目标中心点
        float rx = m_rotationX * static_cast<float>(M_PI) / 180.0f;
        float ry = m_rotationY * static_cast<float>(M_PI) / 180.0f;

        // 屏幕右方向在世界坐标中的近似方向
        float viewCosY = std::cos(ry);
        float viewSinY = std::sin(ry);

        // 平移灵敏度随距离缩放
        float panScale = m_distance * 0.001f;
        m_targetCenter += QPointF(
            viewCosY * delta.x() * panScale - viewSinY * delta.y() * panScale,
            viewSinY * delta.x() * panScale + viewCosY * delta.y() * panScale
        );

        update();
        event->accept();
    } else if (m_isZooming) {
        // 右键拖拽缩放：上拖放大，下拖缩小
        float zoomFactor = 1.0f - delta.y() * 0.005f;
        m_distance *= zoomFactor;
        m_distance = std::clamp(m_distance, 50.0f, 3000.0f);

        update();
        event->accept();
    } else {
        QOpenGLWidget::mouseMoveEvent(event);
    }
}

void Cylinder3DView::mouseReleaseEvent(QMouseEvent* event) {
    bool handled = false;
    if (event->button() == Qt::LeftButton && m_isDragging) {
        m_isDragging = false;
        handled = true;
    } else if (event->button() == Qt::MiddleButton && m_isPanning) {
        m_isPanning = false;
        handled = true;
    } else if (event->button() == Qt::RightButton && m_isZooming) {
        m_isZooming = false;
        handled = true;
    }

    if (handled) {
        setCursor(Qt::ArrowCursor);
        event->accept();
    } else {
        QOpenGLWidget::mouseReleaseEvent(event);
    }
}

void Cylinder3DView::wheelEvent(QWheelEvent* event) {
    float delta = event->angleDelta().y() / 120.0f;
    m_distance *= std::pow(0.9f, delta);
    m_distance = std::clamp(m_distance, 50.0f, 3000.0f);
    update();
    event->accept();
}
