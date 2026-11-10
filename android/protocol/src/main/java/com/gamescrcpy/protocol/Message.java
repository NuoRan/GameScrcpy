package com.gamescrcpy.protocol;

public interface Message extends Struct {

    MessageType type();

    default boolean isValid() {
        return true;
    }
}
