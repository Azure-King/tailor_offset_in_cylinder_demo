#include <QApplication>
#include <QMainWindow>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QVector>

#include <QAction>
#include <QToolBar>
#include <QMenu>
#include <algorithm>
#include <QFileDialog>
#include <QKeySequence>
#include <QBrush>
#include <QColorDialog>
#include <QMessageBox>

#include "Sketch2DView.h"
#include "FourViewContainer.h"
#include "Cylinder3DView.h"
#include "PeriodicClippingViews.h"
#include "CylindricalOffsetViews.h"
#include "PolygonIO.h"

#include <QTabWidget>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle("TailorOffsetDemo");
    window.resize(1200, 700);

    // 创建主水平分割器
    auto* mainSplitter = new QSplitter(Qt::Horizontal, &window);

    // 创建左侧垂直分割器，用于分隔输入和区域结果模型树
    auto* leftSplitter = new QSplitter(Qt::Vertical, mainSplitter);
    leftSplitter->setMinimumWidth(150);
    leftSplitter->setMaximumWidth(300);

    // ==================== 输入模型树（上面部分） ====================
    auto* inputModelTree = new QTreeWidget(leftSplitter);
    inputModelTree->setHeaderLabel("输入模型");
    inputModelTree->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // 输入多段线节点
    auto* polylinesItem = new QTreeWidgetItem(inputModelTree);
    polylinesItem->setText(0, "多段线");
    polylinesItem->setExpanded(true);

    // 输入多边形节点
    auto* polygonsItem = new QTreeWidgetItem(inputModelTree);
    polygonsItem->setText(0, "多边形");
    polygonsItem->setExpanded(true);

    // ==================== 区域结果模型树（第二部分） ====================
    auto* regionResultTree = new QTreeWidget(leftSplitter);
    regionResultTree->setHeaderLabel("区域结果");
    regionResultTree->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // 设置左侧分割器的比例（输入在上，区域结果在下）
    leftSplitter->setStretchFactor(0, 1);  // 输入模型树
    leftSplitter->setStretchFactor(1, 1);  // 区域结果模型树
    leftSplitter->setSizes({ 150, 150 });

    // 使用 FourViewContainer 替代单个 Sketch2DView
    auto* viewContainer = new FourViewContainer(&window);
    viewContainer->setMinimumSize(800, 300);

    auto* view = viewContainer->mainView(); // 主视图用于工具选择
    view->setCylinderRadius(100.0f);  // set boundary width to match cylinder circumference

    // === 分页控件：切换流水线视图 / 周期裁剪视图 / 圆柱偏置视图 ===
    auto* tabWidget = new QTabWidget();
    tabWidget->setTabPosition(QTabWidget::North);

    // ---- Tab 1: 流水线（上排双视图 + 下排3D圆柱，可拖动） ----
    auto* tab1Container = new QWidget();
    auto* tab1Layout = new QVBoxLayout(tab1Container);
    tab1Layout->setContentsMargins(0, 0, 0, 0);
    tab1Layout->setSpacing(0);

    auto* tab1Splitter = new QSplitter(Qt::Vertical, tab1Container);
    tab1Splitter->addWidget(viewContainer);  // 上排：四视图容器

    auto* tab1CylinderPlaceholder = new QWidget(tab1Splitter);
    auto* tab1PhLayout = new QVBoxLayout(tab1CylinderPlaceholder);
    tab1PhLayout->setContentsMargins(0, 0, 0, 0);
    tab1PhLayout->setSpacing(0);
    tab1Splitter->addWidget(tab1CylinderPlaceholder);

    // 上下初始比例接近 1:1，加大圆柱视图占比，允许用户拖动
    tab1Splitter->setSizes({400, 500});
    tab1Splitter->setStretchFactor(0, 1);
    tab1Splitter->setStretchFactor(1, 1);

    tab1Layout->addWidget(tab1Splitter);
    tabWidget->addTab(tab1Container, "流水线 (1-2)");

    // ---- Tab 2: 周期裁剪（2x2 网格，右下为3D圆柱）----
    auto* periodicViews = new PeriodicClippingViews(tabWidget);
    tabWidget->addTab(periodicViews, "周期裁剪 (3-5)");

    // ---- Tab 3: 圆柱偏置（2x2 网格，右下为3D圆柱）----
    auto* offsetViews = new CylindricalOffsetViews(tabWidget);
    tabWidget->addTab(offsetViews, "圆柱偏置 (6-8)");

    // 第五视图：3D 圆柱视图（单例，通过 reparent 在各 tab 间切换）
    auto* cylinder3DView = new Cylinder3DView();
    cylinder3DView->setMinimumSize(400, 200);

    // 初始放置到 Tab 1 的占位区
    tab1CylinderPlaceholder->layout()->addWidget(cylinder3DView);

    // Tab 切换时 re-parent 3D 视图到对应占位区
    QObject::connect(tabWidget, &QTabWidget::currentChanged,
        [tabWidget, cylinder3DView, tab1CylinderPlaceholder, periodicViews, offsetViews](int index) {
            // 关键：不能用 setParent(nullptr)，会销毁 QOpenGLWidget 的 OpenGL 上下文！
            // 先用 removeWidget 从旧布局移除（不改变父控件关系，不销毁上下文）
            QWidget* oldParent = cylinder3DView->parentWidget();
            if (oldParent && oldParent->layout()) {
                oldParent->layout()->removeWidget(cylinder3DView);
            }

            // 再 reparent 到新占位区
            QWidget* newParent = nullptr;
            switch (index) {
            case 0: newParent = tab1CylinderPlaceholder; break;
            case 1: newParent = periodicViews->cylinderPlaceholder(); break;
            case 2: newParent = offsetViews->cylinderPlaceholder(); break;
            }

            if (newParent) {
                cylinder3DView->setParent(newParent);
                newParent->layout()->addWidget(cylinder3DView);
                cylinder3DView->show();
            }
        });

    // 连接：边界线变更 → 圆柱半径更新（从宽度反算半径 R = width / 2π）
    QObject::connect(viewContainer, &FourViewContainer::boundariesUpdated,
                     cylinder3DView, [cylinder3DView](float left, float right) {
                         float radius = (right - left) / (2.0f * 3.14159265358979323846f);
                         cylinder3DView->setCylinderRadius(radius);
                     });

    // 连接：周期裁剪结果 → 更新第3、4视图
    QObject::connect(viewContainer, &FourViewContainer::periodicClipResultReady,
                     periodicViews, [periodicViews](
                         const QVector<Sketch2DView::OffsetResultPolygon>& before,
                         const QVector<Sketch2DView::OffsetResultPolygon>& after) {
                         periodicViews->setBeforePolygons(before);
                         periodicViews->setAfterPolygons(after);
                     });

    // 连接：圆柱区域合并结果 → 展示到 View 5 + 贴到3D圆柱面
    QObject::connect(viewContainer, &FourViewContainer::periodicCylindricalResultReady,
                     periodicViews, [periodicViews, cylinder3DView](
                         const QVector<Sketch2DView::OffsetResultPolygon>& cylindrical) {
                         // View 5 展示圆柱区域着色多边形
                         periodicViews->setMergedPolygons(cylindrical);

                         // 同时将多边形贴到 3D 圆柱面
                         std::vector<Cylinder3DView::Polygon2D> mergedPolys;
                         for (const auto& poly : cylindrical) {
                             Cylinder3DView::Polygon2D mp;
                             mp.color = poly.color;
                             mp.isOpen = poly.isOpen;
                             for (const auto& v : poly.vertices) {
                                 Cylinder3DView::Vertex2D pv;
                                 pv.point = v.point;
                                 pv.bulge = v.bulge;
                                 mp.vertices.push_back(pv);
                             }
                             mergedPolys.push_back(mp);
                         }
                         cylinder3DView->setMergedPolygons(mergedPolys);
                     });

    // 连接：边界线同步到周期裁剪视图的子视图
    QObject::connect(viewContainer, &FourViewContainer::boundariesUpdated,
                     periodicViews, [periodicViews](float left, float right) {
                         periodicViews->setBoundaryLines(left, right);
                     });

    // 连接：边界线同步到圆柱偏置视图
    QObject::connect(viewContainer, &FourViewContainer::boundariesUpdated,
                     offsetViews, [offsetViews](float left, float right) {
                         offsetViews->setBoundaryLines(left, right);
                     });

    // 连接：圆柱偏置距离变更 → 触发偏置流水线
    QObject::connect(offsetViews, &CylindricalOffsetViews::offsetDistanceChanged,
                     viewContainer, &FourViewContainer::processCylindricalOffset);

    // 连接：圆柱偏置结果 → 更新偏置视图 + 3D 圆柱面
    QObject::connect(viewContainer, &FourViewContainer::cylindricalOffsetResultReady,
                     offsetViews, [offsetViews, cylinder3DView](
                         const QVector<Sketch2DView::OffsetResultPolygon>& boundaries,
                         const QVector<Sketch2DView::OffsetResultPolygon>& booleanRst,
                         const QVector<Sketch2DView::OffsetResultPolygon>& finalRst) {
                         // View 6, 7, 8 展示偏置流水线结果
                         offsetViews->setOffsetBoundaryResults(boundaries);
                         offsetViews->setBooleanResults(booleanRst);
                         offsetViews->setFinalResults(finalRst);

                         // 将偏置后的圆柱区域边界叠加到 3D 圆柱面（不覆盖源区域填充）
                         std::vector<Cylinder3DView::Polygon2D> offsetPolys;
                         for (const auto& poly : finalRst) {
                             Cylinder3DView::Polygon2D mp;
                             mp.color = poly.color;
                             mp.isOpen = poly.isOpen;
                             for (const auto& v : poly.vertices) {
                                 Cylinder3DView::Vertex2D pv;
                                 pv.point = v.point;
                                 pv.bulge = v.bulge;
                                 mp.vertices.push_back(pv);
                             }
                             offsetPolys.push_back(mp);
                         }
                         cylinder3DView->setOffsetBoundaryPolygons(offsetPolys);
                     });

    // 连接：周期裁剪结果变更时，同步触发圆柱偏置（如果有偏置距离）
    QObject::connect(viewContainer, &FourViewContainer::periodicCylindricalResultReady,
                     offsetViews, [viewContainer, offsetViews](
                         const QVector<Sketch2DView::OffsetResultPolygon>&) {
                         // 周期裁剪结果更新后，重新计算圆柱偏置
                         viewContainer->processCylindricalOffset(
                             offsetViews->offsetDistance());
                     });

    // 连接：区域树更新 → 刷新左侧区域结果模型树
    QObject::connect(viewContainer, &FourViewContainer::regionTreeUpdated,
                     [viewContainer, regionResultTree]() {
                         viewContainer->buildRegionTree(regionResultTree);
                     });

    // 连接：区域结果树选中 → 按归属分别高亮原始/偏置区域
    QObject::connect(regionResultTree, &QTreeWidget::itemSelectionChanged,
                     [regionResultTree, periodicViews, cylinder3DView]() {
                         QSet<int> origIndices;
                         QSet<int> offsetIndices;
                         for (auto* item : regionResultTree->selectedItems()) {
                             // 判断该节点属于"原始区域"还是"偏置区域"
                             bool isOffset = false;
                             QTreeWidgetItem* ancestor = item->parent();
                             while (ancestor) {
                                 // 顶层标题节点不可选中，用它来判定归属
                                 if (!ancestor->flags().testFlag(Qt::ItemIsSelectable)) {
                                     if (ancestor->text(0) == QStringLiteral("\u504f\u7f6e\u533a\u57df"))
                                         isOffset = true;
                                     break;
                                 }
                                 ancestor = ancestor->parent();
                             }

                             QVector<int> idxList = item->data(0, Qt::UserRole).value<QVector<int>>();
                             auto& dst = isOffset ? offsetIndices : origIndices;
                             for (int idx : idxList) {
                                 if (idx >= 0) dst.insert(idx);
                             }
                         }
                         periodicViews->mergedView()->setHighlightedFillResultIndices(origIndices);
                         cylinder3DView->setHighlightedEdgeIndices(origIndices);
                         cylinder3DView->setHighlightedOffsetBoundaryIndices(offsetIndices);
                     });

    // ========================================
    // 视图联动：View 5 ↔ View 8 ↔ Cylinder3DView
    // ========================================
    auto* view5 = periodicViews->mergedView();
    auto* view8 = offsetViews->finalResultView();

    // View 5 → View 8: 双向同步 pan + zoom（视图范围始终保持一致）
    QObject::connect(view5, &Sketch2DView::viewChanged,
        view8, [view8](qreal scale, QPointF offset) {
            view8->setScale(scale);
            view8->setOffset(offset);
        });

    // View 8 → View 5: 双向同步
    QObject::connect(view8, &Sketch2DView::viewChanged,
        view5, [view5](qreal scale, QPointF offset) {
            view5->setScale(scale);
            view5->setOffset(offset);
        });

    // View 5/8 垂直平移 → 3D 圆柱中心 Y 偏移
    // 关键：2D 视图世界坐标 Y 和 3D 圆柱 Y 之间差一个 polygonYOffset
    // 因为多边形投影到圆柱面时会通过 polygonYOffset 居中
    QObject::connect(view5, &Sketch2DView::viewChanged,
        cylinder3DView, [cylinder3DView](qreal, QPointF offset) {
            float centerY = static_cast<float>(offset.y());
            centerY += cylinder3DView->polygonYOffset();
            cylinder3DView->setTargetCenterY(centerY);
        });
    QObject::connect(view8, &Sketch2DView::viewChanged,
        cylinder3DView, [cylinder3DView](qreal, QPointF offset) {
            float centerY = static_cast<float>(offset.y());
            centerY += cylinder3DView->polygonYOffset();
            cylinder3DView->setTargetCenterY(centerY);
        });

    // 3D 圆柱中心 Y 变化 → View 5/8 垂直平移
    QObject::connect(cylinder3DView, &Cylinder3DView::cameraChanged,
        view5, [view5, cylinder3DView](float /*ry*/, float centerY, float /*dist*/) {
            QPointF offset = view5->offset();
            offset.setY(static_cast<qreal>(centerY - cylinder3DView->polygonYOffset()));
            view5->setOffset(offset);
        });
    QObject::connect(cylinder3DView, &Cylinder3DView::cameraChanged,
        view8, [view8, cylinder3DView](float /*ry*/, float centerY, float /*dist*/) {
            QPointF offset = view8->offset();
            offset.setY(static_cast<qreal>(centerY - cylinder3DView->polygonYOffset()));
            view8->setOffset(offset);
        });

    mainSplitter->addWidget(leftSplitter);
    mainSplitter->addWidget(tabWidget);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({ 300, 1000 });

    window.setCentralWidget(mainSplitter);

    auto* toolbar = window.addToolBar("Tools");
    toolbar->setMovable(false);

    QAction* polygonAction = toolbar->addAction("Polygon");
    polygonAction->setCheckable(true);

    QAction* polylineAction = toolbar->addAction("Polyline");
    polylineAction->setCheckable(true);

    QAction* importAction = toolbar->addAction("Import");
    importAction->setShortcut(QKeySequence("Ctrl+I"));

    // 设置多边形为默认模式
    polygonAction->setChecked(true);
    view->setTool(Sketch2DView::Tool::Polygon);

    QObject::connect(polylineAction, &QAction::triggered, [&]() {
        polylineAction->setChecked(true);
        polygonAction->setChecked(false);
        view->setTool(Sketch2DView::Tool::Polyline);
        });

    QObject::connect(polygonAction, &QAction::triggered, [&]() {
        polygonAction->setChecked(true);
        polylineAction->setChecked(false);
        view->setTool(Sketch2DView::Tool::Polygon);
        });

    QObject::connect(importAction, &QAction::triggered, [&]() {
        QString filePath = QFileDialog::getOpenFileName(nullptr, "导入多边形", "", "Polygon Files (*.txt);;All Files (*)");
        if (filePath.isEmpty()) {
            return;
        }

        std::vector<tailor_visualization::Polygon> importedPolygons;
        bool success = tailor_visualization::PolygonIO::ImportFromFile(filePath, importedPolygons);

        if (!success || importedPolygons.empty()) {
            return;
        }

        // 获取当前的多边形，添加新的
        QVector<Sketch2DView::Polygon> currentPolygons = view->polygons();

        for (size_t i = 0; i < importedPolygons.size(); ++i) {
            const auto& polygon = importedPolygons[i];
            Sketch2DView::Polygon newPolygon;
            for (const auto& edge : polygon.edges) {
                Sketch2DView::PolygonVertex vertex;
                vertex.point = QPointF(edge.startPoint.x, edge.startPoint.y);
                vertex.bulge = edge.bulge;
                newPolygon.vertices.append(vertex);
            }

            // 添加到视图
            currentPolygons.append(newPolygon);
        }

        // 更新视图
        view->setPolygons(currentPolygons);

        // 触发完整计算流水线（包含同步视图）
        viewContainer->runFullPipeline();

        // 更新模型树
        int startIndex = polygonsItem->childCount();
        for (size_t i = 0; i < importedPolygons.size(); ++i) {
            QString name = QString("多边形%1").arg(startIndex + (int)i + 1);
            auto* item = new QTreeWidgetItem(polygonsItem);
            item->setText(0, name);
            item->setData(0, Qt::UserRole, 1); // 1 = polygon
        }

        });

    // 多段线添加信号
    QObject::connect(view, &Sketch2DView::polylineAdded, [polylinesItem](int index, const QString& name) {
        auto* item = new QTreeWidgetItem(polylinesItem);
        item->setText(0, name);
        item->setData(0, Qt::UserRole, 0); // 0 = polyline
        });

    // 多边形添加信号
    QObject::connect(view, &Sketch2DView::polygonAdded, [polygonsItem](int index, const QString& name) {
        auto* item = new QTreeWidgetItem(polygonsItem);
        item->setText(0, name);
        item->setData(0, Qt::UserRole, 1); // 1 = polygon
        });

    // ==================== 输入模型树选择变化 ====================
    QObject::connect(inputModelTree, &QTreeWidget::itemSelectionChanged, [inputModelTree, view]() {
        auto selectedItems = inputModelTree->selectedItems();

        // 清除主视图的选择
        view->clearSelection();

        if (selectedItems.isEmpty()) {
            return;
        }

        // 收集需要选中的类型和索引
        int selectedPolylineIndex = -1;
        int selectedPolygonIndex = -1;
        QSet<int> polylineIndices;
        QSet<int> polygonIndices;

        for (QTreeWidgetItem* item : selectedItems) {
            if (!item->parent()) {
                continue;
            }

            int type = item->data(0, Qt::UserRole).toInt();
            int index = item->parent()->indexOfChild(item);

            if (type == 0) { // polyline
                polylineIndices.insert(index);
                selectedPolylineIndex = index;
            } else if (type == 1) { // polygon
                polygonIndices.insert(index);
                selectedPolygonIndex = index;
            }
        }

        // 处理主视图中的多边形/多段线选择
        for (int index : polylineIndices) {
            view->addSelectedPolyline(index);
        }
        for (int index : polygonIndices) {
            view->addSelectedPolygon(index);
        }

        // 如果只有一个选中项，设置为主选择
        if (polylineIndices.size() + polygonIndices.size() == 1) {
            if (selectedPolylineIndex >= 0) {
                view->setSelectedPolygon(selectedPolylineIndex, false);
            } else if (selectedPolygonIndex >= 0) {
                view->setSelectedPolygon(selectedPolygonIndex, true);
            }
        }
        });

    // ==================== 输入模型树右键菜单 ====================
    inputModelTree->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(inputModelTree, &QTreeWidget::customContextMenuRequested, [inputModelTree, view, viewContainer, polylinesItem, polygonsItem](const QPoint& pos) {
        auto selectedItems = inputModelTree->selectedItems();
        if (selectedItems.isEmpty()) {
            return;
        }

        // 检查是否选中了有效的曲线对象（不是父节点）
        bool hasValidSelection = false;
        for (QTreeWidgetItem* item : selectedItems) {
            if (item->parent()) {
                hasValidSelection = true;
                break;
            }
        }

        if (!hasValidSelection) {
            return;
        }

        // 创建右键菜单
        QMenu menu;
        QAction* deleteAction = nullptr;
        QAction* exportAction = nullptr;
        QAction* changeEdgeColorAction = nullptr;

        // 检查是否有选中的多边形
        bool hasSelectedPolygons = false;
        for (QTreeWidgetItem* item : selectedItems) {
            if (item->parent()) {
                int type = item->data(0, Qt::UserRole).toInt();
                if (type == 1) { // polygon
                    hasSelectedPolygons = true;
                    break;
                }
            }
        }

        // 添加菜单项
        deleteAction = menu.addAction("删除");
        exportAction = menu.addAction("导出");
        if (hasSelectedPolygons) {
            changeEdgeColorAction = menu.addAction("更改边界颜色...");
        }

        QAction* result = menu.exec(inputModelTree->mapToGlobal(pos));

        // 处理更改边界颜色操作
        if (result && result == changeEdgeColorAction) {
            // 收集所有选中多边形的索引
            QSet<int> polygonIndices;
            QColor currentColor = QColor(255, 255, 255);

            for (QTreeWidgetItem* item : selectedItems) {
                if (!item->parent()) continue;
                int type = item->data(0, Qt::UserRole).toInt();
                if (type == 1) { // polygon
                    int index = item->parent()->indexOfChild(item);
                    if (index < 0) continue;

                    const auto& polygons = view->polygons();
                    if (index >= polygons.size()) continue;

                    if (!polygons[index].vertices.isEmpty()) {
                        currentColor = polygons[index].vertices[0].edgeColor;
                    }
                    polygonIndices.insert(index);
                }
            }

            if (polygonIndices.isEmpty()) return;

            // 打开颜色选择对话框
            QColor newColor = QColorDialog::getColor(currentColor, inputModelTree, "选择边界颜色");
            if (newColor.isValid()) {
                // 批量设置所有选中多边形的颜色
                view->setPolygonEdgeColorBatch(polygonIndices, newColor, true);
                // 立即同步其他视图
                QCoreApplication::processEvents();
                viewContainer->synchronizeViews();
            }
            return;
        }

        // 处理删除操作
        if (result == deleteAction) {
            // 收集需要删除的索引（使用 QSet）
            QSet<int> polylineIndicesToDelete;
            QSet<int> polygonIndicesToDelete;

            for (QTreeWidgetItem* item : selectedItems) {
                if (item->parent()) {
                    int type = item->data(0, Qt::UserRole).toInt();
                    int index = item->parent()->indexOfChild(item);

                    if (type == 0) { // polyline
                        polylineIndicesToDelete.insert(index);
                    } else if (type == 1) { // polygon
                        polygonIndicesToDelete.insert(index);
                    }
                }
            }

            // 删除视图中的多段线
            view->deletePolylines(polylineIndicesToDelete);

            // 删除视图中的多边形
            view->deletePolygons(polygonIndicesToDelete);

            // 删除模型树中的项（先收集项指针，避免迭代器失效）
            QList<QTreeWidgetItem*> itemsToDelete;
            for (QTreeWidgetItem* item : selectedItems) {
                if (item->parent()) {
                    itemsToDelete.append(item);
                }
            }

            // 删除模型树中的项
            for (QTreeWidgetItem* item : itemsToDelete) {
                delete item;
            }
            // 立即同步其他视图
            viewContainer->synchronizeViews();
        } else if (result == exportAction) {
            // 导出选中的多边形到文件
            QString filePath = QFileDialog::getSaveFileName(nullptr, "导出多边形", "", "Polygon Files (*.txt);;All Files (*)");
            if (filePath.isEmpty()) {
                return;
            }

            // 收集需要导出的多边形
            std::vector<tailor_visualization::Polygon> polygonsToExport;

            for (QTreeWidgetItem* item : selectedItems) {
                if (!item->parent()) {
                    continue;
                }

                int type = item->data(0, Qt::UserRole).toInt();
                int index = item->parent()->indexOfChild(item);

                tailor_visualization::Polygon polygon;

                if (type == 0) { // polyline - 导出为封闭形式
                    const auto& polylines = view->polylines();
                    if (index >= 0 && index < polylines.size()) {
                        const auto& polyline = polylines[index];
                        int n = polyline.vertices.size();
                        // 正向边
                        for (int j = 0; j < n - 1; ++j) {
                            const auto& vertex = polyline.vertices[j];
                            const auto& nextVertex = polyline.vertices[j + 1];
                            tailor_visualization::PolygonEdge edge;
                            edge.startPoint.x = vertex.point.x();
                            edge.startPoint.y = vertex.point.y();
                            edge.endPoint.x = nextVertex.point.x();
                            edge.endPoint.y = nextVertex.point.y();
                            edge.bulge = vertex.bulge;
                            polygon.edges.push_back(edge);
                        }
                        // 反向边
                        for (int j = n - 1; j > 0; --j) {
                            const auto& vertex = polyline.vertices[j];
                            const auto& prevVertex = polyline.vertices[j - 1];
                            tailor_visualization::PolygonEdge edge;
                            edge.startPoint.x = vertex.point.x();
                            edge.startPoint.y = vertex.point.y();
                            edge.endPoint.x = prevVertex.point.x();
                            edge.endPoint.y = prevVertex.point.y();
                            edge.bulge = -prevVertex.bulge;
                            polygon.edges.push_back(edge);
                        }
                    }
                } else if (type == 1) { // polygon
                    const auto& polygons = view->polygons();
                    if (index >= 0 && index < polygons.size()) {
                        const auto& poly = polygons[index];
                        for (int j = 0; j < poly.vertices.size(); ++j) {
                            const auto& vertex = poly.vertices[j];
                            const auto& nextVertex = poly.vertices[(j + 1) % poly.vertices.size()];

                            tailor_visualization::PolygonEdge edge;
                            edge.startPoint.x = vertex.point.x();
                            edge.startPoint.y = vertex.point.y();
                            edge.endPoint.x = nextVertex.point.x();
                            edge.endPoint.y = nextVertex.point.y();
                            edge.bulge = vertex.bulge;
                            polygon.edges.push_back(edge);
                        }
                    }
                }

                if (!polygon.edges.empty()) {
                    polygonsToExport.push_back(polygon);
                }
            }

            // 导出到文件
            if (!polygonsToExport.empty()) {
                tailor_visualization::PolygonIO::ExportToFile(filePath.toStdString(), polygonsToExport);
            }
        }
        });

    window.show();

    return app.exec();
}
