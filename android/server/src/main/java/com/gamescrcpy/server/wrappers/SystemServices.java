package com.gamescrcpy.server.wrappers;

import android.os.IBinder;
import android.os.IInterface;
import java.lang.reflect.Method;

public final class SystemServices {

    private SystemServices() {
    }

    public static IInterface get(String serviceName, String interfaceName) throws PlatformException {
        try {
            final Method getService = Class.forName("android.os.ServiceManager").getDeclaredMethod("getService",
                    String.class);
            final IBinder binder = (IBinder) getService.invoke(null, serviceName);
            if (binder == null) {
                throw new PlatformException("system service " + serviceName + " is not running");
            }
            final Method asInterface = Class.forName(interfaceName + "$Stub").getMethod("asInterface", IBinder.class);
            return (IInterface) asInterface.invoke(null, binder);
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new PlatformException("cannot reach system service " + serviceName, e);
        }
    }
}
