package com.gamescrcpy.protocol;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.NoSuchElementException;
import java.util.function.Supplier;

public final class StructList<T extends Struct> implements Iterable<T> {

    private final Supplier<T> factory;
    private final ArrayList<T> items = new ArrayList<>();
    private int size;

    public StructList(Supplier<T> factory) {
        this.factory = factory;
    }

    public int size() {
        return size;
    }

    public boolean isEmpty() {
        return size == 0;
    }

    public T get(int index) {
        if (index < 0 || index >= size) {
            throw new IndexOutOfBoundsException(index);
        }
        return items.get(index);
    }

    public void add(T item) {
        if (size < items.size()) {
            items.set(size, item);
        } else {
            items.add(item);
        }
        ++size;
    }

    public void clear() {
        size = 0;
    }

    T appendForDecode() {
        if (size == items.size()) {
            items.add(factory.get());
        }
        return items.get(size++);
    }

    @Override
    public Iterator<T> iterator() {
        return new Iterator<>() {
            private int next;

            @Override
            public boolean hasNext() {
                return next < size;
            }

            @Override
            public T next() {
                if (next >= size) {
                    throw new NoSuchElementException();
                }
                return items.get(next++);
            }
        };
    }
}
