// Device-ul IoT din tema 01, de data aceasta pe un ESP32-C3 Super Mini.
//
// Aceeași logică și același protocol ca device-simulator din tema 01, cu trei
// taskuri FreeRTOS în loc de trei thread-uri Java:
//
//   task periodic --(coada de alarme)--> task sporadic
//        |                                     |
//        +---- telemetry ---> MQTT <--- alarm -+
//                              |
//                          commands --> command_received() --(coada de comenzi)--> task aperiodic ---> status
//
// Gata implementate (nu trebuie modificate):
//   config.h         setările: Wi-Fi, broker, STUDENT, valorile inițiale; DEVICE_ID
//                    se citește din adresa MAC a plăcii
//   connection.h/cpp Wi-Fi, ora exactă (SNTP), clientul MQTT, mqtt_publish(), epoch_ms()
//
// Așa cum este, schița se conectează la Wi-Fi și la broker și afișează pe
// Serial comenzile primite. Taskurile le scrieți voi: TODO 0 ... TODO 3.
// Exemplele de pornire sunt în laboratorul 0 din rt-labs-esp32c3; vezi
// exemple/README.md din folderul temei.
//
// Placa: ESP32C3 Dev Module, USB CDC On Boot: Enabled. Serial Monitor: 115200 baud.

#include "config.h"
#include "connection.h"

// Temperatura cipului, în °C, de la senzorul intern al ESP32-C3.
// Este temperatura siliciului, nu a camerei: imediat după pornirea Wi-Fi-ului
// poate fi în jur de 50 °C, apoi scade în câteva minute spre 35-40 °C.
// Rezoluția este de aproximativ 1 °C. Ca să provocați alarme, puneți pragul
// cu SET_THRESHOLD cu un grad sub valoarea citită.
float read_temperature(void) {
    return temperatureRead();
}

// ---- stare partajată între taskuri ----
volatile uint32_t period_ms = PERIOD_MS_INITIAL;
volatile float threshold = THRESHOLD_INITIAL;

struct AlarmEvent {
    float temperature;
    float threshold;
};

struct Command {
    char text[COMMAND_MAX_LEN];
};

QueueHandle_t alarmQueue;
QueueHandle_t commandQueue;

// ---- TODO 1: task periodic ----
void periodic_task(void* arg) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        // jobul
        float t = read_temperature();
        float th = threshold;

        char msg[96];
        snprintf(msg, sizeof(msg), "%s;%lld;%.2f", DEVICE_ID, (long long)epoch_ms(), t);
        mqtt_publish("telemetry", msg);
        Serial.printf("[periodic] telemetrie: %s\n", msg);

        if (t > th) {
            AlarmEvent ev = { t, th };
            xQueueSend(alarmQueue, &ev, 0);   // timeout 0: nu blochează
        }

        // așteptare ABSOLUTĂ: următoarea eliberare = last + T
        xTaskDelayUntil(&last, pdMS_TO_TICKS(period_ms));
    }
}

// ---- TODO 2: task sporadic ----
void sporadic_task(void* arg) {
    bool anyPublished = false;
    uint32_t lastPublished = 0;   // millis() al ultimei alarme publicate
    for (;;) {
        AlarmEvent ev;
        xQueueReceive(alarmQueue, &ev, portMAX_DELAY);

        uint32_t now = millis();
        if (anyPublished && (now - lastPublished) < MIN_INTERARRIVAL_MS) {
            Serial.println("[sporadic] alarmă respinsă (prea devreme)");
            continue;
        }

        char msg[128];
        snprintf(msg, sizeof(msg), "%s;%lld;%.2f;%.2f",
                 DEVICE_ID, (long long)epoch_ms(), ev.temperature, ev.threshold);
        mqtt_publish("alarm", msg);
        Serial.printf("[sporadic] alarmă publicată: %s\n", msg);

        lastPublished = now;
        anyPublished = true;
    }
}

