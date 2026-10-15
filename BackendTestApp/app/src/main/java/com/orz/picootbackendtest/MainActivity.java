package com.orz.picootbackendtest;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.Bundle;
import android.util.Log;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.net.Inet4Address;
import java.net.InetAddress;

public final class MainActivity extends Activity {
    private static final String TAG = "PicoOTTest";
    private static final int BRIDGE_PORT = 29765;
    private static final String PREFS_NAME = "bridge_settings";
    private static final String PREF_PC_IPV4 = "bridge_pc_ipv4";
    private static final String PREF_LEGACY_PC_IPV4 = "pc_ipv4";
    private static final String PREF_ENABLED = "bridge_enabled";

    static {
        Log.i(TAG, "JAVA static: loading libpico_ot_backend_test.so");
        System.loadLibrary("pico_ot_backend_test");
        Log.i(TAG, "JAVA static: native library loaded");
    }

    private TextView statusView;
    private EditText pcIpView;
    private TextView pcIpHintView;
    private BridgeLifecycle bridge;
    private BridgeLifecycle.State backendState = BridgeLifecycle.State.STOPPED;
    private String backendMessage = "Backend stopped";

    private static native StartResult nativeStart();
    private static native void nativeStop();
    private static native String nativeReinitialize();
    private static native StartResult nativeStartBridge(String pcIpv4, int port);
    private static native boolean nativeIsNumericIpv4(String pcIpv4);
    private static native void nativeStopBridge();

    private void startBackend() {
        Log.i(TAG, "JAVA startBackend: entering nativeStart");
        final StartResult result = nativeStart();
        backendState = result.success ? BridgeLifecycle.State.RUNNING : BridgeLifecycle.State.ERROR;
        backendMessage = result.message;
        Log.i(TAG, "JAVA startBackend: success=" + result.success + " " + result.message.replace('\n', ' '));
        renderStatus();
    }

    private void renderStatus() {
        statusView.setText("Backend: " + backendState + "\n" + backendMessage
            + "\n\nUDP: " + bridge.state() + "\nUDP endpoint: " + bridge.endpoint()
            + "\n" + bridge.message());
    }

