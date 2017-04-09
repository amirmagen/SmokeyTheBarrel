/*  This code has been tested on the WeMos D1 mini, an ESP8266 based platform
 *  It is designed to control the temperature of a UDS (ugly drum smoker) 
 *  with a 12V DC blower fan, some k-type thermocouples, and control via
 *  the Blynk app on a smartphone or tablet. It pushes the temperature readings
 *  to Thingspeak, which can be used to trigger other events like email notifications
 *  or Tweets. My UDS tweets me when the meat is almost ready, or when the pit temp
 *  gets too low (time to add more charcoal). Follow it @SmokeyTheBarrel
 *  Code by Will Blevins. Feel free to reuse and redistribute the code however you like.
 */

#include <SimpleTimer.h>
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#define BLYNK_PRINT Serial    // Comment this out to disable prints and save space
#include "max6675.h"
#include <SPI.h>

//initialize for thingspeak and wifi
SimpleTimer timer;  //Starts timer to run the thingspeak update every x milliseconds
String apiKey = "<your Thingspeak API key here>";
char ssid[] = "<your wifi SSID here>";
char password[] = "<your wifi password here>";
char* server = "api.thingspeak.com";
WiFiClient client;

//initialize for Blynk
char auth[] = "<your Blynk key here>";


//initialize the fan controlling PWM pin and clock speed
int fanControlpin = D0;
int fanSpeed = 400; // value between 0-1020
int minSpeed= 5;
int PWMfreq=30000;

bool E_StillCooking=true;  // when this gets set to false, the fan turns off completely and stays off

//initialize target/pull temp setting knobs
// https://github.com/JChristensen/Thermocouple
// initialize for thermocouples   http://www.14core.com/wiring-thermocouple-max6675-on-esp8266-12e-nodemcu/
int const thermoSOpin = D6; // pin D6  *MISO*
int const thermoSCKpin = D5;  // pin D5 *CLK*
int const probe_A_CSpin = D1;  // 
int const probe_B_CSpin = D2; // 
int const probe_C_CSpin = D3; //  
int const probe_D_CSpin =D4; // 
int const probe_E_CSpin = D7; //  This probe will be used to measure the meat internal temperature

MAX6675 probe_A_Thermocouple(thermoSCKpin, probe_A_CSpin, thermoSOpin);
MAX6675 probe_B_Thermocouple(thermoSCKpin, probe_B_CSpin, thermoSOpin);
MAX6675 probe_C_Thermocouple(thermoSCKpin, probe_C_CSpin, thermoSOpin);
MAX6675 probe_D_Thermocouple(thermoSCKpin, probe_D_CSpin, thermoSOpin);
MAX6675 probe_E_Thermocouple(thermoSCKpin, probe_E_CSpin, thermoSOpin);  
int targetTemp=125;
int pullTemp=95;
float probe_A=25;
float probe_B=25;
float probe_C=25;
float probe_D=25;
float probe_E=25;

float tempWeightedAvg=25;
float probe_A_Last=25;
float probe_B_Last=25;
float probe_C_Last=25;
float probe_D_Last=25;
float probe_E_Last=25;

float tempWeightedAvgLast= 25;

BLYNK_WRITE(V0) {   //pulls data from Blynk app for target temp
  int targetTempBlynk = param.asInt();
  targetTemp = targetTempBlynk;
  Serial.printf("New target temperature = %d\n",targetTemp);
}
BLYNK_WRITE(V1) { //pulls data from Blynk app for pull temp
  int pullTempBlynk = param.asInt();
  pullTemp = pullTempBlynk;
  Serial.printf("New pull temperature = %d\n",pullTemp);
}
BLYNK_READ(V2){ //sends Blynk app the data for 
  Blynk.virtualWrite(V2, tempWeightedAvgLast);
}
BLYNK_READ(V3){
  Blynk.virtualWrite(V3, probe_E_Last);
}
BLYNK_READ(V4){ //sends Blynk app the data for the food probe
  Blynk.virtualWrite(V4, tempWeightedAvgLast);
}
BLYNK_READ(V5){
  Blynk.virtualWrite(V5, probe_E_Last);
}


