#include "KeyMapEditView.h"
#include "KeyMapItems.h"
#include <QDebug>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QEvent>
#include <QPainter>
#include <QShortcut>

// ---------------------------------------------------------
// 撤销命令实现
// ---------------------------------------------------------

MoveItemCommand::MoveItemCommand(QGraphicsItem* item, const QPointF& oldPos, const QPointF& newPos,
                                  QUndoCommand* parent)
    : QUndoCommand(parent), m_item(item), m_oldPos(oldPos), m_newPos(newPos)
{
    setText(QObject::tr("Move Item"));
}

void MoveItemCommand::undo()
{
    m_item->setPos(m_oldPos);
    if (auto* w = dynamic_cast<KeyMapItemSteerWheel*>(m_item)) {
        w->updateSubItemsPos();
    }
}

void MoveItemCommand::redo()
{
    m_item->setPos(m_newPos);
    if (auto* w = dynamic_cast<KeyMapItemSteerWheel*>(m_item)) {
        w->updateSubItemsPos();
    }
}

bool MoveItemCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) return false;
    const MoveItemCommand* cmd = static_cast<const MoveItemCommand*>(other);
    if (cmd->m_item != m_item) return false;
    m_newPos = cmd->m_newPos;
    return true;
}

AddItemCommand::AddItemCommand(QGraphicsScene* scene, QGraphicsItem* item, QUndoCommand* parent)
    : QUndoCommand(parent), m_scene(scene), m_item(item)
{
    setText(QObject::tr("Add Item"));
}

AddItemCommand::~AddItemCommand()
{
    if (m_ownsItem) {
        delete m_item;
    }
}

void AddItemCommand::undo()
{
    m_scene->removeItem(m_item);
    m_ownsItem = true;
}

void AddItemCommand::redo()
{
    m_scene->addItem(m_item);
    m_ownsItem = false;
}

RemoveItemCommand::RemoveItemCommand(QGraphicsScene* scene, QGraphicsItem* item, QUndoCommand* parent)
    : QUndoCommand(parent), m_scene(scene), m_item(item)
{
    m_pos = item->pos();
    setText(QObject::tr("Remove Item"));
}

RemoveItemCommand::~RemoveItemCommand()
{
    if (m_ownsItem) {
        delete m_item;
    }
}

void RemoveItemCommand::undo()
{
    m_scene->addItem(m_item);
    m_item->setPos(m_pos);
    m_ownsItem = false;
}

void RemoveItemCommand::redo()
{
    m_scene->removeItem(m_item);
    m_ownsItem = true;
}

// ---------------------------------------------------------
// 构造与初始化
// 设置透明背景、抗锯齿以及接受拖拽事件
// ---------------------------------------------------------
KeyMapEditView::KeyMapEditView(QWidget *parent) : QGraphicsView(parent)
{
    setStyleSheet("background: rgba(0, 0, 0, 150); border: none;");
    setRenderHint(QPainter::Antialiasing);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAcceptDrops(true);
    m_scene = new QGraphicsScene(this);
    m_scene->setBackgroundBrush(Qt::NoBrush);
    setScene(m_scene);

    // 初始化撤销栈
    m_undoStack = new QUndoStack(this);
    connect(m_undoStack, &QUndoStack::canUndoChanged, this, &KeyMapEditView::undoAvailableChanged);
    connect(m_undoStack, &QUndoStack::canRedoChanged, this, &KeyMapEditView::redoAvailableChanged);

    // 设置快捷键
    QShortcut* undoShortcut = new QShortcut(QKeySequence::Undo, this);
    connect(undoShortcut, &QShortcut::activated, this, &KeyMapEditView::undo);

    QShortcut* redoShortcut = new QShortcut(QKeySequence::Redo, this);
    connect(redoShortcut, &QShortcut::activated, this, &KeyMapEditView::redo);
}

KeyMapEditView::~KeyMapEditView() {}

// ---------------------------------------------------------
// 撤销/重做方法
// ---------------------------------------------------------
void KeyMapEditView::undo()
{
    if (m_undoStack->canUndo()) {
        m_undoStack->undo();
    }
}

void KeyMapEditView::redo()
{
    if (m_undoStack->canRedo()) {
        m_undoStack->redo();
    }
}

