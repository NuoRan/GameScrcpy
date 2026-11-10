package com.gamescrcpy.server.transport;

import java.io.Closeable;
import java.io.IOException;

public interface Listener extends Closeable {

    Connection accept() throws IOException;
}
