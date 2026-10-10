package ssatr.service;

import java.util.Map;
import java.util.TreeMap;

public class Dashboard {

    private static class DeviceState {
        double lastTemperature = Double.NaN;
        long lastSeenMs;
        int alarmCount;
    }

    private final Map<String, DeviceState> devices = new TreeMap<>();

    public synchronized void onTelemetry(String deviceId, double temperature) {
        DeviceState state = stateOf(deviceId);
        state.lastTemperature = temperature;
        state.lastSeenMs = System.currentTimeMillis();
    }

    public synchronized void onAlarm(String deviceId, double temperature) {
        DeviceState state = stateOf(deviceId);
        state.lastTemperature = temperature;
        state.alarmCount++;
        state.lastSeenMs = System.currentTimeMillis();
    }

    public synchronized void onStatus(String deviceId) {
        stateOf(deviceId).lastSeenMs = System.currentTimeMillis();
    }

    private DeviceState stateOf(String deviceId) {
        return devices.computeIfAbsent(deviceId, id -> new DeviceState());
    }

    public synchronized void print() {
        if (devices.isEmpty()) {
            System.out.println("(niciun device nu a trimis mesaje încă)");
            return;
        }
        long now = System.currentTimeMillis();
        System.out.printf("%-12s %8s   %-14s %s%n", "DEVICE", "TEMP", "ULTIMUL MESAJ", "ALARME");
        for (Map.Entry<String, DeviceState> e : devices.entrySet()) {
            DeviceState s = e.getValue();
            String temp = Double.isNaN(s.lastTemperature) ? "-"
                    : String.format(java.util.Locale.US, "%.2f", s.lastTemperature);
            System.out.printf("%-12s %8s   %-14s %d%n", e.getKey(), temp, age(now - s.lastSeenMs), s.alarmCount);
        }
    }

    private static String age(long ms) {
        long seconds = Math.max(0, ms) / 1000;
        return seconds < 60 ? "acum " + seconds + " s" : "acum " + (seconds / 60) + " min";
    }
}