bool KeyMapEditView::canUndo() const
{
    return m_undoStack->canUndo();
}

bool KeyMapEditView::canRedo() const
{
    return m_undoStack->canRedo();
}

// ---------------------------------------------------------
// 冲突检测
// ---------------------------------------------------------
bool KeyMapEditView::hasKeyConflict(Qt::Key key, QGraphicsItem* exclude) const
{
    return !getConflictingItems(key, exclude).isEmpty();
}

QList<QGraphicsItem*> KeyMapEditView::getConflictingItems(Qt::Key key, QGraphicsItem* exclude) const
{
    QList<QGraphicsItem*> conflicts;

    for (auto* item : m_scene->items()) {
        if (item == exclude) continue;

        // 检查各种键位类型
        if (auto* script = dynamic_cast<KeyMapItemScript*>(item)) {
            // TODO: 获取 script 的绑定按键并比较
        } else if (auto* cam = dynamic_cast<KeyMapItemCamera*>(item)) {
            // TODO: 获取 camera 的绑定按键并比较
        }
        // 可以扩展其他类型
    }

    return conflicts;
}

// ---------------------------------------------------------
// 记录移动操作（用于撤销/重做）
// ---------------------------------------------------------
void KeyMapEditView::recordMoveStart(QGraphicsItem* item)
{
    m_draggingItem = item;
    m_dragStartPos = item->pos();
    m_isDragging = true;
}

void KeyMapEditView::recordMoveEnd(QGraphicsItem* item)
{
    if (m_isDragging && m_draggingItem == item) {
        QPointF endPos = item->pos();

        // 如果位置确实改变了，则添加到撤销栈
        if (m_dragStartPos != endPos) {
            m_undoStack->push(new MoveItemCommand(item, m_dragStartPos, endPos));
            emit itemMoved(item, endPos);
        }
    }
    m_isDragging = false;
    m_draggingItem = nullptr;
}

// ---------------------------------------------------------
// 附加到目标窗口
// 将此视图覆盖在视频渲染窗口之上，并安装事件过滤器以跟随大小变化
// ---------------------------------------------------------
void KeyMapEditView::attachTo(QWidget* target)
{
    if (!target) return;
    m_targetWidget = target;
    setParent(target);
    m_targetWidget->installEventFilter(this);
    setGeometry(target->rect());
    updateSize(target->size());
    hide();
}

bool KeyMapEditView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_targetWidget && event->type() == QEvent::Resize) {
        setGeometry(m_targetWidget->rect());
        updateSize(m_targetWidget->size());
    }
    return QGraphicsView::eventFilter(watched, event);
}

// ---------------------------------------------------------
// 尺寸自适应逻辑
// 当窗口大小改变时，保持所有键位元素的相对位置比例不变
// ---------------------------------------------------------
void KeyMapEditView::updateSize(const QSize& size)
{
    QSize oldSize = m_scene->sceneRect().size().toSize();
    if (oldSize.isEmpty() || oldSize.width() <= 0) {
        m_scene->setSceneRect(0, 0, size.width(), size.height());
        return;
    }
    QList<QPair<QGraphicsItem*, QPointF>> ratios;
    for (auto item : m_scene->items()) {
        if (dynamic_cast<KeyMapItemBase*>(item)) {
            QPointF p = item->pos();
            ratios.append({item, QPointF(p.x()/oldSize.width(), p.y()/oldSize.height())});
        }
    }
    m_scene->setSceneRect(0, 0, size.width(), size.height());
    for (auto& pair : ratios) {
        pair.first->setPos(pair.second.x()*size.width(), pair.second.y()*size.height());
        if (auto* w = dynamic_cast<KeyMapItemSteerWheel*>(pair.first)) w->updateSubItemsPos();
    }
}

void KeyMapEditView::resizeEvent(QResizeEvent *event) {
    QGraphicsView::resizeEvent(event);
    if (scene()) scene()->setSceneRect(rect());
}

void KeyMapEditView::showEvent(QShowEvent *event) { Q_UNUSED(event); raise(); }

