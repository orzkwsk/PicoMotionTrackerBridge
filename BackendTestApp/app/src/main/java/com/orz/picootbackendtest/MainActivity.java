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
import android.widget.TextView;

import java.net.Inet4Address;
import java.net.InetAddress;

public final class MainActivity extends Activity {
    private static final String TAG = "PicoOTTest";
    private static final int BRIDGE_PORT = 29765;
    private static final String PREFS_NAME = "bridge_settings";
    private static final String PREF_PC_IPV4 = "pc_ipv4";

    static {
        Log.i(TAG, "JAVA static: loading libpico_ot_backend_test.so");
        System.loadLibrary("pico_ot_backend_test");
        Log.i(TAG, "JAVA static: native library loaded");
    }

    private TextView statusView;
    private EditText pcIpView;
    private SharedPreferences preferences;

    private static native String nativeStart();
    private static native void nativeStop();
    private static native String nativeReinitialize();
    private static native String nativeStartBridge(String pcIpv4, int port);
    private static native void nativeStopBridge();

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

        preferences = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);

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

        pcIpView = new EditText(this);
        pcIpView.setSingleLine(true);
        pcIpView.setHint("PC IPv4 (example: 192.168.1.10)");
        pcIpView.setText(preferences.getString(PREF_PC_IPV4, ""));
        root.addView(pcIpView);

        Button useWifiSubnet = new Button(this);
        useWifiSubnet.setText("Use Wi-Fi subnet (/24)");
        useWifiSubnet.setOnClickListener(v -> applyActiveWifiSubnet());
        root.addView(useWifiSubnet);

        Button startBridge = new Button(this);
        startBridge.setText("Start UDP Bridge -> PC:" + BRIDGE_PORT);
        startBridge.setOnClickListener(v -> {
            final String ip = pcIpView.getText().toString().trim();
            if (!isValidIpv4(ip)) {
                statusView.setText("Enter a valid PC IPv4 first");
                return;
            }
            savePcIp(ip);
            Log.i(TAG, "JAVA bridge: starting to " + ip + ":" + BRIDGE_PORT);
            final String result = nativeStartBridge(ip, BRIDGE_PORT);
            Log.i(TAG, "JAVA bridge: " + result);
            statusView.setText(result);
        });
        root.addView(startBridge);

        Button stopBridge = new Button(this);
        stopBridge.setText("Stop UDP Bridge");
        stopBridge.setOnClickListener(v -> {
            nativeStopBridge();
            statusView.setText("UDP bridge stopped");
        });
        root.addView(stopBridge);

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
    protected void onPause() {
        super.onPause();
        if (pcIpView != null) {
            savePcIp(pcIpView.getText().toString().trim());
        }
    }

    @Override
    protected void onDestroy() {
        Log.i(TAG, "JAVA onDestroy: begin");
        nativeStopBridge();
        nativeStop();
        Log.i(TAG, "JAVA onDestroy: native stop complete");
        super.onDestroy();
    }

    private void savePcIp(String value) {
        if (preferences == null) {
            return;
        }
        preferences.edit().putString(PREF_PC_IPV4, value).apply();
    }

    private void applyActiveWifiSubnet() {
        final String prefix = activeWifiIpv4Prefix();
        if (prefix == null) {
            statusView.setText("Active Wi-Fi IPv4 was not found");
            return;
        }

        String hostOctet = "";
        final String current = pcIpView.getText().toString().trim();
        final String[] currentParts = current.split("\\.", -1);
        if (currentParts.length == 4 && isValidOctet(currentParts[3])) {
            hostOctet = currentParts[3];
        }

        final String updated = prefix + hostOctet;
        pcIpView.setText(updated);
        pcIpView.setSelection(updated.length());
        savePcIp(updated);

        if (hostOctet.isEmpty()) {
            statusView.setText("Wi-Fi subnet inserted: " + prefix + "x  Enter the PC host octet.");
        } else {
            statusView.setText("Wi-Fi subnet applied: " + updated + "  (/24 assumption)");
        }
    }

    private String activeWifiIpv4Prefix() {
        final ConnectivityManager connectivityManager =
            (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
        if (connectivityManager == null) {
            return null;
        }

        final Network network = connectivityManager.getActiveNetwork();
        if (network == null) {
            return null;
        }

        final NetworkCapabilities capabilities = connectivityManager.getNetworkCapabilities(network);
        if (capabilities == null || !capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
            return null;
        }

        final LinkProperties properties = connectivityManager.getLinkProperties(network);
        if (properties == null) {
            return null;
        }

        for (LinkAddress linkAddress : properties.getLinkAddresses()) {
            final InetAddress address = linkAddress.getAddress();
            if (!(address instanceof Inet4Address) || address.isLoopbackAddress()) {
                continue;
            }

            final byte[] bytes = address.getAddress();
            return (bytes[0] & 0xff) + "." +
                   (bytes[1] & 0xff) + "." +
                   (bytes[2] & 0xff) + ".";
        }
        return null;
    }

    private static boolean isValidIpv4(String value) {
        final String[] parts = value.split("\\.", -1);
        if (parts.length != 4) {
            return false;
        }
        for (String part : parts) {
            if (!isValidOctet(part)) {
                return false;
            }
        }
        return true;
    }

    private static boolean isValidOctet(String value) {
        if (value == null || value.isEmpty() || value.length() > 3) {
            return false;
        }
        try {
            final int parsed = Integer.parseInt(value);
            return parsed >= 0 && parsed <= 255;
        } catch (NumberFormatException ignored) {
            return false;
        }
    }
}
