package com.gamescrcpy.server.video;

final class SessionCounter {

    private int last;

    synchronized int next() {
        last = last >= 0xFFFF ? 1 : last + 1;
        return last;
    }
}
