#if defined(ESP32)
#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#include <WiFi.h>                                                //include wifi library 
#include "esp_wpa2.h" //wpa2 library for connections to Enterprise networks
#define DEVICE "ESP32"
#elif defined(ESP8266)
#include <ESP8266WiFiMulti.h>
ESP8266WiFiMulti wifiMulti;
#define DEVICE "ESP8266"
#endif

#include <WiFi.h>                                                //include wifi library 
#include "esp_wpa2.h" //wpa2 library for connections to Enterprise networks

//WiFiClient client;                                              //declare that this device connects to a Wi-Fi network,create a connection to a specified internet IP address

//----------------Fill in your Wi-Fi Credentials-------
#define EAP_IDENTITY "s01mn1@abdn.ac.uk" //if connecting from another corporation, use identity@organisation.domain in Eduroam 
#define EAP_USERNAME "s01mn1@abdn.ac.uk" //oftentimes just a repeat of the identity
#define EAP_PASSWORD "Saraonyx1234!" //your Eduroam password
const char* ssid = "eduroam"; // Eduroam SSID
const char* host = "radius.abdn.ac.uk"; //external server domain for HTTP connection after authentification
//------------------------------------------------------------------

#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

#define INFLUXDB_URL "https://eu-central-1-1.aws.cloud2.influxdata.com"
#define INFLUXDB_TOKEN "WyWehNHX2qKL6XE9WdoGzqa4GHfDxt0YSaLqelCaynH5lGIah1O2oKwU12gkKRGHkKTPVU9l6hWbTKuEu8sGGg=="
#define INFLUXDB_ORG "d3c11141892d5242"
#define INFLUXDB_BUCKET "ALS"

// Time zone info
#define TZ_INFO "UTC0"

// Declare InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

// Declare Data point
Point ALS("ALS");

String input;
String operation;

float T_inlet, T_pipe, Flow_rate = 0, Volume = 0;

int Heater_state, V_inlet, Pump_state, V_S1, V_S2, V_clean, V_disposal;

const int Inlet = 4; // Relay 2
const int Pump = 5; // Relay 3
const int Heater = 6; // Relay 4
const int Sample1 = 7; // Relay 5
const int Sample2 = 15; // Relay 6
const int Waste_Water = 16; // Relay 7
const int MilliQ_Water = 17; // Relay 8

int flushing_time = 5000;
int sampling_time = 5;
int filling_time = 5000;
int circulation_time = 10;
int heating_time = 10;

int pump_start;
int heater_start;
int elapsed_sampling_time, elapsed_heating_time;

float Sterilisation_Temp = 125.0;

const uint8_t ThermistorPin1 = 9;
const uint8_t ThermistorPin2 = 10;
const uint8_t FlowPin = 11;

int Vo;
float R1 = 10000;
float logR2, R2, T, Tc, Tf;
float c1 = 1.009249522e-03, c2 = 2.378405444e-04, c3 = 2.019202697e-07;

float calibrationFactor = 6.5; // 4.5
volatile byte pulseCount = 0;
float flowRate = 0.0;
unsigned int flowMilliLitres = 0;
unsigned long totalMilliLitres = 0;
unsigned int frac;
unsigned int f = 0, v = 0;
unsigned long oldTime = 0;

void setup()
{
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, WPA2_AUTH_PEAP, EAP_IDENTITY, EAP_USERNAME, EAP_PASSWORD);

  Serial.println("Connecting to wifi.....");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }
  Serial.println();

  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  // Check server connection
  if (client.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
  }

  pinMode(Inlet, OUTPUT);
  pinMode(Pump, OUTPUT);
  pinMode(Heater, OUTPUT);
  pinMode(Sample1, OUTPUT);
  pinMode(Sample2, OUTPUT);
  pinMode(Waste_Water, OUTPUT);
  pinMode(MilliQ_Water, OUTPUT);

  pinMode(ThermistorPin1, INPUT);
  pinMode(ThermistorPin2, INPUT);

  //  pinMode(FlowPin, INPUT);
  //  digitalWrite(FlowPin, HIGH);
  attachInterrupt(FlowPin, pulseCounter, FALLING); // Setup Interrupt

  system_reset();
  //  system_preparation();
  //  sampling1();
  //  cleaning();
  //  heating();
  //  sampling2();
  //  cleaning();
  //  heating();
}

