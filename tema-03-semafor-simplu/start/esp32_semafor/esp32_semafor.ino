// Semafor pentru pietoni, cu buton, pe ESP32-C3 Super Mini.
//
// Placa este controlerul semaforului; lămpile sunt desenate de dashboard, care
// le aprinde după mesajele "lights" publicate de placă. Patru activități:
//
//   ISR buton BOOT ----+
//                      +--(coada butonului)--> task sporadic
//   command_received --+   (BUTTON)                 |
//                                                   v
//                                         [ starea semaforului ] <-- mutex
//                                            ^          |
//                                            |          v
//                         task periodic de control   task periodic de ieșire
//                         (fazele, T = 100 ms)       (lămpile, T = 500 ms) --> lights
//
// Gata implementate (nu trebuie modificate):
//   config.h         setările: Wi-Fi, broker, STUDENT, pinii, perioadele, duratele fazelor
//   connection.h/cpp Wi-Fi, ora exactă (SNTP), clientul MQTT, mqtt_publish(), epoch_ms()
//
// Așa cum este, schița se conectează la Wi-Fi și la broker și afișează pe
// Serial comenzile primite. Restul îl scrieți voi: TODO 0 ... TODO 3.
//
// Placa: ESP32C3 Dev Module, USB CDC On Boot: Enabled. Serial Monitor: 115200 baud.

#include "config.h"
#include "connection.h"

// Fazele semaforului (vezi tabelul și diagrama din README.md). Numele din
// PHASE_NAMES sunt cele din mesajul "lights", în aceeași ordine ca în enum.
enum Phase { CAR_GREEN, CAR_YELLOW, ALL_RED, PED_GREEN };
const char* const PHASE_NAMES[] = {"CAR_GREEN", "CAR_YELLOW", "ALL_RED", "PED_GREEN"};

// Culorile unei lămpi, cu numele din mesajul "lights".
enum Light { RED, YELLOW, GREEN };
const char* const LIGHT_NAMES[] = {"RED", "YELLOW", "GREEN"};

struct SemaforState {
    Phase phase;
    uint32_t phaseEnteredMs;   // millis() la intrarea în fază
    bool request;              // lampa AȘTEPTAȚI
};
SemaforState state;
SemaphoreHandle_t stateMutex;

struct ButtonPress { TickType_t tick; };
QueueHandle_t buttonQueue;

void control_task(void* arg) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        bool changed = false;
        Phase current;

        xSemaphoreTake(stateMutex, portMAX_DELAY);
        uint32_t now = millis();
        uint32_t elapsed = now - state.phaseEnteredMs;
        Phase next = state.phase;
        switch (state.phase) {
            case CAR_GREEN:
                if (elapsed >= CAR_GREEN_MIN_MS && state.request) next = CAR_YELLOW;
                break;
            case CAR_YELLOW:
                if (elapsed >= CAR_YELLOW_MS) next = ALL_RED;
                break;
            case ALL_RED:
                if (elapsed >= ALL_RED_MS) { next = PED_GREEN; state.request = false; }
                break;
            case PED_GREEN:
                if (elapsed >= PED_GREEN_MS) next = CAR_GREEN;
                break;
        }
        if (next != state.phase) { state.phase = next; state.phaseEnteredMs = now; changed = true; }
        current = state.phase;
        xSemaphoreGive(stateMutex);

        if (changed) Serial.printf("[control] faza nouă: %s\n", PHASE_NAMES[current]);
        xTaskDelayUntil(&last, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}
void output_task(void* arg) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        SemaforState s = state;
        uint32_t now = millis();
        xSemaphoreGive(stateMutex);

        Light cars = RED, peds = RED;
        switch (s.phase) {
            case CAR_GREEN:  cars = GREEN;  peds = RED;   break;
            case CAR_YELLOW: cars = YELLOW; peds = RED;   break;
            case ALL_RED:    cars = RED;    peds = RED;   break;
            case PED_GREEN:  cars = RED;    peds = GREEN; break;
        }

        uint32_t seconds = 0;
        if (s.phase == PED_GREEN) {
            uint32_t elapsed = now - s.phaseEnteredMs;
            uint32_t remaining = (elapsed >= PED_GREEN_MS) ? 0 : (PED_GREEN_MS - elapsed);
            seconds = (remaining + 999) / 1000;   // rotunjit în sus
        }

        char msg[128];
        snprintf(msg, sizeof(msg), "%s;%lld;%s;%s;%s;%d;%lu",
                 DEVICE_ID, (long long)epoch_ms(), PHASE_NAMES[s.phase],
                 LIGHT_NAMES[cars], LIGHT_NAMES[peds], s.request ? 1 : 0,
                 (unsigned long)seconds);
        mqtt_publish("lights", msg);
        digitalWrite(LED_PIN, s.request ? LOW : HIGH);   // LED-ul e aprins pe LOW

        xTaskDelayUntil(&last, pdMS_TO_TICKS(OUTPUT_PERIOD_MS));
    }
}
void button_task(void* arg) {
    bool anyAccepted = false;
    TickType_t lastAccepted = 0;
    for (;;) {
        ButtonPress p;
        xQueueReceive(buttonQueue, &p, portMAX_DELAY);

        if (anyAccepted && (TickType_t)(p.tick - lastAccepted) < pdMS_TO_TICKS(MIN_INTERARRIVAL_MS)) {
            Serial.println("[buton] apăsare respinsă (prea devreme)");
            continue;
        }
        anyAccepted = true;
        lastAccepted = p.tick;

        xSemaphoreTake(stateMutex, portMAX_DELAY);
        state.request = true;
        xSemaphoreGive(stateMutex);
        Serial.println("[buton] apăsare acceptată: cerere înregistrată");
    }
}

void IRAM_ATTR button_isr(void) {
    ButtonPress p;
    p.tick = xTaskGetTickCountFromISR();
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(buttonQueue, &p, &woken);
    portYIELD_FROM_ISR(woken);
}

// Apelată de clientul MQTT pentru fiecare comandă primită (vezi connection.h).
void command_received(const char* text) {
    Serial.printf("[mqtt] comandă primită: %s\n", text);
    if (strcmp(text, "BUTTON") == 0) {
        ButtonPress p;
        p.tick = xTaskGetTickCount();
        xQueueSend(buttonQueue, &p, 0);
    }
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
    Serial.printf("=== tema 03: STUDENT %s, DEVICE_ID %s ===\n", STUDENT, DEVICE_ID);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);  // LED stins
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    wifi_connect();
    time_sync();

     stateMutex = xSemaphoreCreateMutex();
    buttonQueue = xQueueCreate(16, sizeof(ButtonPress));
    state.phase = CAR_GREEN;
    state.phaseEnteredMs = millis();
    state.request = false;

    mqtt_start();

   xTaskCreate(control_task, "control", 4096, NULL, 12, NULL);
    xTaskCreate(output_task,  "output",  4096, NULL, 10, NULL);
    xTaskCreate(button_task,  "button",  4096, NULL, 14, NULL);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), button_isr, FALLING);
}

void loop() {
    // Totul se face în taskuri: loop() nu are nimic de făcut.
    delay(1000);
}
