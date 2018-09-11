/************************************************************
Date: Sept 2018
Code is used for measuring how often a motorized desk changes height.
Chip: ESP8266 - Node MCU
Sensor: Ultrasonic SR04
Author: L.Butcher and N.Joneja
************************************************************/
/*************************Headers***************************/
#include <Time.h>
#include <TimeLib.h>
#include <RunningMedian.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <WiFiConnect.h>



#define MQTT_KEEPALIVE 60
#define MQTT_MAX_PACKET_SIZE 512


/************************Define Pins***************************/
// defines pins numbers
const int trigPin1 = D8;
const int echoPin1 = D7;  
//const int trigPin2 = D6;
//const int echoPin2 = D5;
/*********************** Global Variables**********************/
long duration1;
//long duration2;
int distance1;
//int distance2;
int movementThreshold = 6;
int baseline;
int baselineSize = 15;
int prevHeight =0;
int newHeight =0;
int heightCheckSize = 7;
int chunkSize = 5;
int max_height = 300;
int min_height = 5;

RunningMedian recordedHeights = RunningMedian(chunkSize);
//How much the measurements in each chunk are allowed to differ to still be considered constant
int chunkThreshold = 10;


unsigned long connect_time;
unsigned long last_update;

int delayval = 100; 


const int mqttPort = 1883;

//EDIT THESE 3 VALUES
const char* clientName = "";
const char* topic_pub = "Desks/pub";    //write to this topic
const char* topic_request_pub = "Desks/requests";    //write to this topic
const char* topic_sub = "Desks/sub";  //listen to this topic

String gitCommitID = "";

String topicString;
char topicChar[18];



WiFiClient espClient;         //wifi client
PubSubClient client(espClient); //MQTT client requires wifi client


/****************setup wifi************************************/
void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  long rssi = WiFi.RSSI();
  Serial.print("RSSI:");
  Serial.println(rssi);

  topicString = WiFi.macAddress();          //sets up the unique clientName using the MacAddress
  topicString.toCharArray(topicChar, 18);
  clientName = topicChar;
  Serial.println(topicChar);
}
/*****************Connect to MQTT Broker**********************************/
void ConnectBroker(PubSubClient client, const char* clientName)
{
    while (!client.connected())
    {
        Serial.print("Connecting to MQTT: ");
        Serial.println(clientName);
        if(client.connect(clientName))      //command to connect to MQTT broker with the unique client name
        {
          Serial.println("Connected");
        }
        else
        {
          Serial.print("Failed with state ");
          Serial.println(client.state());
          delay(200);
        }
    }
} 
/*****************reconnect to MQTT Broker if it goes down**********************************/
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect, just a name to identify the client
    if (client.connect(clientName)) {
      Serial.println("connected");
      
      
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" trying again in 3 seconds");
      // Wait 3 seconds before retrying
      delay(3000);
    }
  }
}

/***************Setup Routine******************************************************/
void setup() {
  pinMode(trigPin1, OUTPUT); // Sets the trigPin as an Output
  pinMode(echoPin1, INPUT); // Sets the echoPin as an Input
//  pinMode(trigPin2, OUTPUT); // Sets the trigPin as an Output
//  pinMode(echoPin2, INPUT); // Sets the echoPin as an Input
  Serial.begin(115200);
  Serial.println("UPDATED 7");
  
  setup_wifi();
  client.setServer(mqttServer,mqttPort);
  ConnectBroker(client, clientName);    //connect to MQTT borker
  client.setCallback(callback);         //Listener function
  client.subscribe(topic_sub);          //Topics to subscribe to

  
  // (timezone, daylight offset in seconds, server1, server2)
  // 3*3600 as setTimezone function converts seconds to hours
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("\nWaiting for time");
  while (!time(nullptr)) {
    Serial.print(".");
    delay(250);
  }
  Serial.println("Configured time.");

  ///
    /// Don't know why but it disconnects often without multiple client.loop() even with increased keepalive time
  ///
  client.loop();
    delay(2000);
    Serial.println("Getting current time");
    client.loop();
      sendStartupMessage();
      
    client.loop();
      Serial.print("Getting Baseline");
      makeBaseline();
      client.loop();
}
/***************Baseline Measurements******************************************************/
void makeBaseline() {
  int total = 0;
  RunningMedian measurements = RunningMedian(baselineSize);   //takes number of measurements defined above and returns median
  for (int i = 0; i < baselineSize; i++) {
    measurements.add(getHeight());
  }
  baseline = measurements.getMedian();
  Serial.println("Baseline: ");
  Serial.println(baseline); 
}