void loop()
{
  while (Serial.available()) {
    input  = Serial.readStringUntil('\n');
  }
  int i1 = input.indexOf(',');
  int i2 = input.indexOf(',', i1 + 1);
  int i3 = input.indexOf(',', i2 + 1);
  int i4 = input.indexOf(',', i3 + 1);

  operation = input.substring(0, i1);
  totalMilliLitres = 0;

  if (operation == "R" || operation == "r")  {
    input = " ";
    totalMilliLitres = 0;
    system_reset();
  }

  if (operation == "P" || operation == "p")  {
    input = " ";
    totalMilliLitres = 0;
    system_preparation();
    system_reset();
  }

  if (operation == "S1" || operation == "s1")  {
    sampling_time = (input.substring(i1 + 1, i2)).toInt();
    input = " ";
    totalMilliLitres = 0;
    sampling1(sampling_time * 1000);
    system_reset();
  }

  if (operation == "S2" || operation == "s2")  {
    sampling_time = (input.substring(i1 + 1, i2)).toInt();
    input = " ";
    totalMilliLitres = 0;
    sampling2(sampling_time * 1000);
    system_reset();
  }

  if (operation == "C" || operation == "c")  {
    circulation_time = (input.substring(i1 + 1, i2)).toInt();
    input = " ";
    totalMilliLitres = 0;
    cleaning(circulation_time * 1000);
    system_reset();
  }

  if (operation == "H" || operation == "h")  {
    heating_time = (input.substring(i1 + 1, i2)).toInt();
    input = " ";
    totalMilliLitres = 0;
    heating(heating_time * 1000);
    system_reset();
  }
  
  T_inlet = Temp_inlet();
  T_pipe = Temp_pipe();
  //  Flow_rate = Flow();
  //  Volume = Vol();

  ALS.clearFields();
  ALS.addField("T_inlet", T_inlet);
  ALS.addField("T_pipe", T_pipe);
  ALS.addField("Flow_rate", Flow_rate);
  ALS.addField("Volume", Volume);
  ALS.addField("Heater", Heater_state);
  ALS.addField("V_inlet", V_inlet);
  ALS.addField("Pump_state", Pump_state);
  ALS.addField("V_S1", V_S1);
  ALS.addField("V_S2", V_S2);
  ALS.addField("V_Clean", V_clean);
  ALS.addField("V_Disposal", V_disposal);

  Serial.println(ALS.toLineProtocol());

  // Check WiFi connection and reconnect if needed
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wifi connection lost");
  }

  // Write point
  if (!client.writePoint(ALS)) {
    Serial.print("InfluxDB write failed: ");
    Serial.println(client.getLastErrorMessage());
  }
  delay(1000);
}

void system_reset() {
  Serial.println("System reset....");
  digitalWrite(Inlet, LOW);
  digitalWrite(Pump, LOW);
  digitalWrite(Heater, LOW);
  digitalWrite(Sample1, LOW);
  digitalWrite(Sample2, LOW);
  digitalWrite(Waste_Water, LOW);
  digitalWrite(MilliQ_Water, LOW);
  totalMilliLitres = 0;
  T_inlet = Temp_inlet();
  T_pipe = Temp_pipe();
  Flow_rate = Flow();
  Volume = Vol();
  Heater_state = 0;
  V_inlet = 0;
  Pump_state = 0;
  V_S1 = 0;
  V_S2 = 0;
  V_clean = 0;
  V_disposal = 0;
  Serial.print(operation);
  Serial.print(":");
  InfluxDB();
}

