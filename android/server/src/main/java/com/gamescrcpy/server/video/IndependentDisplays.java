package com.gamescrcpy.server.video;

import com.gamescrcpy.server.wrappers.PlatformException;

public interface IndependentDisplays {

    int create(int width, int height, int densityDpi) throws PlatformException;

    void resize(int displayId, int width, int height, int densityDpi);

    void release(int displayId);
}