// ---------------------------------------------------------
// 拖拽事件处理
// 处理从工具栏拖入的新键位（轮盘、脚本、视角等）
// ---------------------------------------------------------
void KeyMapEditView::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat("application/x-keymap-type")) event->acceptProposedAction();
}
void KeyMapEditView::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasFormat("application/x-keymap-type")) event->acceptProposedAction();
}
void KeyMapEditView::dropEvent(QDropEvent *event) {
    if (event->mimeData()->hasFormat("application/x-keymap-type")) {
        QByteArray data = event->mimeData()->data("application/x-keymap-type");
        KeyMapType type = (KeyMapType)data.toInt();

        if (type != KMT_STEER_WHEEL && type != KMT_SCRIPT && type != KMT_CAMERA_MOVE) return;

        KeyMapFactoryImpl factory;
        KeyMapItemBase* item = factory.createItem(type);
        if (item) {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
            QPointF dropPos = mapToScene(event->position().toPoint());
#else
            QPointF dropPos = mapToScene(event->pos());
#endif
            item->setPos(dropPos);

            // 使用撤销命令添加项目
            m_undoStack->push(new AddItemCommand(m_scene, item));

            m_scene->clearSelection();
            item->setSelected(true);
            if (auto* w = dynamic_cast<KeyMapItemSteerWheel*>(item)) w->updateSubItemsPos();
        }
        event->acceptProposedAction();
    }
}

// ---------------------------------------------------------
// 编辑状态管理
// 清除当前正在编辑的元素的编辑状态
// ---------------------------------------------------------
void KeyMapEditView::clearEditingState() {
    if (m_editingItem) {
        if (auto* sub = dynamic_cast<SteerWheelSubItem*>(m_editingItem)) sub->setEditing(false);
        else if (auto* script = dynamic_cast<KeyMapItemScript*>(m_editingItem)) script->setEditing(false);
        else if (auto* cam = dynamic_cast<KeyMapItemCamera*>(m_editingItem)) cam->setEditing(false);
        m_editingItem = nullptr;
    }
}

// ---------------------------------------------------------
// 鼠标交互事件
// 处理点击选中、激活编辑模式（如点击视角键位调整灵敏度）
// ---------------------------------------------------------
void KeyMapEditView::mousePressEvent(QMouseEvent *event)
{
    event->accept();

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    QPointF scenePos = mapToScene(event->position().toPoint());
    QGraphicsItem* clickedItem = itemAt(event->position().toPoint());
#else
    QPointF scenePos = mapToScene(event->pos());
    QGraphicsItem* clickedItem = itemAt(event->pos());
#endif

    // 记录拖拽开始位置
    if (clickedItem && dynamic_cast<KeyMapItemBase*>(clickedItem)) {
        recordMoveStart(clickedItem);
    }

    // 如果当前有正在编辑的项
    if (m_editingItem) {
        if (m_editingItem != clickedItem) {
            clearEditingState();
        } else {
            // 特殊处理视角控制键位的点击（判断是点击了XY编辑区还是按键区）
            if (auto* cam = dynamic_cast<KeyMapItemCamera*>(m_editingItem)) {
                QPointF localPos = cam->mapFromScene(scenePos);
                if (localPos.x() < -20 || localPos.x() > 20) {
                    cam->startEditing(localPos);
                    return;
                }
            }
            // 处理鼠标按键录入
            // 注意：左键也可以录入，因为正在编辑模式下点击同一个item应该录入按键而不是拖拽
            if (auto* sub = dynamic_cast<SteerWheelSubItem*>(m_editingItem)) { if(sub->isEditing()) { sub->inputMouse(event->button()); return; } }
            else if (auto* script = dynamic_cast<KeyMapItemScript*>(m_editingItem)) { if(script->isEditing()) { script->inputMouse(event->button()); return; } }
            else if (auto* cam = dynamic_cast<KeyMapItemCamera*>(m_editingItem)) { if(cam->isEditing()) { cam->inputMouse(event->button()); return; } }
        }
    }

    // 点击新元素触发编辑（主要是视角控制的XY值区域）
    if (clickedItem && !m_editingItem) {
        if (auto* cam = dynamic_cast<KeyMapItemCamera*>(clickedItem)) {
            QPointF localPos = cam->mapFromScene(scenePos);
            if (localPos.x() < -20 || localPos.x() > 20) {
                cam->startEditing(localPos);
                m_editingItem = cam;
                return;
            }
        }
    }

    QGraphicsView::mousePressEvent(event);
    if (!clickedItem) m_scene->clearSelection();
}

