#include <JeeLib.h>
#include <SPI.h>
#include <Ethernet.h>
#include <PusherClient.h>
#include <HttpClient.h>

#include <Time.h>  
#include <Wire.h>  
#include <DS1307RTC.h>  // a basic DS1307 library that returns time as a time_t
#include <TimeAlarms.h>


#define API_KEY "aaf8a36f8f5c57b42051"

//--------------
typedef struct { 
  float inside, outside;
} TempTX;

TempTX temperatures;
//

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xFF };
byte own_ip[] = { 192,168,1,201 };
char channel[] = "arduino";
char presence_channel[] = "presence-arduino";

PusherClient client;
MilliTimer sendTimer;
bool pending = false;
byte sendBuffer[18] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x00, 0x08, 0x0A, 0x30, 0x54, 0x57, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00};

struct {
  byte start;   // time at which we started listening for a packet
  byte later;   // how long we had to wait for packet to come in
} payload;

//AlarmID_t alarm1, alarm2;

// Number of milliseconds to wait without receiving any data before we give up
const int kNetworkTimeout = 30*1000;
// Number of milliseconds to wait if no data is available before trying again
const int kNetworkDelay = 1000;
  
EthernetClient c;
HttpClient http(c);
String auth_token;


//--Params for the automatics---------
int auto_switch_off_hour = 23;  //  switch off the automatics
int auto_switch_on_hour  = 7;   //  switch on the automatics

float temp_upper_limit = 21.4;   // when to switch off the heater
float temp_lower_limit = 20.4;   // when to switch on the heater


bool ac_on = false;          // the heater status
bool ac_automatics = false;


void acOn() {
  Serial.println(F("On"));
  sendBuffer[5] = 0x20;
  sendBuffer[17] = 0x36;
  if (temperatures.outside > -13) {
    pending = true; // schedule for sending
    ac_on = true;
  }
}

void acOff() {
  Serial.println(F("Off"));
  sendBuffer[5] = 0x00;
  sendBuffer[17] = 0x16;
  pending = true; // schedule for sending
  ac_on = false;
}

// functions to be called when an alarm triggers:
void handleAlarmOn(){
  Serial.println(F("Alarm: - turn automatics on"));   
  ac_automatics = true;
}

void handleAlarmOff(){
  Serial.println(F("Alarm: - turn automatics off"));   
  ac_automatics = false;
  if (ac_on) {
    acOff();
  }
}

void setupAlarms() {
  // create the alarms 
  Alarm.alarmRepeat(auto_switch_on_hour,00,0, handleAlarmOn);  // 7:00am every day
  Alarm.alarmRepeat(auto_switch_off_hour,00,0,handleAlarmOff);  // 3:00pm every day 
}  

bool isAutomaticsOn() {
  return (hour() >= auto_switch_on_hour) && (hour() < auto_switch_off_hour);
}

void connection(String data) {
  String id;
  int err = 0;
  String auth_resp = "";
  Serial.println(F("Pusher connection established"));
  id = PusherClient::parseMessageMember("socket_id", data);
//  id.toCharArray(socket_id, sizeof(socket_id));
  Serial.println(id);
  String query = "/pusher/auth?socket_id="+id+"&channel_name="+presence_channel+"&user_id=arduino";
  char buf[256];
  query.toCharArray(buf, 256);
  err = http.get("192.168.1.45",5000, buf);
  if (err == 0) {
    Serial.println("startedRequest ok");
    

    err = http.responseStatusCode();
    if (err >= 0) {
      Serial.print("Got status code: ");
      Serial.println(err);

      // Usually you'd check that the response code is 200 or a
      // similar "success" code (200-299) before carrying on,
      // but we'll print out whatever response we get

      err = http.skipResponseHeaders();
      if (err >= 0) {
        int bodyLen = http.contentLength();    
        http.setTimeout(kNetworkTimeout);
        int i = http.readBytes(buf, bodyLen);
        auth_token = PusherClient::parseMessageMember("auth", String(buf));
        client.subscribe(presence_channel, auth_token, "arduino"); 
        Serial.println(auth_token);
      }
      else
      {
        Serial.print("Failed to skip response headers: ");
        Serial.println(err);
      }
    }
    else
    {    
      Serial.print("Getting response failed: ");
      Serial.println(err);
    }
  }
  else
  {
    Serial.print("Connect failed: ");
    Serial.println(err);
  }
  http.stop();
  
  Serial.println(auth_resp);
}


