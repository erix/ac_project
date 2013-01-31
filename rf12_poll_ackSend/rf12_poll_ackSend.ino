#include <JeeLib.h>
#include <SPI.h>
#include <Ethernet.h>
#include <PusherClient.h>
//#include <HttpClient.h>

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

PusherClient client;
MilliTimer sendTimer;
bool pending = false;
byte sendBuffer[18] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x00, 0x08, 0x0A, 0x30, 0x54, 0x57, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00};

bool ac_on = true;

struct {
  byte start;   // time at which we started listening for a packet
  byte later;   // how long we had to wait for packet to come in
} payload;

AlarmID_t alarm1, alarm2;

// Number of milliseconds to wait without receiving any data before we give up
const int kNetworkTimeout = 30*1000;
// Number of milliseconds to wait if no data is available before trying again
const int kNetworkDelay = 1000;
  
//EthernetClient c;
//HttpClient http(c);
//String auth_token;

void setupAlarms() {
  // create the alarms 
  alarm1 = Alarm.alarmRepeat(7,00,0, handleAlarm);  // 7:00am every day
  alarm2 = Alarm.alarmRepeat(15,00,0,handleAlarm);  // 3:00pm every day 
}  

// functions to be called when an alarm triggers:
void handleAlarm(){
  Serial.println(F("Alarm: - turn AC on"));   
  on("");
}

void setup () {
  Serial.begin(57600);
  Serial.println(F("Starting up..."));
  rf12_set_cs(8);
  rf12_initialize(25, RF12_868MHZ, 210);
//    rf12_initialize(25, RF12_433MHZ, 210);

  Ethernet.begin(mac, own_ip);
  //client.bind("pusher:connection_established", connection);
  if(client.connect(API_KEY)) {
    client.bind("on", on);
    client.bind("off", off);
    client.subscribe(channel);
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
  
  Serial.println(F("Ready"));
}

void on(String data) {
  Serial.println("On: " + data);
  sendBuffer[5] = 0x20;
  sendBuffer[17] = 0x36;
  if (temperatures.outside > -13) {
    pending = true; // schedule for sending
    ac_on = true;
  }
}

void off(String data) {
  Serial.println("Off: " + data);
  sendBuffer[5] = 0x00;
  sendBuffer[17] = 0x16;
  pending = true; // schedule for sending
  ac_on = false;

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
      check_temp();
    }
    
    if (node_id == 26) { // IR bridge
//    if (RF12_WANTS_ACK) {
      temperatures.inside = *(float*)rf12_data;
      Serial.println(temperatures.inside);
      check_temp();

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

void check_temp() {
  if (isOff()) {
    if (temperatures.outside > -13) {
      if(temperatures.inside < 20.3) {
        Serial.println(F("Temperature inside dropped: switching ON"));
        if (isOff()) on("");
      }
    }
  } else {
  
    if(temperatures.inside > 21.5) {
      Serial.println(F("Temperature inside raised: switching OFF"));
      off("");
    }
  }
}

bool isOff() {
  return !ac_on;
}

/*
void connection(String data) {
  String id;
  int err = 0;
  String auth_resp = "";
  Serial.println(F("Pusher connection established"));
  id = PusherClient::parseMessageMember("socket_id", data);
//  id.toCharArray(socket_id, sizeof(socket_id));
  Serial.println(id);
  String query = "/pusher/auth?socket_id=" + id;
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
*/