void system_preparation() {
  pump_start = millis();
  Serial.println("System in preparation....");
  while (millis() <= pump_start + flushing_time)
  {
    while (Serial.available()) {
      input  = Serial.readStringUntil('\n');
    }
    digitalWrite(Inlet, HIGH);
    delay(100);
    digitalWrite(Pump, HIGH);
    T_inlet = Temp_inlet();
    T_pipe = Temp_pipe();
    Flow_rate = Flow();
    Volume = Vol();
    Heater_state = 0;
    V_inlet = 1;
    Pump_state = 1;
    V_S1 = 0;
    V_S2 = 0;
    V_clean = 0;
    V_disposal = 0;
    Serial.print(operation);
    Serial.print(":");
    InfluxDB();
    delay(1000);
  }
  pump_start = millis();
  while (millis() <= pump_start + flushing_time)
  {
    while (Serial.available()) {
      input  = Serial.readStringUntil('\n');
    }
    digitalWrite(Inlet, LOW);
    delay(100);
    digitalWrite(Pump, HIGH);
    T_inlet = Temp_inlet();
    T_pipe = Temp_pipe();
    Flow_rate = Flow();
    Volume = Vol();
    Heater_state = 0;
    V_inlet = 0;
    Pump_state = 1;
    V_S1 = 0;
    V_S2 = 0;
    V_clean = 0;
    V_disposal = 1;
    Serial.print(operation);
    Serial.print(":");
    InfluxDB();
    delay(1000);
  }
  digitalWrite(Pump, LOW);
  T_inlet = Temp_inlet();
  T_pipe = Temp_pipe();
  Flow_rate = Flow();
  Volume = Vol();
  Heater_state = 0;
  V_inlet = 0;
  Pump_state = 0;
  V_S1 = 0;
  V_S2 = 0;
  V_clean = 0;
  V_disposal = 0;
  InfluxDB();
}

void sampling1(int sampling_time) {
  pump_start = millis();
  Serial.println("Sample 1 in progress....");
  while (millis() <= pump_start + sampling_time)
  {
    elapsed_sampling_time = millis() - pump_start;
    while (Serial.available()) {
      input  = Serial.readStringUntil('\n');
    }
    digitalWrite(Inlet, HIGH);
    digitalWrite(Pump, HIGH);
    digitalWrite(Sample1, HIGH);
    T_inlet = Temp_inlet();
    T_pipe = Temp_pipe();
    Flow_rate = Flow();
    Volume = Vol();
    Heater_state = 0;
    V_inlet = 1;
    Pump_state = 1;
    V_S1 = 1;
    V_S2 = 0;
    V_clean = 0;
    V_disposal = 0;
    Serial.print(operation);
    Serial.print(",");
    Serial.print(elapsed_sampling_time / 1000);
    Serial.print("/");
    Serial.print(sampling_time / 1000);
    //    Serial.print(",");
    //    Serial.print(circulation_time);
    //    Serial.print(",");
    //    Serial.print(heating_time);
    Serial.print(":");
    InfluxDB();
    delay(1000);
  }
  pump_start = millis();
  while (millis() <= pump_start + flushing_time)
  {
    while (Serial.available()) {
      input  = Serial.readStringUntil('\n');
    }
    digitalWrite(Inlet, LOW);
    digitalWrite(Sample1, LOW);
    T_inlet = Temp_inlet();
    T_pipe = Temp_pipe();
    Flow_rate = Flow();
    Volume = Vol();
    Heater_state = 0;
    V_inlet = 0;
    Pump_state = 1;
    V_S1 = 0;
    V_S2 = 0;
    V_clean = 0;
    V_disposal = 0;
    Serial.print(operation);
    Serial.print(",");
    Serial.print(elapsed_sampling_time / 1000);
    Serial.print("/");
    Serial.print(sampling_time / 1000);
    Serial.print(":");
    InfluxDB();
    delay(1000);
  }
  digitalWrite(Pump, LOW);
  T_inlet = Temp_inlet();
  T_pipe = Temp_pipe();
  Flow_rate = Flow();
  // Volume = Vol();
  Heater_state = 0;
  V_inlet = 0;
  Pump_state = 0;
  V_S1 = 0;
  V_S2 = 0;
  V_clean = 0;
  V_disposal = 0;
  Serial.print(operation);
  Serial.print(",");
  Serial.print(elapsed_sampling_time / 1000);
  Serial.print("/");
  Serial.print(sampling_time / 1000);
  Serial.print(":");
  InfluxDB();
}

