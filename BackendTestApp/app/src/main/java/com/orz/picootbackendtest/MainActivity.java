package com.orz.picootbackendtest;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

public final class MainActivity extends Activity {
    static {
        System.loadLibrary("pico_ot_backend_test");
    }

    private TextView statusView;

    private static native String nativeStart();
    private static native void nativeStop();
    private static native String nativeReinitialize();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
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
        start.setOnClickListener(v -> statusView.setText(nativeStart()));
        root.addView(start);

        Button reinit = new Button(this);
        reinit.setText("Reinitialize Tracker Map");
        reinit.setOnClickListener(v -> statusView.setText(nativeReinitialize()));
        root.addView(reinit);

        Button stop = new Button(this);
        stop.setText("Stop Backend");
        stop.setOnClickListener(v -> {
            nativeStop();
            statusView.setText("Backend stopped");
        });
        root.addView(stop);

        setContentView(root);
        statusView.setText(nativeStart());
    }

    @Override
    protected void onDestroy() {
        nativeStop();
        super.onDestroy();
    }
}
