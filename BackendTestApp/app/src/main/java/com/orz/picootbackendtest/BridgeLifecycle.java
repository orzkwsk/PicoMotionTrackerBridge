package com.orz.picootbackendtest;

// App orchestration only. Neither the backend nor transport owns saved user intent.
final class BridgeLifecycle {
    enum State { STOPPED, RUNNING, ERROR }

    interface Settings {
        String savedIpv4();
        boolean enabled();
        void saveStarted(String ipv4);
        void saveDisabled();
    }

    interface Publisher {
        boolean isNumericIpv4(String ipv4);
        StartResult start(String ipv4, int port);
        void stop();
    }

    private final Settings settings;
    private final Publisher publisher;
    private final int port;
    private State state = State.STOPPED;
    private String endpoint;
    private String message = "UDP bridge stopped";

    BridgeLifecycle(Settings settings, Publisher publisher, int port) {
        this.settings = settings;
        this.publisher = publisher;
        this.port = port;
        endpoint = settings.savedIpv4();
    }

    State state() { return state; }
    String message() { return message; }
    String endpoint() { return endpoint.isEmpty() ? "(not configured)" : endpoint + ":" + port; }

    void restore() {
        if (!settings.enabled()) {
            return;
        }
        final String saved = settings.savedIpv4();
        if (!publisher.isNumericIpv4(saved)) {
            publisher.stop();
            settings.saveDisabled();
            endpoint = saved;
            state = State.ERROR;
            message = "Saved PC IPv4 is invalid; automatic restore disabled";
            return;
        }
        // One attempt only. A transient failure preserves the existing saved intent.
        start(saved);
    }

    void start(String ipv4) {
        endpoint = ipv4;
        final StartResult result = publisher.start(ipv4, port);
        state = result.success ? State.RUNNING : State.ERROR;
        message = result.message;
        if (result.success) {
            settings.saveStarted(ipv4);
        }
    }

    void stopExplicitly() {
        shutdown();
        settings.saveDisabled();
    }

    void shutdown() {
        publisher.stop();
        state = State.STOPPED;
        message = "UDP bridge stopped";
        // Activity destruction is not an explicit change to the user's intent.
    }
}
