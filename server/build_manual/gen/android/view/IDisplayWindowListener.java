/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: C:\\Users\\10971\\AppData\\Local\\Android\\Sdk\\build-tools\\36.1.0\\aidl.exe -oD:\\QtScrcpy2\\server\\build_manual\\gen -ID:\\QtScrcpy2\\server\\src\\main\\aidl -pC:\\Users\\10971\\AppData\\Local\\Android\\Sdk\\platforms\\android-36\\framework.aidl D:\\QtScrcpy2\\server\\src\\main\\aidl\\android\\view\\IDisplayWindowListener.aidl
 *
 * DO NOT CHECK THIS FILE INTO A CODE TREE (e.g. git, etc..).
 * ALWAYS GENERATE THIS FILE FROM UPDATED AIDL COMPILER
 * AS A BUILD INTERMEDIATE ONLY. THIS IS NOT SOURCE CODE.
 */
package android.view;
/**
 * Interface to listen for changes to display window-containers.
 * 
 * This differs from DisplayManager's DisplayListener in a couple ways:
 *  - onDisplayAdded is always called after the display is actually added to the WM hierarchy.
 *    This corresponds to the DisplayContent and not the raw Dislay from DisplayManager.
 *  - onDisplayConfigurationChanged is called for all configuration changes, not just changes
 *    to displayinfo (eg. windowing-mode).
 */
