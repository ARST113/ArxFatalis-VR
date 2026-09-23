package com.arxvr.android;

import android.content.res.AssetManager;
import android.content.Context;
import android.os.Bundle;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import org.libsdl.app.SDLActivity;

public final class ArxActivity extends SDLActivity {
    private static final String TAG = "ArxVR";

    private File gameDirectory;
    private File userDirectory;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        gameDirectory = prepareGameData(this);
        File files = gameDirectory.getParentFile();
        userDirectory = new File(files, "user");
        userDirectory.mkdirs();
        super.onCreate(savedInstanceState);
    }

    public static File prepareGameData(Context context) {
        File gameDirectory = new File(context.getExternalFilesDir(null), "game");
        gameDirectory.mkdirs();
        new File(gameDirectory, "localisation").mkdirs();
        new File(gameDirectory, "misc").mkdirs();
        try {
            installAssetTree(context.getAssets(), "game_files", gameDirectory);
        } catch(IOException exception) {
            Log.e(TAG, "Could not install bundled Arx runtime files", exception);
        }
        return gameDirectory;
    }

    private static void installAssetTree(AssetManager assets, String assetPath, File destination)
            throws IOException {
        String[] children = assets.list(assetPath);
        if(children != null && children.length != 0) {
            if(!destination.isDirectory() && !destination.mkdirs()) {
                throw new IOException("Could not create " + destination);
            }
            for(String child : children) {
                installAssetTree(assets, assetPath + "/" + child, new File(destination, child));
            }
            return;
        }

        if(destination.isFile() && destination.length() != 0) {
            return;
        }
        File parent = destination.getParentFile();
        if(parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("Could not create " + parent);
        }
        try(InputStream input = assets.open(assetPath);
                FileOutputStream output = new FileOutputStream(destination)) {
            byte[] buffer = new byte[64 * 1024];
            int count;
            while((count = input.read(buffer)) != -1) {
                output.write(buffer, 0, count);
            }
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "arx" };
    }

    @Override
    protected String[] getArguments() {
        return new String[] {
            "--data-dir", gameDirectory.getAbsolutePath(),
            "--user-dir", userDirectory.getAbsolutePath(),
            "--config-dir", userDirectory.getAbsolutePath()
        };
    }
}
