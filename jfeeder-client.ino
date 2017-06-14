/*
 * jfeeder-client.ino
 */

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Hash.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "WiFiManager.h"
#include "HX711.h"

HX711 scale(12, 14);

ESP8266WiFiMulti WiFiMulti;
WebSocketsClient webSocket;


#define USE_SERIAL Serial
ArduinoJson::DynamicJsonBuffer jsonBuffer;
#define DEFAULT_MEAL_AMOUNT 5;

volatile int onMeal;
volatile boolean bOnRequest = false;
volatile int errCount = 0;

String sid = "4OI5fC0CzgQc2P8GdoQ2OJVnHBz1";

#define DIR_PIN 13
#define STEP_PIN 15

int CMD_INIT = 1000;
int CMD_SET = 1001;
int CMD_GET = 1002;

int ERR_CD_SUCCESS = 0;
int ERR_CD_INVALIDATED_PARAM = 1;
int ERR_CD_DEFAULT = 10;

int LED = 5;    // Use D1, GPIO5
int pushButton = 4;  // Use D2, GPIO4

bool isScaleReady = false;

float calibrationFactor = 7050;
float weightAdjustFactor = 1189.7;
int weightBoul = 79;
int weight = 0;
int oldWeight = 0;
int maxWeight = 45;

typedef struct {
   String key;
   int val;
} DataSet;


const int stepsPerRevolution = 400;


void configModeCallback (WiFiManager *myWiFiManager) {
  USE_SERIAL.println("Entered config mode");
  USE_SERIAL.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  USE_SERIAL.println(myWiFiManager->getConfigPortalSSID());
}




void processMeal(){

//  USE_SERIAL.println(onMeal);

  if(onMeal > 0){

    isScaleReady = false;

    digitalWrite(LED, HIGH);
    yield();

    while(onMeal > 0){
      // step motor on
      rotateDeg(-360, .2);
      yield();

      onMeal--;
      USE_SERIAL.println(onMeal);


      weight = getWeight();
      yield();

      USE_SERIAL.print("weight : ");
      USE_SERIAL.println(weight);

      if(weight > maxWeight){
        onMeal = 0;
      }

      if(onMeal == 0){

        USE_SERIAL.print("cmdSet - onMeal : ");
        USE_SERIAL.println(onMeal);
        int dataCount = 2;
        DataSet dataSet[2] = {
          {"onMeal", onMeal},
          {"weight", weight}
        };
        cmdSet(dataSet, dataCount);
      }
    }


  } else {
    digitalWrite(LED, LOW);
    yield();
  }

}


void parseData (String text){

    USE_SERIAL.println(text);

    JsonObject& root = jsonBuffer.parseObject(text);

    if (root.success()){
      int errCode = root["err_cd"].as<int>();
      onMeal = root["data"]["onMeal"].as<int>();

      USE_SERIAL.print("errCode : ");
      USE_SERIAL.println(errCode);
      USE_SERIAL.print("onMeal : ");
      USE_SERIAL.println(onMeal);
    }

    delay(100);
    bOnRequest = false;
}

String setSendData (int cmd, DataSet dataSet[], int dataCount){
  JsonObject& root = jsonBuffer.createObject();
  root["cmd"] = cmd;
  root["sid"] = sid;

  JsonArray& data = root.createNestedArray("data");

  if(dataCount > 0){
    JsonObject& child = jsonBuffer.createObject();
    for(int i=0;i<dataCount;i++){
      child[dataSet[i].key] = dataSet[i].val;
    }
    data.add(child);
  }
  root.printTo(USE_SERIAL);
  USE_SERIAL.println();

  String output;
  root.printTo(output);
  return output;
}


void cmdGet(){

  int dataCount = 0;
  DataSet dataSet[0];

  String sendData = setSendData(CMD_GET, dataSet, dataCount);
  webSocket.sendTXT(sendData);
}


void cmdSet(DataSet dataSet[], int dataCount){
  String sendData = setSendData(CMD_SET, dataSet, dataCount);
  webSocket.sendTXT(sendData);
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t lenght) {


    switch(type) {
        case WStype_DISCONNECTED:
            USE_SERIAL.printf("[WSc] Disconnected!\n");
            break;
        case WStype_CONNECTED:
            {
                USE_SERIAL.printf("[WSc] Connected to url: %s\n",  payload);
                cmdGet();
                isScaleReady = true;
            }
            break;
        case WStype_TEXT:
        {
            USE_SERIAL.printf("[WSc] get text: %s\n", payload);
            String text = String((char *) &payload[0]);

            parseData(text);
            isScaleReady = true;
        }
      // send message to server
      // webSocket.sendTXT("message here");
            break;
        case WStype_BIN:
            USE_SERIAL.printf("[WSc] get binary lenght: %u\n", lenght);
            hexdump(payload, lenght);

            // send data to server
            // webSocket.sendBIN(payload, lenght);
            break;
    }
}


