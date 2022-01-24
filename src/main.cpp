#include <ESP8266WiFi.h>
#include "DHT.h"

// Stuff for debug environment (will enable or disable Serial usage
// depending on BUILD_TYPE set on the diferent platformio.ini envs)
#define SERIAL_PRINTER Serial
#define ENV_DEBUG 1
#ifndef BUILD_TYPE
  #define BUILD_TYPE 0
#endif
#if BUILD_TYPE==ENV_DEBUG
  #define DEBUG_PRINTF(...) \
    { SERIAL_PRINTER.printf(__VA_ARGS__); }
  #define DELAY_MULTIPLIER 10
#else
  #define DEBUG_PRINTF(...) {}
  #define DELAY_MULTIPLIER 1000
#endif

// The program takes X measures then smooths (averages) them into one and the submits it
#define START_DELAY_SECONDS 600
#define NUMBER_OF_MEASURES_BETWEEN_SUBMISIONS 5
#define DELAY_BETWEEN_SUBMISIONS_SECONDS 3600
#define DHTTYPE DHT11 // DHT11, DHT12, DHT21, DHT22, AM2301
#define THINGSPEAK_SERVER "api.thingspeak.com"
#define THINGSPEAK_ENDPOINT "/update?api_key="

const int DHTPin = D2;
DHT dht(DHTPin, DHTTYPE);

static char tempStr[7];
static char rhStr[7];
static char hicStr[7];
float temp[NUMBER_OF_MEASURES_BETWEEN_SUBMISIONS], rh[NUMBER_OF_MEASURES_BETWEEN_SUBMISIONS], hic[NUMBER_OF_MEASURES_BETWEEN_SUBMISIONS];

/* -------------------------- Function declaration -------------------------- */

void initWifi();
void fetchSensorData(uint8_t pos);
void calculateSmoothedValues(float *retRh, float *retTemp, float *retHic);
void submitData(float rh, float temp, float hic);

/* -------------------------- Application -------------------------- */

void setup() {
  // Initializing serial port for debugging purposes
  #if BUILD_TYPE==ENV_DEBUG
  Serial.begin(9600);
  delay(10);
  DEBUG_PRINTF("\n\n######### Running in dev mode! #########\n");
  #endif
  
  delay(DELAY_MULTIPLIER * START_DELAY_SECONDS);

  dht.begin();
  initWifi();
  
}

void loop() {
  // Reconnect code. Need improves
  if(WiFi.status() != WL_CONNECTED) {
    initWifi();
  }

  // Take spaced measures in the total time
  for (uint8_t i = 0; i < NUMBER_OF_MEASURES_BETWEEN_SUBMISIONS; i++)
  {
    fetchSensorData(i);
    delay(DELAY_MULTIPLIER * DELAY_BETWEEN_SUBMISIONS_SECONDS/NUMBER_OF_MEASURES_BETWEEN_SUBMISIONS);
  }
  
  // Apply smooth logic and submit data
  float hum=0, temperature, heatIndex;
  calculateSmoothedValues(&hum, &temperature, &heatIndex);
  if (hum > 0) // Just check if there are valid values
    submitData(hum, temperature, heatIndex);

  // Desconnect wifi because its gonna be a long rest
  if(DELAY_MULTIPLIER * DELAY_BETWEEN_SUBMISIONS_SECONDS > 3600000) {
    WiFi.disconnect();
  }
}

/* -------------------------- Function definitions -------------------------- */

// Establish a Wi-Fi connection with your router
void initWifi() {
  WiFi.disconnect(true);
  DEBUG_PRINTF("Connecting to: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint8_t retries;
  while(WiFi.status() != WL_CONNECTED) {
    retries++;
    delay(200);
    DEBUG_PRINTF(".");
  }
  DEBUG_PRINTF("\nWiFi connected in %d ms, IP address: %s\n", retries*200, WiFi.localIP().toString().c_str());
}

// Get data from the sensor
void fetchSensorData(uint8_t pos) {
  rh[pos] = dht.readHumidity();
  temp[pos] = dht.readTemperature();
  hic[pos] = dht.computeHeatIndex(false);
  DEBUG_PRINTF("\tMeasure %d: RH: %.0f %%, T: %.2f ºC, HIC: %.2f ºC\n", pos, rh[pos], temp[pos], hic[pos]);
}

// Apply a smoothing algorithm. Im just averaging
void calculateSmoothedValues(float *retRh, float *retTemp, float *retHic) {
  float auxRh=0, auxTemp=0, auxHic=0;
  uint8_t numberOfValidMeasures=0;
  for (uint8_t i = 0; i < NUMBER_OF_MEASURES_BETWEEN_SUBMISIONS; i++)
  {
    if (!isnan(rh[i]) && !isnan(temp[i])) {
      auxRh += rh[i];
      auxTemp += temp[i];
      auxHic += hic[i];
      numberOfValidMeasures++;
    }
  }
  if(numberOfValidMeasures > 0) {
    *retRh = auxRh/numberOfValidMeasures;
    *retTemp = auxTemp/numberOfValidMeasures;
    *retHic = auxHic/numberOfValidMeasures;
  }
}

// Make an HTTP request to Thingspeak
void submitData(float rh, float temp, float hic) {

  dtostrf(temp, 6, 2, tempStr);
  dtostrf(rh, 6, 2, rhStr);
  dtostrf(hic, 6, 2, hicStr);
  DEBUG_PRINTF("Submit values: RH: %.0f %%, T: %.2f ºC, HIC: %.2f ºC\n", rh, temp, hic);
  
  DEBUG_PRINTF("Connecting to %s .", THINGSPEAK_SERVER);
  
  WiFiClient client;
  int retries = 5;
  while(!!!client.connect(THINGSPEAK_SERVER, 80) && (retries-- > 0)) {
    DEBUG_PRINTF(".");
  }
  DEBUG_PRINTF("\n");

  if(!!!client.connected()) {
    DEBUG_PRINTF("/!\\ Failed to connect Thingspeak. Aborting measure.\n");
    return;
  }
  
  DEBUG_PRINTF("\tCalling endpoint %s\n", THINGSPEAK_ENDPOINT);
  client.print(String("GET ") + THINGSPEAK_ENDPOINT + CHANNEL_API_KEY + "&field1=" + rhStr + "&field2=" + tempStr + "&field3=" + hicStr +
                  " HTTP/1.1\r\n" +
                  "Host: " + THINGSPEAK_SERVER + "\r\n" + 
                  "Connection: close\r\n\r\n");
                  
  retries = 5 * 10; // 5 seconds             
  while(!!!client.available() && (retries-- > 0)){
    delay(100);
  }
  if(!!!client.available()) {
    DEBUG_PRINTF("/!\\ Thingspeak connection timeout.\n");
  }
  // while(client.available()){
  //   DEBUG_PRINTF("%d\n", client.read());
  // }
  
  DEBUG_PRINTF("\tConnection to Thingspeak finished.\n");
  client.stop();
}