void sampling2(int sampling_time) {
  pump_start = millis();
  Serial.println("Sample 2 in progress....");
  while (millis() <= pump_start + sampling_time)
  {
    while (Serial.available()) {
      input  = Serial.readStringUntil('\n');
    }
    digitalWrite(Inlet, HIGH);
    digitalWrite(Pump, HIGH);
    digitalWrite(Sample2, HIGH);
    T_inlet = Temp_inlet();
    T_pipe = Temp_pipe();
    Flow_rate = Flow();
    Volume = Vol();
    Heater_state = 0;
    V_inlet = 1;
    Pump_state = 1;
    V_S1 = 0;
    V_S2 = 1;
    V_clean = 0;
    V_disposal = 0;
    Serial.print(operation);
    Serial.print(",");
    Serial.print(sampling_time);
    Serial.print(":");
    InfluxDB();
    delay(1000);
  }
  pump_start = millis();
  while (millis() <= pump_start + flushing_time)
  {
    while (Serial.available()) {
      input  = Serial.readStringUntil('\n');
    }
    digitalWrite(Inlet, LOW);
    digitalWrite(Sample2, LOW);
    T_inlet = Temp_inlet();
    T_pipe = Temp_pipe();
    Flow_rate = Flow();
    Volume = Vol();
    Heater_state = 0;
    V_inlet = 0;
    Pump_state = 1;
    V_S1 = 0;
    V_S2 = 0;
    V_clean = 0;
    V_disposal = 0;
    Serial.print(operation);
    Serial.print(",");
    Serial.print(sampling_time);
    Serial.print(":");
    InfluxDB();
    delay(1000);
  }
  digitalWrite(Pump, LOW);
  T_inlet = Temp_inlet();
  T_pipe = Temp_pipe();
  Flow_rate = Flow();
  Volume = Vol();
  Heater_state = 0;
  V_inlet = 0;
  Pump_state = 0;
  V_S1 = 0;
  V_S2 = 0;
  V_clean = 0;
  V_disposal = 0;
  Serial.print(operation);
  Serial.print(",");
  Serial.print(sampling_time);
  Serial.print(":");
  InfluxDB();
}

