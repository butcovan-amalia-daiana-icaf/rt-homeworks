package ssatr.device;
import java.util.Locale;
import java.util.concurrent.BlockingQueue;

/**
 * TASK SPORADIC: tratează alarmele de temperatură.
 *
 * Evenimentele sosesc în coada alarmEvents (puse de taskul periodic).
 * Intervalul minim între sosiri este MIN_INTERARRIVAL_MS: între două eliberări
 * ale taskului (două alarme publicate) trebuie să treacă cel puțin atât.
 * Intervalul este impus de cod: evenimentele care sosesc mai des sunt
 * respinse, ca să nu fie inundat brokerul.
 */
public class SporadicAlarmTask implements Runnable {

    static final long MIN_INTERARRIVAL_MS = 5000;

    private final String deviceId;
    private final MqttConnection mqtt;
    private final BlockingQueue<Double> alarmEvents;
    private final PeriodicTelemetryTask periodic;   // de aici se citește pragul curent

    public SporadicAlarmTask(String deviceId, MqttConnection mqtt, BlockingQueue<Double> alarmEvents,
                             PeriodicTelemetryTask periodic) {
        this.deviceId = deviceId;
        this.mqtt = mqtt;
        this.alarmEvents = alarmEvents;
        this.periodic = periodic;
    }

    @Override
    public void run() {
        Long lastRelease = null;   // momentul ultimei alarme publicate (timp monoton)

        try {
            while (true) {
                double temperature = alarmEvents.take();       // blocat până sosește un eveniment
                long arrival = System.nanoTime();

                if (lastRelease != null) {
                    long sinceLastMs = (arrival - lastRelease) / 1_000_000;
                    if (sinceLastMs < MIN_INTERARRIVAL_MS) {
                        System.out.printf(Locale.US,
                                "[sporadic] alarmă RESPINSĂ (T=%.2f): au trecut doar %d ms (minim %d ms)%n",
                                temperature, sinceLastMs, MIN_INTERARRIVAL_MS);
                        continue;
                    }
                }
                lastRelease = arrival;

                double threshold = periodic.getThreshold();
                String message = deviceId + ";" + System.currentTimeMillis() + ";"
                        + String.format(Locale.US, "%.2f", temperature) + ";"
                        + String.format(Locale.US, "%.2f", threshold);
                mqtt.publish(Config.alarmTopic(deviceId), message);
                System.out.println("[sporadic] alarmă publicată: " + message);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }
}
