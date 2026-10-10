package ssatr.device;
import java.util.Locale;
import java.util.concurrent.BlockingQueue;

/**
 * TASK APERIODIC: execută comenzile primite de la business service.
 *
 * Comenzile sosesc oricând, pe MQTT, fără un interval minim între ele, și sunt
 * puse în coada commands. Fiecare comandă eliberează un job al taskului.
 * Comenzi care trebuie suportate:
 *
 *   SET_INTERVAL <ms>       schimbă perioada T a taskului periodic
 *   SET_THRESHOLD <valoare> schimbă pragul de alarmă
 *   RESET                   readuce senzorul la valoarea inițială
 *   STATUS                  raportează perioada și pragul curente
 *
 * După executarea fiecărei comenzi, device-ul publică pe Config.statusTopic(deviceId)
 * o confirmare: "deviceId;timestamp;text", de exemplu
 * "device-1;1727600000000;OK SET_INTERVAL 1000".
 */
public class AperiodicCommandTask implements Runnable {

    private final String deviceId;
    private final TemperatureSensor sensor;
    private final MqttConnection mqtt;
    private final BlockingQueue<String> commands;
    private final PeriodicTelemetryTask periodic;

    public AperiodicCommandTask(String deviceId, TemperatureSensor sensor, MqttConnection mqtt,
                                BlockingQueue<String> commands, PeriodicTelemetryTask periodic) {
        this.deviceId = deviceId;
        this.sensor = sensor;
        this.mqtt = mqtt;
        this.commands = commands;
        this.periodic = periodic;
    }

    @Override
    public void run() {
        try {
            while (true) {
                String command = commands.take();
                String reply;
                try {
                    reply = execute(command.trim());
                } catch (RuntimeException e) {
                    reply = "ERROR " + e.getMessage();
                }
                publishStatus(reply);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    private String execute(String command) {
        if (command.isEmpty()) {
            return "ERROR comanda goala";
        }
        String[] parts = command.split("\\s+");
        String name = parts[0].toUpperCase(Locale.ROOT);

        switch (name) {
            case "SET_INTERVAL": {
                if (parts.length != 2) {
                    return "ERROR SET_INTERVAL necesita un parametru: SET_INTERVAL <ms>";
                }
                try {
                    long ms = Long.parseLong(parts[1]);
                    if (ms < 100) {
                        return "ERROR interval invalid (minim 100 ms): " + parts[1];
                    }
                    periodic.setPeriodMs(ms);
                    return "OK SET_INTERVAL " + ms;
                } catch (NumberFormatException e) {
                    return "ERROR parametru invalid pentru SET_INTERVAL: " + parts[1];
                }
            }
            case "SET_THRESHOLD": {
                if (parts.length != 2) {
                    return "ERROR SET_THRESHOLD necesita un parametru: SET_THRESHOLD <valoare>";
                }
                try {
                    double value = Double.parseDouble(parts[1]);
                    if (Double.isNaN(value) || Double.isInfinite(value)) {
                        return "ERROR prag invalid: " + parts[1];
                    }
                    periodic.setThreshold(value);
                    return "OK SET_THRESHOLD " + parts[1];
                } catch (NumberFormatException e) {
                    return "ERROR parametru invalid pentru SET_THRESHOLD: " + parts[1];
                }
            }
            case "RESET":
                sensor.reset();
                return "OK RESET";
            case "STATUS":
                return String.format(Locale.US, "OK STATUS period=%d threshold=%.2f",
                        periodic.getPeriodMs(), periodic.getThreshold());
            default:
                return "ERROR comanda necunoscuta: " + name;
        }
    }

    private void publishStatus(String text) {
        String safe = text.replace(';', ',');
        String message = deviceId + ";" + System.currentTimeMillis() + ";" + safe;
        mqtt.publish(Config.statusTopic(deviceId), message);
        System.out.println("[aperiodic] " + message);
    }
}