public interface IDisplayWindowListener extends android.os.IInterface
{
  /** Default implementation for IDisplayWindowListener. */
  public static class Default implements android.view.IDisplayWindowListener
  {
    /**
     * Called when a new display is added to the WM hierarchy. The existing display ids are returned
     * when this listener is registered with WM via {@link #registerDisplayWindowListener}.
     */
    @Override public void onDisplayAdded(int displayId) throws android.os.RemoteException
    {
    }
    /** Called when a display's window-container configuration has changed. */
    @Override public void onDisplayConfigurationChanged(int displayId, android.content.res.Configuration newConfig) throws android.os.RemoteException
    {
    }
    /** Called when a display is removed from the hierarchy. */
    @Override public void onDisplayRemoved(int displayId) throws android.os.RemoteException
    {
    }
    @Override
    public android.os.IBinder asBinder() {
      return null;
    }
  }
  /** Local-side IPC implementation stub class. */
  public static abstract class Stub extends android.os.Binder implements android.view.IDisplayWindowListener
  {
    /** Construct the stub and attach it to the interface. */
    @SuppressWarnings("this-escape")
    public Stub()
    {
      this.attachInterface(this, DESCRIPTOR);
    }
    /**
     * Cast an IBinder object into an android.view.IDisplayWindowListener interface,
     * generating a proxy if needed.
     */
    public static android.view.IDisplayWindowListener asInterface(android.os.IBinder obj)
    {
      if ((obj==null)) {
        return null;
      }
      android.os.IInterface iin = obj.queryLocalInterface(DESCRIPTOR);
      if (((iin!=null)&&(iin instanceof android.view.IDisplayWindowListener))) {
        return ((android.view.IDisplayWindowListener)iin);
      }
      return new android.view.IDisplayWindowListener.Stub.Proxy(obj);
    }
    @Override public android.os.IBinder asBinder()
    {
      return this;
    }
    @Override public boolean onTransact(int code, android.os.Parcel data, android.os.Parcel reply, int flags) throws android.os.RemoteException
    {
      java.lang.String descriptor = DESCRIPTOR;
      if (code >= android.os.IBinder.FIRST_CALL_TRANSACTION && code <= android.os.IBinder.LAST_CALL_TRANSACTION) {
        data.enforceInterface(descriptor);
      }
      if (code == INTERFACE_TRANSACTION) {
        reply.writeString(descriptor);
        return true;
      }
      switch (code)
      {
        case TRANSACTION_onDisplayAdded:
        {
          int _arg0;
          _arg0 = data.readInt();
          this.onDisplayAdded(_arg0);
          break;
        }
        case TRANSACTION_onDisplayConfigurationChanged:
        {
          int _arg0;
          _arg0 = data.readInt();
          android.content.res.Configuration _arg1;
          _arg1 = _Parcel.readTypedObject(data, android.content.res.Configuration.CREATOR);
          this.onDisplayConfigurationChanged(_arg0, _arg1);
          break;
        }
        case TRANSACTION_onDisplayRemoved:
        {
          int _arg0;
          _arg0 = data.readInt();
          this.onDisplayRemoved(_arg0);
          break;
        }
        default:
        {
          return super.onTransact(code, data, reply, flags);
        }
      }
      return true;
    }
    private static class Proxy implements android.view.IDisplayWindowListener
    {
      private android.os.IBinder mRemote;
      Proxy(android.os.IBinder remote)
      {
        mRemote = remote;
      }
      @Override public android.os.IBinder asBinder()
      {
        return mRemote;
      }
      public java.lang.String getInterfaceDescriptor()
      {
        return DESCRIPTOR;
      }
      /**
       * Called when a new display is added to the WM hierarchy. The existing display ids are returned
       * when this listener is registered with WM via {@link #registerDisplayWindowListener}.
       */
      @Override public void onDisplayAdded(int displayId) throws android.os.RemoteException
      {
        android.os.Parcel _data = android.os.Parcel.obtain();
        try {
          _data.writeInterfaceToken(DESCRIPTOR);
          _data.writeInt(displayId);
          boolean _status = mRemote.transact(Stub.TRANSACTION_onDisplayAdded, _data, null, android.os.IBinder.FLAG_ONEWAY);
        }
        finally {
          _data.recycle();
        }
      }
      /** Called when a display's window-container configuration has changed. */
      @Override public void onDisplayConfigurationChanged(int displayId, android.content.res.Configuration newConfig) throws android.os.RemoteException
      {
        android.os.Parcel _data = android.os.Parcel.obtain();
        try {
          _data.writeInterfaceToken(DESCRIPTOR);
          _data.writeInt(displayId);
          _Parcel.writeTypedObject(_data, newConfig, 0);
          boolean _status = mRemote.transact(Stub.TRANSACTION_onDisplayConfigurationChanged, _data, null, android.os.IBinder.FLAG_ONEWAY);
        }
        finally {
          _data.recycle();
        }
      }
      /** Called when a display is removed from the hierarchy. */
      @Override public void onDisplayRemoved(int displayId) throws android.os.RemoteException
      {
        android.os.Parcel _data = android.os.Parcel.obtain();
        try {
          _data.writeInterfaceToken(DESCRIPTOR);
          _data.writeInt(displayId);
          boolean _status = mRemote.transact(Stub.TRANSACTION_onDisplayRemoved, _data, null, android.os.IBinder.FLAG_ONEWAY);
        }
        finally {
          _data.recycle();
        }
      }
    }
    static final int TRANSACTION_onDisplayAdded = (android.os.IBinder.FIRST_CALL_TRANSACTION + 0);
    static final int TRANSACTION_onDisplayConfigurationChanged = (android.os.IBinder.FIRST_CALL_TRANSACTION + 1);
    static final int TRANSACTION_onDisplayRemoved = (android.os.IBinder.FIRST_CALL_TRANSACTION + 2);
  }
  /** @hide */
  public static final java.lang.String DESCRIPTOR = "android.view.IDisplayWindowListener";
  /**
   * Called when a new display is added to the WM hierarchy. The existing display ids are returned
   * when this listener is registered with WM via {@link #registerDisplayWindowListener}.
   */
  public void onDisplayAdded(int displayId) throws android.os.RemoteException;
  /** Called when a display's window-container configuration has changed. */
  public void onDisplayConfigurationChanged(int displayId, android.content.res.Configuration newConfig) throws android.os.RemoteException;
  /** Called when a display is removed from the hierarchy. */
  public void onDisplayRemoved(int displayId) throws android.os.RemoteException;
  /** @hide */
  static class _Parcel {
    static private <T> T readTypedObject(
        android.os.Parcel parcel,
        android.os.Parcelable.Creator<T> c) {
      if (parcel.readInt() != 0) {
          return c.createFromParcel(parcel);
      } else {
          return null;
      }
    }
    static private <T extends android.os.Parcelable> void writeTypedObject(
        android.os.Parcel parcel, T value, int parcelableFlags) {
      if (value != null) {
        parcel.writeInt(1);
        value.writeToParcel(parcel, parcelableFlags);
      } else {
        parcel.writeInt(0);
      }
    }
  }
}