void setup() {                
  Serial.begin(9600);
  delay(10);
  pinMode(fanControlpin,OUTPUT); //configure the fan controller 
  analogWriteFreq(PWMfreq);
  analogWrite(fanControlpin, 300); // start the fan off

// connect to wifi
  Serial.printf("\nConnecting to %s\n",ssid);
  Blynk.begin(auth, ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n\nWiFi connected\n\n");
  timer.setInterval(20000, postToThingspeak);  // commented out until we want to re-enable internet connectivity
  timer.setInterval(5000, readTempSensors);
  timer.setInterval(10000, fanController);
}
  
void loop() {
  if (E_StillCooking){
  timer.run();
  Blynk.run();
   
    delay(500); 
    
  } else {
    delay(60000);
  } 
}

void postToThingspeak(){
  if (client.connect(server,80)) {  //   "184.106.153.149" or api.thingspeak.com
    String postStr = apiKey;
           postStr +="&field1=";
           postStr += String(probe_A_Last);
           postStr +="&field2=";
           postStr += String(probe_B_Last);
           postStr +="&field3=";
           postStr += String(probe_C_Last);
           postStr +="&field4=";
           postStr += String(probe_D_Last);
           postStr +="&field5=";
           postStr += String(probe_E_Last);
           postStr +="&field6=";
           postStr += String(targetTemp);
           postStr +="&field7=";
           postStr += String(pullTemp);
           postStr += "\r\n\r\n"; 
     client.print("POST /update HTTP/1.1\n"); 
     client.print("Host: api.thingspeak.com\n"); 
     client.print("Connection: close\n"); 
     client.print("X-THINGSPEAKAPIKEY: "+apiKey+"\n"); 
     client.print("Content-Type: application/x-www-form-urlencoded\n"); 
     client.print("Content-Length: "); 
     client.print(postStr.length()); 
     client.print("\n\n"); 
     client.print(postStr);
     delay(200);
     client.stop();
     Serial.println("sent packet to thingspeak");
  }
}

void fanController(){
  if(probe_E_Last+1 <= pullTemp){  //check if the meat is almost done
    int tempBelowTarget=targetTemp-tempWeightedAvgLast ;
      if(tempBelowTarget > 0){
        int newSpeed= 1000;
        if (tempBelowTarget <= 19){
           newSpeed=((sq(tempBelowTarget*0.5))+(0.2*tempBelowTarget)+15)*10;
        }
         analogWrite(fanControlpin, newSpeed);
         fanSpeed=newSpeed;
         Serial.printf("** Turned fan on with speed = %d%.\n",(int)(fanSpeed/10));
      }else if(fanSpeed > minSpeed){
          fanSpeed=minSpeed;
         analogWrite(fanControlpin, fanSpeed);
         Serial.println("** Turned fan off.");
      }else{
         Serial.println("** Fan is off.");
      }
  }else{  // if the meat is at the pull temp, turn off fan, and notify that the food is done.
     fanSpeed=minSpeed;
     analogWrite(fanControlpin, fanSpeed);
     Serial.println("Turned fan off.");
     E_StillCooking=false;
     Serial.println("#### Finished! Time to pull :-D  ####");
  }
}


void readTempSensors(){
   probe_A=probe_A_Thermocouple.readCelsius();
   delay(100);
   probe_B=probe_B_Thermocouple.readCelsius();
      delay(100);
   probe_C=probe_C_Thermocouple.readCelsius();
      delay(100);
   probe_D=probe_D_Thermocouple.readCelsius();
      delay(100);
   probe_E=probe_E_Thermocouple.readCelsius(); 

   Serial.printf("pull\t|target\t|A\t|B\t|C\t|D\t|E\n%d\t|%d\t|%d\t|%d\t|%d\t|%d\t|%d\n",(int)pullTemp,(int)targetTemp,(int)probe_A_Last,(int)probe_B_Last,(int)probe_C_Last,(int)probe_D_Last,(int)probe_E_Last);
   /*
 
   Serial.printf("Probe A temperature = %d\n",(int)probe_A_Last);
   Serial.printf("Probe B temperature = %d\n",(int)probe_B_Last);
   Serial.printf("Probe C temperature = %d\n",(int)probe_C_Last);
   Serial.printf("Probe D temperature = %d\n",(int)probe_D_Last);
   Serial.printf("Probe E temperature | pull temperature = %d | %d\n\n",(int)probe_E_Last,(int)pullTemp);
   Serial.printf("Meat temperature = %d\n\n",(int)probe_E_Last);
   */
   
    tempWeightedAvg=(probe_A+probe_B+probe_C+probe_D)/4;

    probe_A_Last=(2*probe_A_Last+probe_A)/3;
    probe_B_Last=(2*probe_B_Last+probe_B)/3;
    probe_C_Last=(2*probe_C_Last+probe_C)/3;
    probe_D_Last=(2*probe_D_Last+probe_D)/3;
    probe_E_Last=(2*probe_E_Last+probe_E)/3;
    tempWeightedAvgLast=(2*tempWeightedAvgLast+tempWeightedAvg)/3;
}