void sendTempToPusher(String tempStr) {
  String data = "{\"temp\":\""+ tempStr + "\"}";
  client.triggerEvent("client-temp", data , presence_channel);  
  
}

String toString(float val) {
  char buf[10];
  dtostrf(val,1,2, buf);
  return String(buf);
}

void newWebClient(String data) {
  sendTempToPusher(toString(temperatures.inside));
}

void pusherOn(String data) {
  Serial.println("On: " + data);
  acOn();
}

void pusherOff(String data) {
  Serial.println("Off: " + data);
  acOff();
}

void ping(String data) {
  client.triggerEvent("pusher:pong", "{}");
}

void check_temp() {
  if (ac_on) {
    if(temperatures.inside > temp_upper_limit) {
      Serial.println(F("Temperature inside raised: switching OFF"));
      acOff();
    }
  } else {  
    if(temperatures.inside < temp_lower_limit) {
      Serial.println(F("Temperature inside dropped: switching ON"));
      acOn();
    }
  }
}


void setup () {
  Serial.begin(57600);
  Serial.println(F("Starting up..."));
  
#if !defined(__AVR_ATmega2560__) && !defined(__AVR_ATmega1280__)
  rf12_set_cs(8);
#endif

  rf12_initialize(25, RF12_868MHZ, 210);
//    rf12_initialize(25, RF12_433MHZ, 210);

  Ethernet.begin(mac, own_ip);
  
  client.bind("pusher:connection_established", connection);
  if(client.connect(API_KEY)) {
    client.bind("on", pusherOn);
    client.bind("off", pusherOff);
    client.bind("pusher:ping", ping);
    client.subscribe(channel);
    client.bind("pusher_internal:member_added", newWebClient);
  } else {
    Serial.println(F("Pusher subscribe failed"));
  }

  // setup RTC
  setSyncProvider(RTC.get);   // the function to get the time from the RTC
  if(timeStatus()!= timeSet) {
     Serial.println(F("Unable to sync with the RTC"));
  } else { 
     Serial.println(F("RTC has set the system time"));
  }
  
  setupAlarms();
  ac_automatics = isAutomaticsOn();
  Serial.print(F("Automatics starts at: "));
  Serial.print(auto_switch_on_hour);
  Serial.println(F(":00"));
  
  Serial.print(F("Automatics stops at: "));
  Serial.print(auto_switch_off_hour);
  Serial.println(F(":00"));
  
  Serial.print(F("Current time: "));
  Serial.print(hour());
  Serial.print(F(":"));
  Serial.println(minute());
  
  Serial.print(F("Automatics is "));
  if (ac_automatics) {
    Serial.println(F("ON"));
  } else { 
    Serial.println(F("OFF"));
  }
    
  Serial.println(F("Ready"));
}

void loop () {
  Alarm.delay(0);
  // Pusher monitoring
  if (client.connected()) {
    client.monitor();
  }

  if (rf12_recvDone() && rf12_crc == 0) {
    int node_id = (rf12_hdr & 0x1F);
    Serial.print("Packet received from: ");
    Serial.println(node_id);
    
    if (node_id == 30)  {
      // temperatures
      temperatures=*(TempTX*) rf12_data;    
      Serial.println("Temperatures:");
      Serial.println(temperatures.inside);
      Serial.println(temperatures.outside);
      if (ac_automatics) {
        check_temp();
      }
    }
    
    if (node_id == 26) { // IR bridge
//    if (RF12_WANTS_ACK) {
      float temp = *(float*)rf12_data;
      if (temp != temperatures.inside) {
        temperatures.inside = temp;
        Serial.println(temperatures.inside);
        sendTempToPusher(toString(temperatures.inside));
        if (ac_automatics) {
          check_temp();
        }
      }

      if (pending) { // if we have data to send -> send it
        rf12_sendStart(RF12_ACK_REPLY, sendBuffer, sizeof sendBuffer);
        pending = false;  
      } else { // othewise just send and empty ACK to put the receiver to sleep        
        payload.later = (byte) millis() - payload.start;
        rf12_sendStart(RF12_ACK_REPLY, &payload, 0);
      }
    }
  }

}