void cleaning(int circulation_time) {
  pump_start = millis();
  Serial.println("Cleaning in progress....");
  // 1. Filling
  while (millis() <= pump_start + filling_time)
  {
    while (Serial.available()) {
      input  = Serial.readStringUntil('\n');
    }
    digitalWrite(MilliQ_Water, HIGH);
    digitalWrite(Waste_Water, HIGH);
    digitalWrite(Pump, HIGH);
    T_inlet = Temp_inlet();
    T_pipe = Temp_pipe();
    Flow_rate = Flow();
    Volume = Vol();
    Heater_state = 0;
    V_inlet = 0;
    Pump_state = 1;
    V_S1 = 0;
    V_S2 = 0;
    V_clean = 1;
    V_disposal = 0;
    Serial.print(operation);
    Serial.print(",");
    Serial.print(circulation_time/1000);
    Serial.print(":");
    InfluxDB();
    delay(1000);
  }
  digitalWrite(MilliQ_Water, LOW);
  digitalWrite(Pump, LOW);
  T_inlet = Temp_inlet();
  T_pipe = Temp_pipe();
  Flow_rate = Flow();
  Volume = Vol();
  Heater_state = 0;
  V_inlet = 0;
  Pump_state = 0;
  V_S1 = 0;
  V_S2 = 0;
  V_clean = 0;
  V_disposal = 0;
  Serial.print(operation);
  Serial.print(",");
  Serial.print(circulation_time/1000);
  Serial.print(":");
  InfluxDB();
  // 2. Circulating
  pump_start = millis();
  while (millis() <= pump_start + circulation_time)
  {
    while (Serial.available()) {
      input  = Serial.readStringUntil('\n');
    }
    digitalWrite(Pump, HIGH);
    T_inlet = Temp_inlet();
    T_pipe = Temp_pipe();
    Flow_rate = Flow();
    Volume = Vol();
    Heater_state = 0;
    V_inlet = 0;
    Pump_state = 1;
    V_S1 = 0;
    V_S2 = 0;
    V_clean = 0;
    V_disposal = 0;
    Serial.print(operation);
    Serial.print(",");
    Serial.print(circulation_time/1000);
    Serial.print(":");
    InfluxDB();
    delay(1000);
  }
  //  delay(circulation_time);
  digitalWrite(Pump, LOW);
  T_inlet = Temp_inlet();
  T_pipe = Temp_pipe();
  Flow_rate = Flow();
  Volume = Vol();
  Heater_state = 0;
  V_inlet = 0;
  Pump_state = 0;
  V_S1 = 0;
  V_S2 = 0;
  V_clean = 0;
  V_disposal = 0;
  Serial.print(operation);
  Serial.print(",");
  Serial.print(circulation_time/1000);
  Serial.print(":");
  InfluxDB();
  // 3. Flushing
  pump_start = millis();
  while (millis() <= pump_start + flushing_time)
  {
    while (Serial.available()) {
      input  = Serial.readStringUntil('\n');
    }
    digitalWrite(Waste_Water, LOW);
    digitalWrite(Pump, HIGH);
    T_inlet = Temp_inlet();
    T_pipe = Temp_pipe();
    Flow_rate = Flow();
    Volume = Vol();
    Heater_state = 0;
    V_inlet = 0;
    Pump_state = 1;
    V_S1 = 0;
    V_S2 = 0;
    V_clean = 0;
    V_disposal = 1;
    Serial.print(operation);
    Serial.print(",");
    Serial.print(circulation_time/1000);
    Serial.print(":");
    InfluxDB();
    delay(1000);
  }
  digitalWrite(Pump, LOW);
  T_inlet = Temp_inlet();
  T_pipe = Temp_pipe();
  Flow_rate = Flow();
  Volume = Vol();
  Heater_state = 0;
  V_inlet = 0;
  Pump_state = 0;
  V_S1 = 0;
  V_S2 = 0;
  V_clean = 0;
  V_disposal = 0;
  InfluxDB();
}