/***************Startup Message - sent on bootup connection******************************************************/
//To universal Topic
void sendStartupMessage(){
  
  time_t now = time(nullptr);
  connect_time = time(nullptr);
  Serial.println("Time is now: ");
  Serial.println(now);
  
  StaticJsonBuffer<100> JSONbuffer;            //Creates JSON message
  JsonObject& JSONencoder = JSONbuffer.createObject();
  JSONencoder["id"] = clientName;
  JSONencoder["startuptime"] = now;
  char JSONmessageBuffer[100];
  JSONencoder.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
  client.publish(topic_request_pub, JSONmessageBuffer, false);
  
}

/***************Loop - Main Function******************************************************/
void loop() {
  if (!client.connected()) {
    reconnect();
    client.subscribe(topic_sub);
  }
  
  client.loop();

  checkHeight();
  Serial.println("Height: ");
  Serial.println(recordedHeights.getMedian());
  client.loop();
  Serial.println("Client ID: ");
  Serial.println(clientName);
  Serial.println("old height: ");
  Serial.println(prevHeight);
  Serial.println("new height: ");
  Serial.println(newHeight);
  
  delay(10000);
}

/***************Validates Range of measurement******************************************************/
boolean measurementsWithinRange(){
  int maxVal = recordedHeights.getHighest();
  int minVal = recordedHeights.getLowest();
  int diff = maxVal-minVal;           //checks for a maximum distance change between readings
  bool test = false;

  if (diff < chunkThreshold){
    if (recordedHeights.getMedian() < max_height && recordedHeights.getMedian() > min_height)   //checks to make sure reading is withing limits
      test = true;
  }
  
  return test;   //return if diff is less than the chunkthreshold
}

/***************Check Height******************************************************/
void checkHeight() {
  if (!prevHeight) {
    prevHeight = baseline;       //on first measurement use baseline
  }
  for(int i = 0; i<chunkSize;i++){
    recordedHeights.add(getHeight());
  }
    if(measurementsWithinRange()){
        newHeight = recordedHeights.getMedian();
    if (abs(newHeight - prevHeight) >= movementThreshold) { //if the movement is greater than the threshold there is a new height
      Serial.println("**********Sending*********");
      Serial.println("old: ");
      Serial.println(prevHeight);
      Serial.println("new: ");
      Serial.println(newHeight);
      
      //sendHeight(prevHeight, newHeight);
      sendHeight();
      prevHeight = newHeight;
      }
    }
  
}

