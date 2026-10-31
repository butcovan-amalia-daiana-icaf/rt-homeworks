package ssatr.service;

import java.util.concurrent.BlockingQueue;

/**
 * Preia mesajele de un anumit tip ("telemetry", "alarm" sau "status") din coada
 * inbox, le salvează și actualizează dashboard-ul.
 *
 * Formatul mesajelor (câmpuri separate prin ";"):
 *   telemetry:  deviceId;timestamp;temperatura
 *   alarm:      deviceId;timestamp;temperatura;prag
 *   status:     deviceId;timestamp;text
 */
public class MessageProcessor implements Runnable {

    private final String kind;
    private final BlockingQueue<String> inbox;
    private final CsvStorage storage;
    private final Dashboard dashboard;

    public MessageProcessor(String kind, BlockingQueue<String> inbox, CsvStorage storage, Dashboard dashboard) {
        this.kind = kind;
        this.inbox = inbox;
        this.storage = storage;
        this.dashboard = dashboard;
    }

    @Override
    public void run() {
        while (true) {
            try {
                String msg = inbox.take();
                storage.append(kind, msg);
                String[] p = msg.split(";");
                switch (kind) {
                    case "telemetry":
                        dashboard.onTelemetry(p[0], Double.parseDouble(p[2]));
                        break;
                    case "alarm":
                        dashboard.onAlarm(p[0], Double.parseDouble(p[2]));
                        System.out.println("[ALARMA] " + msg);
                        break;
                    case "status":
                        dashboard.onStatus(p[0]);
                        System.out.println("[STATUS] " + msg);
                        break;
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return;
            } catch (Exception e) {
                System.out.println("Mesaj invalid (" + kind + "): " + e.getMessage());
            }
        }
    }
}
