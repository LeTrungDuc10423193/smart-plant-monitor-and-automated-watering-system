#include <WiFi.h>
#include <ThingSpeak.h>
#include <DHT.h>

//================ WIFI =================
const char* ssid = "VGU_Student_Guest";
const char* password = "";

//================ THINGSPEAK =================
unsigned long channelID = 3411541;
const char* writeAPIKey = "O76TK7L3SKHVJDV6";
WiFiClient client;

//================ DHT11 =================
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

//================ SOIL =================
#define SOIL_PIN 1

//================ RELAY =================
#define RELAY_PIN 18

// Calibration
#define SOIL_DRY 4095
#define SOIL_WET 1200

//================ WATERING HISTORY =================
unsigned long lastTSWrite = 0;            // last ThingSpeak write time (ms)
const unsigned long TS_MIN_GAP = 16000;   // ThingSpeak min 15s -> use 16s to be safe
int wateringCount = 0;                    // total watering events since power on

// Write all fields to ThingSpeak, respecting the 15s minimum interval.
// pumpStatus: 1 = pump ON (watering), 0 = pump OFF (idle)
// duration  : watering duration in seconds (0 for periodic / non-watering writes)
void uploadToThingSpeak(float temperature, float humidity, int soilADC,
                        int soilPercent, int pumpStatus, int duration)
{
    // Respect ThingSpeak's minimum 15s interval between writes
    while (millis() - lastTSWrite < TS_MIN_GAP)
    {
        delay(200);
    }

    ThingSpeak.setField(1, temperature);
    ThingSpeak.setField(2, humidity);
    ThingSpeak.setField(3, soilADC);
    ThingSpeak.setField(4, soilPercent);
    ThingSpeak.setField(5, pumpStatus);   // 1 = watering, 0 = idle  <-- watering history
    ThingSpeak.setField(6, duration);     // watering duration in seconds

    int status = ThingSpeak.writeFields(channelID, writeAPIKey);
    lastTSWrite = millis();

    if (status == 200)
    {
        Serial.println("ThingSpeak Upload Success");
    }
    else
    {
        Serial.print("ThingSpeak Error: ");
        Serial.println(status);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2000);
    dht.begin();
    pinMode(RELAY_PIN, OUTPUT);
    // Relay OFF
    digitalWrite(RELAY_PIN, HIGH);

    Serial.println("Connecting WiFi...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.println("WiFi Connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    ThingSpeak.begin(client);
}

void loop()
{
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    int soilADC = analogRead(SOIL_PIN);

    int soilPercent =
        map(soilADC, SOIL_DRY, SOIL_WET, 0, 100);
    soilPercent = constrain(soilPercent, 0, 100);

    String risk = "Normal";

    //================ RISK ANALYSIS =================
    if (temperature >= 20 && temperature <= 30 &&
        humidity > 80 && soilADC < 1500)
    {
        risk = "Mold Risk";
    }
    if (temperature >= 22 && temperature <= 30 &&
        humidity > 85 && soilADC < 1200)
    {
        risk = "Root Rot Risk";
    }
    if (temperature >= 25 && temperature <= 32 &&
        humidity >= 70 && humidity <= 90 && soilADC < 1300)
    {
        risk = "Pest Risk";
    }

    //================ AUTO WATERING =================
    if (soilADC > 3500)
    {
        Serial.println("SOIL DRY -> WATERING");

        unsigned long startTime = millis();
        wateringCount++;

        // Pump ON
        digitalWrite(RELAY_PIN, LOW);

        // ---- Log watering START event (pump = 1) ----
        uploadToThingSpeak(temperature, humidity, soilADC, soilPercent, 1, 0);

        // Water until target moisture reached
        while (analogRead(SOIL_PIN) > 2500)
        {
            Serial.print("Current Soil ADC: ");
            Serial.println(analogRead(SOIL_PIN));
            delay(5000);
        }

        // Pump OFF
        digitalWrite(RELAY_PIN, HIGH);
        Serial.println("TARGET REACHED");

        // Watering duration in seconds
        int duration = (millis() - startTime) / 1000;

        // Refresh soil reading after watering
        int soilADCend = analogRead(SOIL_PIN);
        int soilPercentEnd =
            constrain(map(soilADCend, SOIL_DRY, SOIL_WET, 0, 100), 0, 100);

        Serial.print("Watering #");
        Serial.print(wateringCount);
        Serial.print(" finished. Duration: ");
        Serial.print(duration);
        Serial.println(" s");

        // ---- Log watering END event (pump = 0, with duration) ----
        uploadToThingSpeak(temperature, humidity, soilADCend, soilPercentEnd, 0, duration);

        // Update local vars so the periodic upload below reflects post-watering state
        soilADC = soilADCend;
        soilPercent = soilPercentEnd;
    }

    //================ SERIAL =================
    Serial.println();
    Serial.println("==============================");
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.println(" C");
    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");
    Serial.print("Soil ADC: ");
    Serial.println(soilADC);
    Serial.print("Soil Moisture: ");
    Serial.print(soilPercent);
    Serial.println(" %");
    Serial.print("Risk Level: ");
    Serial.println(risk);
    Serial.print("Watering Count: ");
    Serial.println(wateringCount);

    //================ THINGSPEAK (periodic, pump = 0) =================
    uploadToThingSpeak(temperature, humidity, soilADC, soilPercent, 0, 0);

    // Sampling interval (currently 20s for testing).
    delay(20000);
}