// ---- TODO 3: task aperiodic ----
void publish_status(const char* text) {
    char msg[160];
    snprintf(msg, sizeof(msg), "%s;%lld;%s", DEVICE_ID, (long long)epoch_ms(), text);
    mqtt_publish("status", msg);
    Serial.printf("[aperiodic] status: %s\n", msg);
}

void aperiodic_task(void* arg) {
    for (;;) {
        Command c;
        xQueueReceive(commandQueue, &c, portMAX_DELAY);

        char name[COMMAND_MAX_LEN] = "";
        char arg1[COMMAND_MAX_LEN] = "";
        int n = sscanf(c.text, "%63s %63s", name, arg1);
        char reply[128];

        if (n < 1) {
            publish_status("ERROR comanda goala");
        } else if (strcmp(name, "SET_INTERVAL") == 0) {
            char* end;
            long v = (n == 2) ? strtol(arg1, &end, 10) : 0;
            if (n < 2 || *end != '\0' || v <= 0) {
                publish_status("ERROR SET_INTERVAL necesita un numar pozitiv (ms)");
            } else {
                period_ms = (uint32_t)v;
                snprintf(reply, sizeof(reply), "OK SET_INTERVAL %ld", v);
                publish_status(reply);
            }
        } else if (strcmp(name, "SET_THRESHOLD") == 0) {
            char* end;
            float v = (n == 2) ? strtof(arg1, &end) : 0;
            if (n < 2 || *end != '\0') {
                publish_status("ERROR SET_THRESHOLD necesita un numar");
            } else {
                threshold = v;
                snprintf(reply, sizeof(reply), "OK SET_THRESHOLD %.2f", v);
                publish_status(reply);
            }
        } else if (strcmp(name, "RESET") == 0) {
            period_ms = PERIOD_MS_INITIAL;
            threshold = THRESHOLD_INITIAL;
            publish_status("OK RESET");
        } else if (strcmp(name, "STATUS") == 0) {
            snprintf(reply, sizeof(reply), "OK STATUS interval=%lu threshold=%.2f",
                     (unsigned long)period_ms, (float)threshold);
            publish_status(reply);
        } else {
            snprintf(reply, sizeof(reply), "ERROR comanda necunoscuta: %s", name);
            publish_status(reply);
        }
    }
}
// Apelată de clientul MQTT pentru fiecare comandă primită (vezi connection.h).
void command_received(const char* text) {
    Serial.printf("[mqtt] comandă primită: %s\n", text);
     Command c;
    strncpy(c.text, text, COMMAND_MAX_LEN - 1);
    c.text[COMMAND_MAX_LEN - 1] = '\0';
    xQueueSend(commandQueue, &c, 0); 
}

void setup() {
    Serial.begin(115200);
    // Fără așteptare la scriere: cu placa la USB, dar fără Serial Monitor deschis,
    // fiecare Serial.printf() ar aștepta până la 2 s să fie citit și ar întârzia
    // taskul care scrie. Așa, textul pe care nu-l citește nimeni se pierde.
    Serial.setTxTimeoutMs(0);
    uint32_t t = millis();
    while (!Serial && millis() - t < 3000) delay(10);  // așteptăm Serial Monitor, cel mult 3 s
    delay(200);
    Serial.println();
    Serial.printf("=== tema 02: STUDENT %s, DEVICE_ID %s ===\n", STUDENT, DEVICE_ID);

    wifi_connect();
    time_sync();

    alarmQueue = xQueueCreate(8, sizeof(AlarmEvent));
    commandQueue = xQueueCreate(8, sizeof(Command));

    mqtt_start();
    xTaskCreate(periodic_task,  "periodic",  4096, NULL, 10, NULL);
    xTaskCreate(sporadic_task,  "sporadic",  4096, NULL, 8,  NULL);
    xTaskCreate(aperiodic_task, "aperiodic", 4096, NULL, 6,  NULL);
}

void loop() {
    // Totul se face în taskuri: loop() nu are nimic de făcut.
    delay(1000);
}