void KeyMapEditView::mouseReleaseEvent(QMouseEvent *event)
{
    // 记录拖拽结束
    if (m_isDragging && m_draggingItem) {
        recordMoveEnd(m_draggingItem);
    }

    QGraphicsView::mouseReleaseEvent(event);
    event->accept();
}

void KeyMapEditView::mouseMoveEvent(QMouseEvent *event) { QGraphicsView::mouseMoveEvent(event); event->accept(); }
void KeyMapEditView::wheelEvent(QWheelEvent *event) {
    event->accept();
    if (m_editingItem) {
        if (auto* sub = dynamic_cast<SteerWheelSubItem*>(m_editingItem)) { if(sub->isEditing()) sub->inputWheel(event->angleDelta().y()); }
    }
}

// ---------------------------------------------------------
// 双击事件
// 双击键位进入按键绑定编辑模式
// ---------------------------------------------------------
void KeyMapEditView::mouseDoubleClickEvent(QMouseEvent *event) {
    event->accept();

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    QPointF scenePos = mapToScene(event->position().toPoint());
    QGraphicsItem* item = itemAt(event->position().toPoint());
#else
    QPointF scenePos = mapToScene(event->pos());
    QGraphicsItem* item = itemAt(event->pos());
#endif

    if (!item) { clearEditingState(); return; }

    if (event->button() == Qt::LeftButton) {
        if (m_editingItem && m_editingItem == item) {
        }
    }

    if (m_editingItem != item) clearEditingState();

    if (auto* sub = dynamic_cast<SteerWheelSubItem*>(item)) {
        sub->setEditing(true); m_editingItem = sub;
    } else if (auto* script = dynamic_cast<KeyMapItemScript*>(item)) {
        script->setEditing(true); m_editingItem = script;
    } else if (auto* cam = dynamic_cast<KeyMapItemCamera*>(item)) {
        QPointF localPos = cam->mapFromScene(scenePos);
        cam->startEditing(localPos);
        m_editingItem = cam;
    }
}

// ---------------------------------------------------------
// 键盘事件
// 处理编辑模式下的按键录入，或删除选中的键位
// ---------------------------------------------------------
void KeyMapEditView::keyPressEvent(QKeyEvent *event) {
    event->accept();
    // 如果处于编辑模式，录入按键
    if (m_editingItem) {
        bool editing = false;
        if (auto* sub = dynamic_cast<SteerWheelSubItem*>(m_editingItem)) editing = sub->isEditing();
        else if (auto* script = dynamic_cast<KeyMapItemScript*>(m_editingItem)) editing = script->isEditing();
        else if (auto* cam = dynamic_cast<KeyMapItemCamera*>(m_editingItem)) editing = cam->isEditing();

        if (editing) {
            if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Escape) clearEditingState();
            else {
                if (auto* sub = dynamic_cast<SteerWheelSubItem*>(m_editingItem)) sub->inputKey(event);
                else if (auto* script = dynamic_cast<KeyMapItemScript*>(m_editingItem)) script->inputKey(event);
                else if (auto* cam = dynamic_cast<KeyMapItemCamera*>(m_editingItem)) cam->inputKey(event);
            }
            return;
        }
    }

    // 删除选中项（使用撤销命令）
    if (event->key() == Qt::Key_Delete) {
        QList<QGraphicsItem*> selectedItems = m_scene->selectedItems();
        if (!selectedItems.isEmpty()) {
            m_undoStack->beginMacro(tr("Delete Items"));
            for (auto* item : selectedItems) {
                if (auto* sub = dynamic_cast<SteerWheelSubItem*>(item)) {
                    if (sub->parentItem()) {
                        QGraphicsItem* top = sub->parentItem();
                        m_undoStack->push(new RemoveItemCommand(m_scene, top));
                    }
                } else if (auto* base = dynamic_cast<KeyMapItemBase*>(item)) {
                    m_undoStack->push(new RemoveItemCommand(m_scene, base));
                }
            }
            m_undoStack->endMacro();
        }
    }
}


