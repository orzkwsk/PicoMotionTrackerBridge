package com.orz.picootbackendtest;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

// Plain JVM regression tests: no Android runtime, native library, or new dependency.
public final class BridgeLifecycleTest {
    private static final String A = "192.168.1.10";
    private static final String B = "10.0.0.2";
    private static final int PORT = 29765;

    private static final class Settings implements BridgeLifecycle.Settings {
        String ip = "";
        boolean enabled;
        int writes;
        final List<String> events = new ArrayList<>();
        public String savedIpv4() { return ip; }
        public boolean enabled() { return enabled; }
        public void saveStarted(String value) {
            events.add("save:" + value);
            ip = value;
            enabled = true;
            ++writes;
        }
        public void saveDisabled() {
            events.add("disable");
            enabled = false;
            ++writes;
        }
    }

    private static final class Publisher implements BridgeLifecycle.Publisher {
        final Settings settings;
        boolean running;
        boolean failSocket;
        int starts;
        int stops;
        Publisher(Settings settings) { this.settings = settings; }
        public boolean isNumericIpv4(String ip) {
            // Validation itself is covered by the shared native transport tests.
            return Arrays.asList(A, B, "127.0.0.1", "255.255.255.255").contains(ip);
        }
        public StartResult start(String ip, int port) {
            check(port == PORT, "port");
            ++starts;
            settings.events.add("start:" + ip);
            running = isNumericIpv4(ip) && !failSocket;
            // Deliberately include success text in a failure to catch text matching.
            return new StartResult(running, running ? "started" : "failed; UDP bridge started is not true");
        }
        public void stop() { ++stops; running = false; }
    }

    private static final class Fixture {
        final Settings settings = new Settings();
        final Publisher publisher = new Publisher(settings);
        BridgeLifecycle create() {
            BridgeLifecycle lifecycle = new BridgeLifecycle(settings, publisher, PORT);
            lifecycle.shutdown(); // Same initialization as MainActivity.onCreate.
            return lifecycle;
        }
    }

    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static void freshStart() {
        Fixture f = new Fixture();
        BridgeLifecycle lifecycle = f.create();
        lifecycle.restore();
        check(lifecycle.state() == BridgeLifecycle.State.STOPPED, "fresh STOPPED");
        check(f.publisher.starts == 0 && !f.publisher.running, "fresh no start");
        check(f.settings.writes == 0 && !f.settings.enabled, "fresh no writes");
    }

    private static void firstManualStart() {
        Fixture f = new Fixture();
        BridgeLifecycle lifecycle = f.create();
        lifecycle.start(A);
        check(lifecycle.state() == BridgeLifecycle.State.RUNNING && f.publisher.running, "manual RUNNING");
        check(f.settings.ip.equals(A) && f.settings.enabled, "manual saved intent");
        check(lifecycle.endpoint().equals(A + ":" + PORT), "manual endpoint");
        check(f.settings.events.equals(Arrays.asList("start:" + A, "save:" + A)), "save after success only");
    }

    private static void recreation() {
        Fixture f = new Fixture();
        BridgeLifecycle old = f.create();
        old.start(A);
        old.shutdown();
        check(!f.publisher.running && f.settings.enabled, "destroy stops but preserves intent");
        BridgeLifecycle recreated = f.create();
        recreated.restore();
        check(f.publisher.starts == 2 && f.publisher.running, "recreate auto start");
        check(recreated.state() == BridgeLifecycle.State.RUNNING, "recreate RUNNING");
        check(recreated.endpoint().equals(A + ":" + PORT), "recreate endpoint");
    }

    private static void explicitStop() {
        Fixture f = new Fixture();
        BridgeLifecycle lifecycle = f.create();
        lifecycle.start(A);
        lifecycle.stopExplicitly();
        lifecycle.stopExplicitly();
        lifecycle.shutdown();
        BridgeLifecycle recreated = f.create();
        recreated.restore();
        check(!f.settings.enabled && f.settings.ip.equals(A), "explicit stop preserves IP, disables intent");
        check(f.publisher.starts == 1 && !f.publisher.running, "no auto start after stop");
        check(recreated.state() == BridgeLifecycle.State.STOPPED, "stopped after recreation");
    }

    private static void invalidManualStart() {
        for (String invalid : Arrays.asList("", "999.999.999.999", "localhost", "::1")) {
            Fixture f = new Fixture();
            BridgeLifecycle lifecycle = f.create();
            lifecycle.start(invalid);
            check(lifecycle.state() == BridgeLifecycle.State.ERROR && !f.publisher.running, "invalid ERROR");
            check(f.settings.writes == 0 && !f.settings.enabled && f.settings.ip.isEmpty(), "invalid not saved");
        }
    }