/***************Get Height******************************************************/
int getHeight() {
  int realDistance;

  RunningMedian measurements = RunningMedian(heightCheckSize);

  // Clears the trigPin
  for (int i = 0; i < heightCheckSize; i++) {
    
    digitalWrite(trigPin1, LOW);
    //digitalWrite(trigPin2, LOW);
    //delayMicroseconds(2);
    // Sets the trigPin on HIGH state for 10 micro seconds
    digitalWrite(trigPin1, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin1, LOW);
    duration1 = pulseIn(echoPin1, HIGH);    //echo pin changes to HIGH when pulse is read - duration measures time
//    delayMicroseconds(10);
 //   digitalWrite(trigPin2, HIGH);
 //   delayMicroseconds(10);
 //   digitalWrite(trigPin2, LOW);
    // Reads the echoPin, returns the sound wave travel time in microseconds
 //   duration2 = pulseIn(echoPin2, HIGH);
   // Calculating the distance
    distance1 = (duration1 * 0.0343) / 2;
//    distance2 = duration2 * 0.034 / 2;
    // Prints the distance on the Serial Monitor
    //Serial.println("dist1");
    //Serial.println(distance1);
    //Serial.println("dist2");
    //Serial.println(distance2);
/*
    if (distance1 >= distance2) {
      realDistance = distance1;
    }
    else {
      realDistance = distance2;
    }
    measurements.add(realDistance);
    */

    measurements.add(distance1);
    delay(150);
  }

  realDistance = measurements.getMedian();

   
  
  Serial.println(realDistance);
  return realDistance;
}
/***************send Height******************************************************/

//void sendHeight(int oldHeight, int newHeight) {
void sendHeight() {  
  last_update = time(nullptr);
  StaticJsonBuffer<300> JSONbuffer;
  JsonObject& JSONencoder = JSONbuffer.createObject();
  JSONencoder["Id"] = clientName;
  JSONencoder["Oldheight"] = prevHeight;
  JSONencoder["Newheight"] = newHeight;
  JSONencoder["Time"] = time(nullptr);
  char JSONmessageBuffer[200];
  JSONencoder.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));

  
 client.publish(topic_pub, JSONmessageBuffer, false);
}


/*****************MQTT Listener******************************************************/
void callback(char* topic, byte* payload, unsigned int length2){
  //topicString = WiFi.macAddress();
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);

   
  Serial.print("Message: ");
  for(int i = 0; i<length2;i++){
    Serial.print((char)payload[i]);
  }

  Serial.println();
  Serial.println("-------------");
  payload[length2] = 0;
  StaticJsonBuffer<300> JSONbuffer; 
  String inData = String((char*)payload);
  JsonObject& root = JSONbuffer.parseObject(inData);


    String ID = root["ID"];
    if(ID == "All" || ID == topicString){
      String request = root["details"];
      if(request == "height"){
        JsonObject& JSONencoder = JSONbuffer.createObject();
        JSONencoder["currentHeight"] = getHeight();
        JSONencoder["previousRecordedHeight"] = prevHeight;
        char JSONmessageBuffer[100];
        JSONencoder.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
        client.publish(topic_request_pub, JSONmessageBuffer);
      }           
      else if(request == "diagnostics"){
        Serial.println("-----Getting Diagnostic Data--------");
        JsonObject& JSONencoder = JSONbuffer.createObject();
        JSONencoder["ID"] = clientName;
        JSONencoder["Connected"] = connect_time;
        JSONencoder["LastUpdate"] = last_update;
        JSONencoder["WiFiSig"] = WiFi.RSSI();
        JSONencoder["Height"] = getHeight();
        char JSONmessageBuffer[300];
        JSONencoder.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
        client.publish(topic_request_pub, JSONmessageBuffer);
        Serial.println(JSONmessageBuffer);
          } 
       else if (request == "update"){
        updateFirmware();      
       }
  
    }
  
}

/*****************Firmware Upate******************************************************/

void updateFirmware(){
  
 // t_httpUpdate_return ret = ESPhttpUpdate.update("http://99.231.14.167/update");
    t_httpUpdate_return ret = ESPhttpUpdate.update("http://nj2299.duckdns.org/UpdateSitStand");

      Serial.println(ret);
        switch(ret) {
            case HTTP_UPDATE_FAILED:
                Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
                break;
                
            case HTTP_UPDATE_OK:
                Serial.println("HTTP_UPDATE_OK");
                break;

            case HTTP_UPDATE_NO_UPDATES:
                Serial.println("NO UPDATES");
                break;

           default:
              Serial.println("Something else...");
              break;
        }
}

