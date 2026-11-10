#include "JsSequence.h"

#include <QJSValue>
#include <QMetaContainer>
#include <QMetaSequence>
#include <QMetaType>
#include <QSequentialIterable>
#include <QString>
#include <QVariant>
#include <QVariantList>

#include <algorithm>

namespace gs::app {

namespace {

using Face = QtMetaContainerPrivate::QMetaSequenceInterface;
using Position = QtMetaContainerPrivate::QMetaContainerInterface::Position;

struct Cursor {
    const QJSValue* array = nullptr;
    quint32 index = 0;
};

const QJSValue& arrayOf(const void* container)
{
    return *static_cast<const QJSValue*>(container);
}

const Cursor& cursorOf(const void* iterator)
{
    return *static_cast<const Cursor*>(iterator);
}

quint32 lengthOf(const QJSValue& array)
{
    return static_cast<quint32>(std::max(0, array.property(QStringLiteral("length")).toInt()));
}

void* cursorAt(const void* container, Position position)
{
    const QJSValue& array = arrayOf(container);
    return new Cursor{.array = &array, .index = position == Position::AtEnd ? lengthOf(array) : 0U};
}

void* mutableCursorAt(void* container, Position position)
{
    return cursorAt(container, position);
}

void destroyCursor(const void* iterator)
{
    delete static_cast<const Cursor*>(iterator);
}

bool sameCursor(const void* a, const void* b)
{
    return cursorOf(a).array == cursorOf(b).array && cursorOf(a).index == cursorOf(b).index;
}

void copyCursor(void* target, const void* source)
{
    *static_cast<Cursor*>(target) = cursorOf(source);
}

void advanceCursor(void* iterator, qsizetype step)
{
    static_cast<Cursor*>(iterator)->index += static_cast<quint32>(step);
}

qsizetype cursorDistance(const void* a, const void* b)
{
    return static_cast<qsizetype>(cursorOf(a).index) - static_cast<qsizetype>(cursorOf(b).index);
}

void valueAtCursor(const void* iterator, void* value)
{
    const Cursor& cursor = cursorOf(iterator);
    *static_cast<QVariant*>(value) = cursor.array->property(cursor.index).toVariant();
}

qsizetype arraySize(const void* container)
{
    return lengthOf(arrayOf(container));
}

void valueAtIndex(const void* container, qsizetype index, void* value)
{
    *static_cast<QVariant*>(value) = arrayOf(container).property(static_cast<quint32>(index)).toVariant();
}

Face arrayFace()
{
    Face face{};
    face.iteratorCapabilities = QtMetaContainerPrivate::ForwardCapability | QtMetaContainerPrivate::BiDirectionalCapability
                                | QtMetaContainerPrivate::RandomAccessCapability;
    face.valueMetaType = QtPrivate::qMetaTypeInterfaceForType<QVariant>();
    face.sizeFn = &arraySize;
    face.valueAtIndexFn = &valueAtIndex;
    face.createIteratorFn = &mutableCursorAt;
    face.destroyIteratorFn = &destroyCursor;
    face.compareIteratorFn = &sameCursor;
    face.copyIteratorFn = &copyCursor;
    face.advanceIteratorFn = &advanceCursor;
    face.diffIteratorFn = &cursorDistance;
    face.valueAtIteratorFn = &valueAtCursor;
    face.createConstIteratorFn = &cursorAt;
    face.destroyConstIteratorFn = &destroyCursor;
    face.compareConstIteratorFn = &sameCursor;
    face.copyConstIteratorFn = &copyCursor;
    face.advanceConstIteratorFn = &advanceCursor;
    face.diffConstIteratorFn = &cursorDistance;
    face.valueAtConstIteratorFn = &valueAtCursor;
    return face;
}

QSequentialIterable sequenceOf(const QJSValue& value)
{
    if (!value.isArray()) {
        static const QVariantList nothing;
        return QSequentialIterable(QMetaSequence::fromContainer<QVariantList>(), &nothing);
    }
    static Face face = arrayFace();
    return QSequentialIterable(QMetaSequence(&face), &value);
}

}

void installJsSequence()
{
    const QMetaType from = QMetaType::fromType<QJSValue>();
    const QMetaType to = QMetaType::fromType<QSequentialIterable>();
    if (QMetaType::hasRegisteredConverterFunction(from, to)) {
        QMetaType::unregisterConverterFunction(from, to);
    }
    QMetaType::registerConverter<QJSValue, QSequentialIterable>(&sequenceOf);
}

}
