package com.gamescrcpy.server.uhid;

import java.io.IOException;

public interface UhidFile {

    interface Opener {
        UhidFile open() throws IOException;
    }

    int read(byte[] buffer) throws IOException;

    void write(byte[] event) throws IOException;

    void close();
}