    private void logBridgeResult(String operation) {
        final String text = "JAVA bridge " + operation + ": " + bridge.state()
            + " endpoint=" + bridge.endpoint() + " " + bridge.message().replace('\n', ' ');
        if (bridge.state() == BridgeLifecycle.State.ERROR) {
            Log.e(TAG, text);
        } else {
            Log.i(TAG, text);
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Log.i(TAG, "JAVA onCreate: begin");
        super.onCreate(savedInstanceState);

        final SharedPreferences preferences = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        bridge = new BridgeLifecycle(new BridgeLifecycle.Settings() {
            public String savedIpv4() { return preferences.getString(PREF_PC_IPV4, ""); }
            public boolean enabled() { return preferences.getBoolean(PREF_ENABLED, false); }
            public void saveStarted(String ipv4) {
                preferences.edit().putString(PREF_PC_IPV4, ipv4).putBoolean(PREF_ENABLED, true).apply();
            }
            public void saveDisabled() { preferences.edit().putBoolean(PREF_ENABLED, false).apply(); }
        }, new BridgeLifecycle.Publisher() {
            public boolean isNumericIpv4(String ipv4) { return nativeIsNumericIpv4(ipv4); }
            public StartResult start(String ipv4, int port) { return nativeStartBridge(ipv4, port); }
            public void stop() { nativeStopBridge(); }
        }, BRIDGE_PORT);
        // Clear any previous native publisher before starting this Activity's lifecycle.
        bridge.shutdown();

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(32, 24, 32, 24);

        TextView title = new TextView(this);
        title.setText("PICO OT Backend / Bridge Test\nLogcat tag: PicoOTTest");
        title.setTextSize(22f);
        root.addView(title);

        statusView = new TextView(this);
        statusView.setTextSize(16f);
        root.addView(statusView);

        Button start = new Button(this);
        start.setText("Start / Restart Backend");
        start.setOnClickListener(v -> startBackend());
        root.addView(start);

        Button reinit = new Button(this);
        reinit.setText("Reinitialize Tracker Map");
        reinit.setOnClickListener(v -> {
            Log.i(TAG, "JAVA reinitialize: entering nativeReinitialize");
            final String result = nativeReinitialize();
            Log.i(TAG, "JAVA reinitialize: returned: " + result);
            backendMessage = result;
            renderStatus();
        });
        root.addView(reinit);

        pcIpView = new EditText(this);
        pcIpView.setSingleLine(true);
        pcIpView.setHint("PC IPv4 (example: 192.168.1.10)");
        // Older APKs saved draft input under pc_ipv4. Reuse it as an input hint,
        // without treating it as a successful START or enabling automatic restore.
        pcIpView.setText(preferences.getString(PREF_PC_IPV4,
            preferences.getString(PREF_LEGACY_PC_IPV4, "")));
        root.addView(pcIpView);

        pcIpHintView = new TextView(this);
        root.addView(pcIpHintView);

        Button useWifiSubnet = new Button(this);
        useWifiSubnet.setText("Use Wi-Fi subnet (/24)");
        useWifiSubnet.setOnClickListener(v -> applyActiveWifiSubnet());
        root.addView(useWifiSubnet);

        Button startBridge = new Button(this);
        startBridge.setText("Start UDP Bridge -> PC:" + BRIDGE_PORT);
        startBridge.setOnClickListener(v -> {
            final String ip = pcIpView.getText().toString().trim();
            Log.i(TAG, "JAVA bridge manual start attempt endpoint=" + ip + ":" + BRIDGE_PORT);
            bridge.start(ip);
            logBridgeResult("manual start");
            renderStatus();
        });
        root.addView(startBridge);

        Button stopBridge = new Button(this);
        stopBridge.setText("Stop UDP Bridge");
        stopBridge.setOnClickListener(v -> {
            bridge.stopExplicitly();
            logBridgeResult("manual stop");
            renderStatus();
        });
        root.addView(stopBridge);

        Button stop = new Button(this);
        stop.setText("Stop Backend");
        stop.setOnClickListener(v -> {
            Log.i(TAG, "JAVA stop: entering nativeStop");
            nativeStop();
            Log.i(TAG, "JAVA stop: complete");
            backendState = BridgeLifecycle.State.STOPPED;
            backendMessage = "Backend stopped";
            renderStatus();
        });
        root.addView(stop);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(root);
        setContentView(scroll);
        // Backend first, then restore the independent publisher once and render both.
        startBackend();
        final boolean enabled = preferences.getBoolean(PREF_ENABLED, false);
        Log.i(TAG, "JAVA bridge restore intent enabled=" + enabled + " endpoint=" + bridge.endpoint());
        if (enabled) {
            Log.i(TAG, "JAVA bridge auto-start attempt");
            bridge.restore();
            logBridgeResult("auto-start");
        }
        renderStatus();
        Log.i(TAG, "JAVA onCreate: complete");
    }

    @Override
    protected void onDestroy() {
        Log.i(TAG, "JAVA onDestroy: begin");
        bridge.shutdown();
        nativeStop();
        Log.i(TAG, "JAVA onDestroy: native stop complete");
        super.onDestroy();
    }

    private void applyActiveWifiSubnet() {
        final String prefix = activeWifiIpv4Prefix();
        if (prefix == null) {
            pcIpHintView.setText("Active Wi-Fi IPv4 was not found");
            return;
        }

        String hostOctet = "";
        final String[] currentParts = pcIpView.getText().toString().trim().split("\\.", -1);
        if (currentParts.length == 4 && nativeIsNumericIpv4("0.0.0." + currentParts[3])) {
            hostOctet = currentParts[3];
        }
        final String updated = prefix + hostOctet;
        pcIpView.setText(updated);
        pcIpView.setSelection(updated.length());
        pcIpHintView.setText(hostOctet.isEmpty()
            ? "Wi-Fi subnet inserted: " + prefix + "x  Enter the PC host octet."
            : "Wi-Fi subnet applied: " + updated + "  (/24 assumption)");
        // This edits draft input only. Successful native START commits settings.
    }

    private String activeWifiIpv4Prefix() {
        final ConnectivityManager manager =
            (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
        if (manager == null) return null;
        final Network network = manager.getActiveNetwork();
        if (network == null) return null;
        final NetworkCapabilities capabilities = manager.getNetworkCapabilities(network);
        if (capabilities == null || !capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
            return null;
        }
        final LinkProperties properties = manager.getLinkProperties(network);
        if (properties == null) return null;
        for (LinkAddress linkAddress : properties.getLinkAddresses()) {
            final InetAddress address = linkAddress.getAddress();
            if (address instanceof Inet4Address && !address.isLoopbackAddress()) {
                final byte[] bytes = address.getAddress();
                return (bytes[0] & 0xff) + "." + (bytes[1] & 0xff) + "." + (bytes[2] & 0xff) + ".";
            }
        }
        return null;
    }
}
