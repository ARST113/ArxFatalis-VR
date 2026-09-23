package com.picovr.openxr;

import android.app.NativeActivity;
import android.content.res.AssetManager;
import android.media.MediaScannerConnection;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import com.arxvr.android.ArxActivity;
import java.io.File;

public final class MainActivity extends NativeActivity {
    private static final String TAG = "ArxVR";

    static {
        System.loadLibrary("openxr_loader");
        System.loadLibrary("openxr_demos");
        System.loadLibrary("arx");
    }

    public native void setNativeAssetManager(AssetManager assetManager);
    public native long probeArxData(String gameDirectory);
    public native void configureArxEnginePaths(String gameDirectory, String userDirectory);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setNativeAssetManager(getAssets());
        File gameDirectory = ArxActivity.prepareGameData(this);
        File userDirectory = new File(gameDirectory.getParentFile(), "user");
        userDirectory.mkdirs();
        configureArxEnginePaths(gameDirectory.getAbsolutePath(), userDirectory.getAbsolutePath());
        long totalBytes = probeArxData(gameDirectory.getAbsolutePath());
        if(totalBytes >= 0) {
            Log.i(TAG, "Arx engine bridge ready; validated game data bytes=" + totalBytes);
        } else {
            Log.e(TAG, "Arx engine bridge could not validate all required PAK files in " + gameDirectory);
        }
    }

    public void scanFile(String path) {
        MediaScannerConnection.scanFile(this, new String[] { path }, null,
                (String scannedPath, Uri uri) ->
                        Log.i(TAG, "Finished scanning " + scannedPath + " as " + uri));
    }
}