void heating(int heating_time) {
  heater_start = millis();
  Serial.println("Heating in progress....");
  while (millis() <= heater_start + heating_time)
  {
    if (T_pipe < 50) {
      elapsed_heating_time = millis() - heater_start;
      while (Serial.available()) {
        input  = Serial.readStringUntil('\n');
      }
      //    // 2. Circulating
      //    digitalWrite(Pump, HIGH);
      digitalWrite(Heater, HIGH);
      T_inlet = Temp_inlet();
      T_pipe = Temp_pipe();
      Flow_rate = Flow();
      Volume = Vol();
      Heater_state = 1;
      V_inlet = 0;
      Pump_state = 1;
      V_S1 = 0;
      V_S2 = 0;
      V_clean = 0;
      V_disposal = 0;
      Serial.print(operation);
      Serial.print(",");
      Serial.print(elapsed_heating_time / 1000);
      Serial.print("/");
      Serial.print(heating_time / 1000);
      Serial.print(":");
      InfluxDB();
    }
    if (T_pipe > 50) {
      digitalWrite(Heater, LOW);
      T_inlet = Temp_inlet();
      T_pipe = Temp_pipe();
      Flow_rate = Flow();
      Volume = Vol();
      Heater_state = 1;
      V_inlet = 0;
      Pump_state = 1;
      V_S1 = 0;
      V_S2 = 0;
      V_clean = 0;
      V_disposal = 0;
      Serial.print(operation);
      Serial.print(",");
      Serial.print(elapsed_heating_time / 1000);
      Serial.print("/");
      Serial.print(heating_time / 1000);
      Serial.print(":");
      InfluxDB();
    }
  }
  //  digitalWrite(Pump, LOW);
  digitalWrite(Heater, LOW);
  T_inlet = Temp_inlet();
  T_pipe = Temp_pipe();
  Flow_rate = Flow();
  Volume = Vol();
  Heater_state = 0;
  V_inlet = 0;
  Pump_state = 0;
  V_S1 = 0;
  V_S2 = 0;
  V_clean = 0;
  V_disposal = 0;
  Serial.print(operation);
  Serial.print(",");
  Serial.print(elapsed_heating_time / 1000);
  Serial.print("/");
  Serial.print(heating_time / 1000);
  Serial.print(":");
  InfluxDB();
  delay(1000);
}

float Temp_inlet() {
  Vo = analogRead(ThermistorPin1);
  R2 = R1 * (4095.0 / (float)Vo - 1.0);
  logR2 = log(R2);
  T = (1.0 / (c1 + c2 * logR2 + c3 * logR2 * logR2 * logR2));

  //  Vo = analogRead(ThermistorPin1) * (5.0 / 4095.0);
  //  R1 = R2 * ((Vin / Vo) - 1);
  //  logR1 = log(R1);
  //  T = (1.0 / (c1 + c2 * logR1 + c3 * logR1 * logR1 * logR1));
  Tc = T - 273.15 - 27;
  return Tc;
}

float Temp_pipe() {
  Vo = analogRead(ThermistorPin2);
  R2 = R1 * (4095.0 / (float)Vo - 1.0);
  logR2 = log(R2);
  T = (1.0 / (c1 + c2 * logR2 + c3 * logR2 * logR2 * logR2));

  Tc = T - 273.15 - 27;
  return Tc;
}

int Flow() {
  if ((millis() - oldTime) > 1000)   // Only process counters once per second
  {
    detachInterrupt(FlowPin);
    flowRate = ((1000.0 / (millis() - oldTime)) * pulseCount) / calibrationFactor;
    oldTime = millis();
    flowMilliLitres = (flowRate / 60) * 1000; // mL/Sec
    pulseCount = 0;

    attachInterrupt(FlowPin, pulseCounter, FALLING);
    f = flowMilliLitres;
    return f;
  }
}

int Vol() {
  totalMilliLitres += flowMilliLitres; // mL
  frac = (flowRate - int(flowRate)) * 10; // LPM

  v = totalMilliLitres;
  return v;
}

void pulseCounter() // Interrupt function
{
  pulseCount++;
}

void InfluxDB() {
  ALS.clearFields();
  ALS.addField("T_inlet", T_inlet);
  ALS.addField("T_pipe", T_pipe);
  ALS.addField("Flow_rate", Flow_rate);
  ALS.addField("Volume", Volume);
  ALS.addField("Heater", Heater_state);
  ALS.addField("V_inlet", V_inlet);
  ALS.addField("Pump_state", Pump_state);
  ALS.addField("V_S1", V_S1);
  ALS.addField("V_S2", V_S2);
  ALS.addField("V_Clean", V_clean);
  ALS.addField("V_Disposal", V_disposal);

  Serial.println(ALS.toLineProtocol());

  // Check WiFi connection and reconnect if needed
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wifi connection lost");
  }

  // Write point
  if (!client.writePoint(ALS)) {
    Serial.print("InfluxDB write failed: ");
    Serial.println(client.getLastErrorMessage());
  }
}
