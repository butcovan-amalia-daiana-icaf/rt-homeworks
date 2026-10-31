package ssatr.service;

import java.util.Map;
import java.util.TreeMap;

/**
 * Ultima stare cunoscută a fiecărui device: ultima temperatură, momentul
 * ultimului mesaj, numărul de alarme etc.
 *
 * Este actualizată de thread-urile MessageProcessor și citită de CommandConsole,
 * deci accesul trebuie sincronizat.
 */
public class Dashboard {

    private static class State {
        double temp;
        long lastMs;
        int alarms;
    }

    private final Map<String, State> devices = new TreeMap<>();

    public synchronized void onTelemetry(String id, double temp) {
        State s = devices.computeIfAbsent(id, k -> new State());
        s.temp = temp;
        s.lastMs = System.currentTimeMillis();
    }

    public synchronized void onAlarm(String id, double temp) {
        State s = devices.computeIfAbsent(id, k -> new State());
        s.temp = temp;
        s.alarms++;
        s.lastMs = System.currentTimeMillis();
    }

    public synchronized void onStatus(String id) {
        State s = devices.computeIfAbsent(id, k -> new State());
        s.lastMs = System.currentTimeMillis();
    }

    public synchronized void print() {
        System.out.printf("%-14s %-8s %-16s %s%n", "DEVICE", "TEMP", "ULTIMUL MESAJ", "ALARME");
        long now = System.currentTimeMillis();
        for (Map.Entry<String, State> e : devices.entrySet()) {
            State s = e.getValue();
            System.out.printf("%-14s %-8.2f acum %-11s %d%n",
                    e.getKey(), s.temp, ((now - s.lastMs) / 1000) + " s", s.alarms);
        }
    }
}
