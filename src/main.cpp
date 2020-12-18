#define BLYNK_PRINT Serial

// Libraries
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <BlynkSimpleEsp32_SSL.h>
#include <DHTesp.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// MACROS
#define DHTTYPE DHT22
#define DHTPIN 22
#define DS18B20 17
#define PUMP 21
#define SOILSENSOR 35
#define WATER_LEVEL_SENSOR 36
#define uS_TO_S_FACTOR 1000000ULL // Conversion factor for micro seconds to seconds
#define TIME_TO_SLEEP 60          // Multiplier: Time ESP32 will go to sleep (in seconds)
#define WAKE_UP_INTERVAL 30       // Multiplier: TIme ESP32 will go to sleep (in minutes)

// constant variables
const int AirConstant = 2635;   // 0% water
const int WaterConstant = 1190; // 100% water

// Auth Token for the Blynk App.
const char *blynkAuthenticationCode = "*-*-*-*--*--*-*-";

// Replace with your unique IFTTT URL resource
const char *resource = "/trigger/pump_trigger/with/key/*-*-*-*-*-*-*-*-";

// Maker Webhooks IFTTT
const char *server = "maker.ifttt.com";

// Your WiFi credentials.
const char *ssid = "******";
const char *pass = "**************";

// Creating objects
BlynkTimer timer;
DHTesp dht;
OneWire oneWire(DS18B20);            // setup a oneWire instance
DallasTemperature sensors(&oneWire); // pass oneWire to DallasTemperature library

// Function definitions
void SendMeasurementDataToBlynkk();
void MakeIFTTTRequest(int dataStart, int dataEnd);
int MoistureMeasurements(int samples);

void setup()
{
  // Debug console
  Serial.begin(9600);

  Blynk.begin(blynkAuthenticationCode, ssid, pass);
  sensors.begin();
  dht.setup(DHTPIN, DHTesp::DHT22);

  timer.setInterval(1000L, SendMeasurementDataToBlynkk); // 1 seconds interval

  esp_sleep_enable_timer_wakeup(WAKE_UP_INTERVAL * TIME_TO_SLEEP * uS_TO_S_FACTOR);
}

void loop()
{
  Blynk.run();
  timer.run(); // Initiates BlynkTimer
}

void SendMeasurementDataToBlynkk()
{

  delay(1000);

  // Soil temperature measurement
  sensors.requestTemperatures();                  // send the command to get temperatures
  float tempCelsius = sensors.getTempCByIndex(0); // read temperature in Celsius

  if (isnan(tempCelsius))
  {
    Blynk.notify("Failed to read from DS18B20 sensor!");
  }

  // Soil moisture measurement
  int soilMoisture = MoistureMeasurements(1000);

  // Control measurements to be in scale
  if (soilMoisture < 0)
  {
    soilMoisture = 0;
  }
  if (soilMoisture > 100)
  {
    soilMoisture = 100;
  }
  
  When soil is too dry...
  if (soilMoisture < 30)
  {
    int soilMeasureData = 0;
    /* If soil moisture is too dry, use waterpump to pump more water
    to plant. Measuring soilMoisture in each round*/
    do
    {
      digitalWrite(PUMP, HIGH); // Turn waterpump on
      soilMeasureData = MoistureMeasurements(100);
    } while (soilMeasureData < 35);

    digitalWrite(PUMP, LOW); // Turn waterpump off

    // Use funtion to write pumping information to Google Sheet
    MakeIFTTTRequest(soilMoisture, soilMeasureData);
  }

  // Measure air humidity and temperature
  float airHumidity = dht.getHumidity();
  float airTemperature = dht.getTemperature();

  if (isnan(airHumidity) || isnan(airTemperature))
  {
    Blynk.notify("Failed to read from DHT sensor!");
  }

  int adcValueFromWaterTank = 0;
  int voltage_value = 0;

  adcValueFromWaterTank = analogRead(WATER_LEVEL_SENSOR);
  voltage_value = (adcValueFromWaterTank * 3.3) / (4095);

  // Send information to Blynk environment
  Blynk.virtualWrite(V0, tempCelsius);
  Blynk.virtualWrite(V1, soilMoisture);
  Blynk.virtualWrite(V2, airHumidity);
  Blynk.virtualWrite(V3, airTemperature);

  if (voltage_value != 3)
  {
    Blynk.notify("Hey, the water level is too low!\nCheck the water level in the tank.");
  }

  // Debugging prints
  Serial.printf("DS18B20 Temp: %f\n", tempCelsius);
  Serial.printf("Soil moisture: %d\n", soilMoisture);
  Serial.printf("DHT22 Humidity: %f\n", airHumidity);
  Serial.printf("DHT22 Temp: %f\n", airTemperature);
  Serial.printf("Entering sleep mode...\n");

  delay(1000);
  esp_deep_sleep_start();
  delay(10);
}

/* Make an HTTP request to the IFTTT web service which adds 
new row to Google sheet and writes time and measurement information
to sheet everytime when pump has worked */

void MakeIFTTTRequest(int dataStart, int dataEnd)
{
  WiFiClient client;
  int retries = 5;
  while (!client.connect(server, 80) && (retries-- > 0)) { }

  if (!client.connected()) 
  {
    Blynk.virtualWrite(V10, "Failed to connect...");
  }
  else 
  {
    String jsonObject = String("{\"value1\":\"") + dataStart + String("\",\"value2\":\"") + dataEnd + "\"}";

    client.println(String("POST ") + resource + " HTTP/1.1");
    client.println(String("Host: ") + server);
    client.println("Connection: close\r\nContent-Type: application/json");
    client.print("Content-Length: ");
    client.println(jsonObject.length());
    client.println();
    client.println(jsonObject);

    int timeout = 5 * 10; // 5 seconds
    while (!!!client.available() && (timeout-- > 0))
    {
      delay(100);
    }
    if (!client.available())
    {
      Blynk.virtualWrite(V10, "No response...");
    }
    while (client.available())
    {
      Blynk.virtualWrite(V10, client.read());
      //Serial.write(client.read());
    }
  }
  Blynk.virtualWrite(V10, "closing connection");
  client.stop();
}

int MoistureMeasurements(int samples)
{
  // Measure n samples from sensor to decrease noise in moisture measurements
  int soilMoisture = 0;
  long sum = 0;

  for (int i = 0; i < samples; i++)
  {
    sum += analogRead(SOILSENSOR);
    delay(10);
  }

  // Calculate average value from measurements
  soilMoisture = sum / samples;
  // Remap average to 0 to 100 scale and after it control values to in desired scale.
  soilMoisture = map(soilMoisture, AirConstant, WaterConstant, 0, 100);
  return soilMoisture;
}
