package com.orz.picootbackendtest;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

public final class MainActivity extends Activity {
    private static final String TAG = "PicoOTTest";

    static {
        Log.i(TAG, "JAVA static: loading libpico_ot_backend_test.so");
        System.loadLibrary("pico_ot_backend_test");
        Log.i(TAG, "JAVA static: native library loaded");
    }

    private TextView statusView;

    private static native String nativeStart();
    private static native void nativeStop();
    private static native String nativeReinitialize();

    private String startBackend() {
        Log.i(TAG, "JAVA startBackend: entering nativeStart");
        final String result = nativeStart();
        Log.i(TAG, "JAVA startBackend: nativeStart returned: " + result.replace('\n', ' '));
        return result;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Log.i(TAG, "JAVA onCreate: begin");
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(32, 24, 32, 24);

        TextView title = new TextView(this);
        title.setText("PICO OT Backend Test\nLogcat tag: PicoOTTest");
        title.setTextSize(22f);
        root.addView(title);

        statusView = new TextView(this);
        statusView.setTextSize(16f);
        root.addView(statusView);

        Button start = new Button(this);
        start.setText("Start / Restart Backend");
        start.setOnClickListener(v -> statusView.setText(startBackend()));
        root.addView(start);

        Button reinit = new Button(this);
        reinit.setText("Reinitialize Tracker Map");
        reinit.setOnClickListener(v -> {
            Log.i(TAG, "JAVA reinitialize: entering nativeReinitialize");
            final String result = nativeReinitialize();
            Log.i(TAG, "JAVA reinitialize: returned: " + result);
            statusView.setText(result);
        });
        root.addView(reinit);

        Button stop = new Button(this);
        stop.setText("Stop Backend");
        stop.setOnClickListener(v -> {
            Log.i(TAG, "JAVA stop: entering nativeStop");
            nativeStop();
            Log.i(TAG, "JAVA stop: complete");
            statusView.setText("Backend stopped");
        });
        root.addView(stop);

        setContentView(root);
        statusView.setText(startBackend());
        Log.i(TAG, "JAVA onCreate: complete");
    }

    @Override
    protected void onDestroy() {
        Log.i(TAG, "JAVA onDestroy: begin");
        nativeStop();
        Log.i(TAG, "JAVA onDestroy: nativeStop complete");
        super.onDestroy();
    }
}