void buttonInterrupt(){
  if(!bOnRequest){
    USE_SERIAL.println("button down");

    if(onMeal > 0){
      onMeal = 0;

      int dataCount = 1;
      DataSet dataSet[1] = {
        {"onMeal", onMeal}
      };
//      bOnRequest = true;
      errCount = 0;
      cmdSet(dataSet, dataCount);

    } else {
      onMeal = DEFAULT_MEAL_AMOUNT;
    }

    USE_SERIAL.print("onMeal : ");
    USE_SERIAL.println(onMeal);

  } else {
    USE_SERIAL.println("bOnRequest");
    errCount++;

    if(errCount > 5){
      errCount = 0;
      webSocket.disconnect();
      delay(100);
      startClient();
      delay(100);
      bOnRequest = false;
    }
  }
}

void startClient(){
  webSocket.begin("mamma.neosave.me", 8081);
  webSocket.onEvent(webSocketEvent);
}

void setup() {



    pinMode(LED, OUTPUT);
    pinMode(pushButton, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pushButton), buttonInterrupt, FALLING);

    // set step motor
    pinMode(DIR_PIN, OUTPUT);
    pinMode(STEP_PIN, OUTPUT);

    // init variables
    onMeal = 0;

    USE_SERIAL.begin(115200);

    USE_SERIAL.setDebugOutput(true);

    USE_SERIAL.println("start");

    USE_SERIAL.println();
    USE_SERIAL.println();
    USE_SERIAL.println();

      for(uint8_t t = 4; t > 0; t--) {
          USE_SERIAL.printf("[SETUP] BOOT WAIT %d...\n", t);
          USE_SERIAL.flush();
          delay(1000);
      }


    /*
    //WiFiManager
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;
    //reset settings - for testing
    wifiManager.resetSettings();

    //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
    wifiManager.setAPCallback(configModeCallback);

    //fetches ssid and pass and tries to connect
    //if it does not connect it starts an access point with the specified name
    //here  "AutoConnectAP"
    //and goes into a blocking loop awaiting configuration
    if(!wifiManager.autoConnect("rockk_iPhone6", "8111021102")) {
      Serial.println("failed to connect and hit timeout");
      //reset and try again, or maybe put it to deep sleep
      ESP.reset();
      delay(1000);
    }

    Serial.println("Wife connected.");
    */

    WiFiMulti.addAP("rockk", "7903190319");
//    WiFiMulti.addAP("rockk_iPhone6", "8111021102");
//    WiFiMulti.addAP("SKP1002658MN001", "7903190319");

    //WiFi.disconnect();
    while(WiFiMulti.run() != WL_CONNECTED) {
        delay(100);
    }

    startClient();


    // HX711.DOUT  - D6 : 12
    // HX711.PD_SCK - D5 : 14
    scale.set_scale(calibrationFactor);
//    scale.tare();

//    webSocket.begin("mamma.neosave.me", 8081);
//    webSocket.onEvent(webSocketEvent);


}

int getWeight(){
    int w = ((scale.get_units(10) - weightAdjustFactor) * 15.8) - weightBoul;
    Serial.print("weight:\t");
    Serial.println(w);
    return w;
}

void checkWeight() {
  if(isScaleReady){
//    Serial.print("one reading:\t");
//    Serial.print(scale.get_units(), 1);
//    scale.set_scale(calibrationFactor);
    weight = getWeight();
    yield();

    if(weight > maxWeight){
        onMeal = 0;
    }

    if(oldWeight != weight){

      Serial.println("update weight");

      oldWeight = weight;

      int dataCount = 2;
      DataSet dataSet[2] = {
        {"onMeal", onMeal},
        {"weight", weight}
      };
      cmdSet(dataSet, dataCount);
    }

    delay(5000);
  }
}

void loop() {
  webSocket.loop();
  processMeal();
  checkWeight();
}

void rotate(int steps, float speed){
  //rotate a specific number of microsteps (8 microsteps per step) - (negitive for reverse movement)
  //speed is any number from .01 -> 1 with 1 being fastest - Slower is stronger
  int dir = (steps > 0)? HIGH:LOW;
  steps = abs(steps);

  digitalWrite(DIR_PIN,dir);

  float usDelay = (1/speed) * 70;

  for(int i=0; i < steps; i++){
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(usDelay);

    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(usDelay);
  }
}

void rotateDeg(float deg, float speed){
  //rotate a specific number of degrees (negitive for reverse movement)
  //speed is any number from .01 -> 1 with 1 being fastest - Slower is stronger
  int dir = (deg > 0)? HIGH:LOW;
  digitalWrite(DIR_PIN,dir);

  int steps = abs(deg)*(1/0.225);
  float usDelay = (1/speed) * 70;

  for(int i=0; i < steps; i++){
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(usDelay);

    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(usDelay);
  }
}
