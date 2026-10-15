package com.orz.picootbackendtest;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;

// On-device smoke without external test dependencies or the PICO shell's UI tree.
// Run only against this test app; it leaves UDP enabled toward the supplied PC IP.
public final class BridgeSmokeInstrumentation extends Instrumentation {
    private String pcIpv4;
    private Activity activity;
    private SharedPreferences preferences;

    @Override
    public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        pcIpv4 = arguments == null ? null : arguments.getString("pc_ipv4");
        start();
    }

    @Override
    public void onStart() {
        Bundle result = new Bundle();
        try {
            check(pcIpv4 != null && !pcIpv4.isEmpty(), "Supply -e pc_ipv4 <PC numeric IPv4>");
            preferences = getTargetContext().getSharedPreferences("bridge_settings", 0);
            // Isolate the two settings owned by this smoke; preserve unrelated keys.
            check(preferences.edit().remove("bridge_pc_ipv4").remove("bridge_enabled").commit(), "fresh preferences");
            activity = launch();
            expectState("STOPPED");
            check(savedIp().isEmpty() && !enabled(), "fresh saved settings");
            check(input().equals(preferences.getString("pc_ipv4", "")), "legacy IP input retained");
            click("Use Wi-Fi subnet (/24)");
            expectState("STOPPED");
            check(savedIp().isEmpty() && !enabled(), "subnet helper does not save START intent");
            report("A fresh start");

            startBridge(pcIpv4);
            expectState("RUNNING");
            check(savedIp().equals(pcIpv4) && enabled(), "successful manual START saved");
            check(status().contains("UDP endpoint: " + pcIpv4 + ":29765"), "endpoint displayed");
            report("B manual START and saved intent");

            final String countBeforeReinitialize = trackerCountLine();
            click("Reinitialize Tracker Map");
            expectState("RUNNING");
            check(trackerCountLine().equals(countBeforeReinitialize), "Reinitialize retains registry count display");
            report("tracker count remains visible after Reinitialize");

            recreate();
            expectState("RUNNING");
            check(input().equals(pcIpv4), "saved input restored");
            report("C Activity recreation auto restore");

            click("Stop UDP Bridge");
            expectState("STOPPED");
            check(!enabled() && savedIp().equals(pcIpv4), "STOP disabled but retained IP");
            recreate();
            expectState("STOPPED");
            report("D explicit STOP survives recreation");

            for (String invalid : new String[]{"999.999.999.999", "localhost"}) {
                startBridge(invalid);
                expectState("ERROR");
                check(!enabled() && savedIp().equals(pcIpv4), "invalid START not saved");
            }
            report("E invalid IPv4 fails immediately");

            startBridge("127.0.0.1");
            expectState("RUNNING");
            check(savedIp().equals("127.0.0.1"), "changed endpoint saved after START");
            startBridge(pcIpv4);
            expectState("RUNNING");
            startBridge(pcIpv4);
            expectState("RUNNING");
            check(enabled() && savedIp().equals(pcIpv4), "repeated START saved PC endpoint");
            report("F endpoint change and G repeated START");

            startBridge("localhost");
            expectState("ERROR");
            check(enabled() && savedIp().equals(pcIpv4), "failed restart preserves last success");
            recreate();
            expectState("RUNNING");
            report("last successful endpoint restored after failed restart");

            click("Stop Backend");
            check(status().contains("Backend: STOPPED") && status().contains("UDP: RUNNING"), "independent STOP states");
            check(trackerCountLine().equals("Trackers: 0"), "stopped backend has no registered trackers");
            click("Start / Restart Backend");
            expectState("RUNNING");
            report("independent backend/publisher states");

            result.putString("stream", "\nPASS: lifecycle smoke; UDP enabled toward " + pcIpv4 + ":29765\n");
            finish(Activity.RESULT_OK, result);
        } catch (Throwable failure) {
            result.putString("stream", "\nFAIL: " + Log.getStackTraceString(failure) + "\n");
            finish(Activity.RESULT_CANCELED, result);
        }
    }

    private Activity launch() {
        Intent intent = new Intent(getTargetContext(), MainActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        Activity started = startActivitySync(intent);
        waitForIdleSync();
        return started;
    }

    private void recreate() {
        ActivityMonitor monitor = addMonitor(MainActivity.class.getName(), null, false);
        try {
            runOnMainSync(() -> activity.recreate());
            activity = waitForMonitorWithTimeout(monitor, 20000);
            check(activity != null, "Activity recreation completed");
            waitForIdleSync();
        } finally {
            removeMonitor(monitor);
        }
    }

    private View find(View view, Class<?> type, String prefix) {
        if (type.isInstance(view) && (prefix == null ||
            (view instanceof TextView && ((TextView) view).getText().toString().startsWith(prefix)))) {
            return view;
        }
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); ++i) {
                View found = find(group.getChildAt(i), type, prefix);
                if (found != null) return found;
            }
        }
        return null;
    }

    private View view(Class<?> type, String prefix) {
        View found = find(activity.getWindow().getDecorView(), type, prefix);
        check(found != null, "UI element: " + type.getSimpleName() + " " + prefix);
        return found;
    }

    private void click(String prefix) {
        runOnMainSync(() -> ((Button) view(Button.class, prefix)).performClick());
        waitForIdleSync();
    }

    private void startBridge(String ip) {
        runOnMainSync(() -> ((EditText) view(EditText.class, null)).setText(ip));
        click("Start UDP Bridge");
    }

    private String status() {
        final String[] text = new String[1];
        runOnMainSync(() -> text[0] = ((TextView) view(TextView.class, "Backend:")).getText().toString());
        return text[0];
    }

    private String input() {
        final String[] text = new String[1];
        runOnMainSync(() -> text[0] = ((EditText) view(EditText.class, null)).getText().toString());
        return text[0];
    }

    private void expectState(String udp) {
        String text = status();
        check(text.contains("Backend: RUNNING") && text.contains("UDP: " + udp), text);
        trackerCountLine();
    }

    private String trackerCountLine() {
        String count = null;
        for (String line : status().split("\n")) {
            if (line.startsWith("Trackers: ")) {
                check(count == null, "single tracker count line");
                check(line.matches("Trackers: [0-9]+"), "numeric registry count");
                count = line;
            }
        }
        check(count != null, "tracker count is always visible");
        return count;
    }

    private String savedIp() { return preferences.getString("bridge_pc_ipv4", ""); }
    private boolean enabled() { return preferences.getBoolean("bridge_enabled", false); }

    private void report(String name) {
        Bundle progress = new Bundle();
        progress.putString("stream", "PASS: " + name + "\n");
        sendStatus(0, progress);
    }

    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
