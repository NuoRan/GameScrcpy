/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: C:\\Users\\10971\\AppData\\Local\\Android\\Sdk\\build-tools\\36.1.0\\aidl.exe -oD:\\QtScrcpy2\\server\\build_manual\\gen -ID:\\QtScrcpy2\\server\\src\\main\\aidl D:\\QtScrcpy2\\server\\src\\main\\aidl\\android\\content\\IOnPrimaryClipChangedListener.aidl
 *
 * DO NOT CHECK THIS FILE INTO A CODE TREE (e.g. git, etc..).
 * ALWAYS GENERATE THIS FILE FROM UPDATED AIDL COMPILER
 * AS A BUILD INTERMEDIATE ONLY. THIS IS NOT SOURCE CODE.
 */
package android.content;
/** {@hide} */
public interface IOnPrimaryClipChangedListener extends android.os.IInterface
{
  /** Default implementation for IOnPrimaryClipChangedListener. */
  public static class Default implements android.content.IOnPrimaryClipChangedListener
  {
    @Override public void dispatchPrimaryClipChanged() throws android.os.RemoteException
    {
    }
    @Override
    public android.os.IBinder asBinder() {
      return null;
    }
  }
  /** Local-side IPC implementation stub class. */
  public static abstract class Stub extends android.os.Binder implements android.content.IOnPrimaryClipChangedListener
  {
    /** Construct the stub and attach it to the interface. */
    @SuppressWarnings("this-escape")
    public Stub()
    {
      this.attachInterface(this, DESCRIPTOR);
    }
    /**
     * Cast an IBinder object into an android.content.IOnPrimaryClipChangedListener interface,
     * generating a proxy if needed.
     */
    public static android.content.IOnPrimaryClipChangedListener asInterface(android.os.IBinder obj)
    {
      if ((obj==null)) {
        return null;
      }
      android.os.IInterface iin = obj.queryLocalInterface(DESCRIPTOR);
      if (((iin!=null)&&(iin instanceof android.content.IOnPrimaryClipChangedListener))) {
        return ((android.content.IOnPrimaryClipChangedListener)iin);
      }
      return new android.content.IOnPrimaryClipChangedListener.Stub.Proxy(obj);
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
        case TRANSACTION_dispatchPrimaryClipChanged:
        {
          this.dispatchPrimaryClipChanged();
          break;
        }
        default:
        {
          return super.onTransact(code, data, reply, flags);
        }
      }
      return true;
    }
    private static class Proxy implements android.content.IOnPrimaryClipChangedListener
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
      @Override public void dispatchPrimaryClipChanged() throws android.os.RemoteException
      {
        android.os.Parcel _data = android.os.Parcel.obtain();
        try {
          _data.writeInterfaceToken(DESCRIPTOR);
          boolean _status = mRemote.transact(Stub.TRANSACTION_dispatchPrimaryClipChanged, _data, null, android.os.IBinder.FLAG_ONEWAY);
        }
        finally {
          _data.recycle();
        }
      }
    }
    /** @hide */
    public static final java.lang.String DESCRIPTOR = "android.content.IOnPrimaryClipChangedListener";
    static final int TRANSACTION_dispatchPrimaryClipChanged = (android.os.IBinder.FIRST_CALL_TRANSACTION + 0);
  }
  public void dispatchPrimaryClipChanged() throws android.os.RemoteException;
}
