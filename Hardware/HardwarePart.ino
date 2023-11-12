#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "time.h"
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include "QuickMedianLib.h"

//variables: 

#define NUM 3

// Insert Firebase project API Key
#define API_KEY "AIzaSyCq2kWIjZkwoL6cpusgq9Vu5iDFtDeNYPQ"

// Insert Authorized Email and Corresponding Password
#define USER_EMAIL "queuingsmart@gmail.com"
#define USER_PASSWORD "12345678"

// Insert RTDB URLefine the RTDB URL
#define DATABASE_URL "https://smartqueue-60f8d-default-rtdb.europe-west1.firebasedatabase.app/"

void WriteReading(int* sample, int threshold, int size);
void SetJson(int* sample, int threshold, int size);
void updateSendDataPrevMillis();
bool isFireBaseReady();
void InitFirebase();
unsigned long getTime();
void GetSample(long* result);
long GetAverageDistance(int trig, int echo);
long GetDistance(int trig, int echo);
void InitSensors();
void InitWiFi();

//firebase
// Define Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Variable to save USER UID
String uid;

// Database main path (to be updated in setup with the user UID)
String databasePath;

// Parent Node (to be updated in every loop)
String parentPath;

int timestamp;
FirebaseJson json;

const char* ntpServer = "pool.ntp.org";

// Timer variables (send new readings every three minutes)
unsigned long sendDataPrevMillis = 0;
unsigned long timerDelay = 1;

//wifi
const char* WIFI_SSID = "ICST";
const char* PASSWORD = "arduino123";

//sensors
const int sensorsNum = NUM;
const int averageTimes = 10;
const int samplesNum = 10;
const int height = 220;
const int min_size = 150;
const int offset = 10;
const int distThreshold = height - min_size - offset;
const int delayBetweenSensors = 100;
const int delayBetweenSamples = 100;

int trigs[NUM] = {13,5,18};
int echos[NUM] = {14,2,21};
float avg[NUM] = {0,0,0};

//wifi  
void InitWiFi() {
  WiFi.begin(WIFI_SSID, PASSWORD);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
  Serial.println();
}

//sensors

void InitSensors()
{
  for(int i = 0; i < sensorsNum; i++)
  {
    pinMode(trigs[i], OUTPUT);
    pinMode(echos[i], INPUT);
  }
}

long GetDistance(int trig, int echo)
{
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  long duration = pulseIn(echo, HIGH);
  return duration * 0.034 / 2;
}

long GetMedianDistance(int trig, int echo)
{
  long samples[samplesNum];
  for(int i = 0; i < samplesNum; i++)
  {
    long dist = GetDistance(trig, echo);
    samples[i] = dist;
    delay(delayBetweenSamples);
    // Serial.print("sample:");
    // Serial.println(dist);
  }
  return QuickMedian<long>::GetMedian(samples, samplesNum);
}

long GetAverageDistance(int trig, int echo, int sensor)
{
  String name = "sensor_average" + String(sensor) + ":"; 
  for(int i = 0; i < averageTimes; i++)
  {
    double result = GetDistance(trig, echo);
    if(result <= height){
      avg[sensor] = 0.8*avg[sensor] + 0.2*result;
      Serial.println("0 220 ");
      Serial.print("sensor " + String(i)+":");
      Serial.println(avg[sensor]);
    }
    delay(delayBetweenSamples);
  }

  return (long)avg[sensor];
}

void GetSample(long* result)
{
    for (int i = 0; i < sensorsNum; i++)
    {
      String varName = "sensor" + String(i) + ":";
      long dist = GetAverageDistance(trigs[i], echos[i], i);
      result[i] = dist;
      // Serial.print(varName);
      // Serial.println(dist);
      delay(delayBetweenSensors);
    }
}

//firebase
unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    return(0);
  }
  time(&now);
  return now;
}

void InitFirebase()
{
  configTime(0, 0, ntpServer);

  // Assign the api key (required)
  config.api_key = API_KEY;

  // Assign the user sign in credentials
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  // Assign the RTDB URL (required)
  config.database_url = DATABASE_URL;

  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(4096);

  // Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h

  // Assign the maximum retry of token generation
  config.max_token_generation_retry = 5;

  // Initialize the library with the Firebase authen and config
  Firebase.begin(&config, &auth);

  // Getting the user UID might take a few seconds
  Serial.println("Getting User UID");
  while ((auth.token.uid) == "") {
   Serial.print('.');
    delay(1000);
  }
  // Print user UID
  uid = auth.token.uid.c_str();
  Serial.print("User UID: ");
  Serial.println(uid);

  // Update database path
  databasePath = "/UsersData/" + uid + "/readings";
}


bool isFireBaseReady()
{
  return Firebase.ready() && (millis() - sendDataPrevMillis > timerDelay || sendDataPrevMillis == 0);
}

void updateSendDataPrevMillis()
{
  sendDataPrevMillis = millis();
}

void SetJson(float* sample, int threshold, int size)
{
  int numOfPeople = 0;
  for (int i = 0; i < size ; i++)
  {
    String distName = "distance/[" + String(i) + "]";
    json.set(distName.c_str(), sample[i]);
    String queueName = "queue/[" + String(i) + "]";
    json.set(queueName.c_str(), sample[i]<= threshold);
    numOfPeople += sample[i]<= threshold;
  }
  json.set("num_of_people", numOfPeople);
}

void WriteReading(float* sample, int threshold, int size)
{
  sendDataPrevMillis = millis();
  //Get current timestamp
  timestamp = getTime();
  parentPath= databasePath + "/" + String(timestamp);
  SetJson(sample, threshold, size);
  Serial.printf("Set json... %s\n", Firebase.RTDB.setJSON(&fbdo, parentPath.c_str(), &json) ? "ok" : fbdo.errorReason().c_str());
}

void new_read(int trig, int echo, int sensor, String name)
{
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  long duration = pulseIn(echo, HIGH);
  float result = duration * 0.034 / 2;
  if(result < 400) {
    avg[sensor] = avg[sensor] * 0.7 + result * 0.3;
    Serial.println("0 70 220 ");
    Serial.print(name);
    Serial.println(avg[sensor]);
    delay(90);
  }
}

void UpdateSample()
{
  new_read(trigs[0], echos[0], 0, "sensor1:");
  delay(100);
  new_read(trigs[1], echos[1], 1, "sensor2:");
  delay(100);
  new_read(trigs[2], echos[2], 2, "sensor3:");
  delay(100);
}

void setup(){
  Serial.begin (9600);
  InitSensors();
  InitWiFi();
  InitFirebase();
}

void WifiRecovery()
{
  if(WiFi.status() != WL_CONNECTED){
    InitWiFi();
  }
}

void loop()
{
    // if (Firebase.ready() && (millis() - sendDataPrevMillis > timerDelay || sendDataPrevMillis == 0)){
    // sendDataPrevMillis = millis();

    //Get current timestamp
    // timestamp = getTime();
    // Serial.print ("time: ");
    // Serial.println (timestamp);
    // String name = "distance";
    // long distArray[sensorsNum];
    // bool queue[sensorsNum];

    WifiRecovery();

    UpdateSample();

    if (Firebase.ready() && (millis() - sendDataPrevMillis > timerDelay || sendDataPrevMillis == 0)){
      parentPath= databasePath + "/0";
      SetJson(avg, distThreshold, sensorsNum);

      Serial.printf("Set json... %s\n", Firebase.RTDB.setJSON(&fbdo, parentPath.c_str(), &json) ? "ok" : fbdo.errorReason().c_str());
    }
}