    private static void endpointChangeAndRepeatedStart() {
        Fixture f = new Fixture();
        BridgeLifecycle lifecycle = f.create();
        lifecycle.start(A);
        lifecycle.start(B);
        lifecycle.start(B);
        check(f.settings.ip.equals(B) && f.settings.enabled, "endpoint change saved");
        check(lifecycle.endpoint().equals(B + ":" + PORT), "new endpoint displayed");
        check(f.publisher.starts == 3 && f.publisher.running, "repeated start");
    }

    private static void failedEndpointChange() {
        Fixture f = new Fixture();
        BridgeLifecycle lifecycle = f.create();
        lifecycle.start(A);
        lifecycle.start("localhost");
        check(lifecycle.state() == BridgeLifecycle.State.ERROR && !f.publisher.running, "failed restart stops sender");
        check(f.settings.ip.equals(A) && f.settings.enabled && f.settings.writes == 1, "retain last successful config");
        lifecycle.shutdown();
        f.create().restore();
        check(f.publisher.running && f.settings.ip.equals(A), "restore last successful config");
    }

    private static void invalidSavedSetting() {
        Fixture f = new Fixture();
        f.settings.ip = "999.1.1.1";
        f.settings.enabled = true;
        BridgeLifecycle lifecycle = f.create();
        lifecycle.restore();
        check(lifecycle.state() == BridgeLifecycle.State.ERROR, "invalid restore ERROR");
        check(f.publisher.starts == 0 && !f.publisher.running, "invalid restore no start");
        check(!f.settings.enabled && f.settings.writes == 1, "invalid saved intent disabled");
    }

    private static void transientRestoreFailure() {
        Fixture f = new Fixture();
        f.settings.ip = A;
        f.settings.enabled = true;
        f.publisher.failSocket = true;
        BridgeLifecycle lifecycle = f.create();
        lifecycle.restore();
        check(lifecycle.state() == BridgeLifecycle.State.ERROR && !f.publisher.running, "socket restore ERROR");
        check(f.settings.ip.equals(A) && f.settings.enabled && f.settings.writes == 0, "socket failure preserves intent");
        check(f.publisher.starts == 1, "one attempt, no retries");
        lifecycle.shutdown();
        f.publisher.failSocket = false;
        BridgeLifecycle recreated = f.create();
        recreated.restore();
        check(f.publisher.starts == 2 && f.publisher.running, "next lifecycle can restore");
    }

    private static void transientManualFailure() {
        Fixture f = new Fixture();
        f.publisher.failSocket = true;
        BridgeLifecycle lifecycle = f.create();
        lifecycle.start(A);
        check(lifecycle.state() == BridgeLifecycle.State.ERROR && !f.settings.enabled, "first socket failure stays disabled");
        check(f.settings.writes == 0, "socket failure not saved");
        f.publisher.failSocket = false;
        lifecycle.start(A);
        f.publisher.failSocket = true;
        lifecycle.start(B);
        check(f.settings.ip.equals(A) && f.settings.enabled && f.settings.writes == 1, "failed B preserves successful A");
    }

    public static void main(String[] args) {
        freshStart();
        firstManualStart();
        recreation();
        explicitStop();
        invalidManualStart();
        endpointChangeAndRepeatedStart();
        failedEndpointChange();
        invalidSavedSetting();
        transientRestoreFailure();
        transientManualFailure();
        explicitPublisherRestart();
        System.out.println("BridgeLifecycle: 11 tests PASS (original 10 saved-intent cases plus explicit restart)");
    }

    private static void explicitPublisherRestart() {
        Fixture f = new Fixture();
        BridgeLifecycle lifecycle = f.create();
        lifecycle.start(A);
        int stopsBefore = f.publisher.stops;
        lifecycle.restart(A);
        check(f.publisher.starts == 2 && f.publisher.stops == stopsBefore + 1 && f.publisher.running,
            "explicit restart stops and starts once");
        check(f.settings.ip.equals(A) && f.settings.enabled, "restart saves successful endpoint");
        f.publisher.failSocket = true;
        lifecycle.restart(B);
        check(lifecycle.state() == BridgeLifecycle.State.ERROR && !f.publisher.running && f.settings.ip.equals(A),
            "failed explicit restart preserves prior saved intent");
    }
}
