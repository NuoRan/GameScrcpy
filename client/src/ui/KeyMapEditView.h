// Header definition
#ifndef KEYMAPEDITVIEW_H
#define KEYMAPEDITVIEW_H

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QResizeEvent>
#include <QUndoStack>
#include <QUndoCommand>
#include <memory>
#include <vector>
#include "KeyMapBase.h"

class SteerWheelSubItem;

// ---------------------------------------------------------
// 撤销命令：移动项目
// ---------------------------------------------------------
class MoveItemCommand : public QUndoCommand
{
public:
    MoveItemCommand(QGraphicsItem* item, const QPointF& oldPos, const QPointF& newPos,
                    QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
    int id() const override { return 1; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    QGraphicsItem* m_item;
    QPointF m_oldPos;
    QPointF m_newPos;
};

// ---------------------------------------------------------
// 撤销命令：添加项目
// ---------------------------------------------------------
class AddItemCommand : public QUndoCommand
{
public:
    AddItemCommand(QGraphicsScene* scene, QGraphicsItem* item,
                   QUndoCommand* parent = nullptr);
    ~AddItemCommand();
    void undo() override;
    void redo() override;

private:
    QGraphicsScene* m_scene;
    QGraphicsItem* m_item;
    bool m_ownsItem = false;
};

// ---------------------------------------------------------
// 撤销命令：删除项目
// ---------------------------------------------------------
class RemoveItemCommand : public QUndoCommand
{
public:
    RemoveItemCommand(QGraphicsScene* scene, QGraphicsItem* item,
                      QUndoCommand* parent = nullptr);
    ~RemoveItemCommand();
    void undo() override;
    void redo() override;

private:
    QGraphicsScene* m_scene;
    QGraphicsItem* m_item;
    QPointF m_pos;
    bool m_ownsItem = true;
};

// ---------------------------------------------------------
// 键位编辑视图
// 支持撤销/重做、冲突检测
// ---------------------------------------------------------
class KeyMapEditView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit KeyMapEditView(QWidget *parent = nullptr);
    ~KeyMapEditView();

    void attachTo(QWidget* target);

    // 撤销/重做
    QUndoStack* undoStack() const { return m_undoStack; }
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

    // 冲突检测
    bool hasKeyConflict(Qt::Key key, QGraphicsItem* exclude = nullptr) const;
    QList<QGraphicsItem*> getConflictingItems(Qt::Key key, QGraphicsItem* exclude = nullptr) const;

signals:
    void undoAvailableChanged(bool available);
    void redoAvailableChanged(bool available);
    void itemMoved(QGraphicsItem* item, const QPointF& newPos);
    void keyConflictDetected(Qt::Key key, const QList<QGraphicsItem*>& items);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    void keyPressEvent(QKeyEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void clearEditingState();
    void updateSize(const QSize& size);
    void recordMoveStart(QGraphicsItem* item);
    void recordMoveEnd(QGraphicsItem* item);

private:
    QGraphicsScene* m_scene;
    QGraphicsObject* m_editingItem = nullptr;
    QWidget* m_targetWidget = nullptr;

    // 撤销/重做
    QUndoStack* m_undoStack;
    QPointF m_dragStartPos;
    QGraphicsItem* m_draggingItem = nullptr;
    bool m_isDragging = false;
};

#endif // KEYMAPEDITVIEW_H

