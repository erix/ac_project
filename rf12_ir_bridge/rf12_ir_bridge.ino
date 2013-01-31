#include <JeeLib.h>
#include <avr/sleep.h>

#include <OneWire.h>
#include <DallasTemperature.h>

// Data wire is plugged into pin 2 on the Arduino
#define ONE_WIRE_BUS 5

#define SYSCLOCK 16000000

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

MilliTimer recvTimer;
bool ack_done = false;

ISR(WDT_vect) { Sleepy::watchdogEvent(); }


void setup_ir() {
  pinMode(3, OUTPUT);
  digitalWrite(3, LOW); // When not sending PWM, we want it low

  // WGM2 = 101: phase-correct PWM with OCRA as top
  // CS2 = 000: no prescaling
  TCCR2A = _BV(WGM20);
  TCCR2B = _BV(WGM22) | _BV(CS20);

  // frequency and duty cycle
  int khz = 38;
  OCR2A = SYSCLOCK / 2 / khz / 1000;
  OCR2B = OCR2A / 3; // 33% duty cycle
  
  sensors.begin(); // IC Default 9 bit. If you have troubles consider upping it 12. Ups the delay giving the IC more time to process the temperature measurement
}  
  
inline void mark(int time) {
  TCCR2A |= _BV(COM2B1); // Enable pin 3 PWM output
  delayMicroseconds(time);
}

inline void space(int time) {
  TCCR2A &= ~(_BV(COM2B1)); // Disable pin 3 PWM output
  delayMicroseconds(time);
}

void sendbyte(unsigned char b) {
  int i;
  for (i = 0; i < 8; i++) {
    if (b & 1) {
      // one
      mark (440);
      space(1300);
    } else {
      // zero
      mark (440);
      space(440);
    }
    b >>= 1;
  }
}


void send_ir_command(byte *command, byte length) {
  // header
  mark (3500);
  space(1700);

  for (byte i = 0; i < length; i++) {
    sendbyte(command[i]);
  }

   // stop
  mark (440);
  space(0); 
}


void setup () {
  rf12_initialize(26, RF12_868MHZ, 210);
  setup_ir();
}

void loop () {
  if (rf12_recvDone() && rf12_crc == 0) {
    ack_done = true;
    send_ir_command((byte *)rf12_data, rf12_len);
    delay(2);
    send_ir_command((byte *)rf12_data, rf12_len); 
  }
  
  if (recvTimer.poll(100) || ack_done) {
    // power down for 2 seconds (multiple of 16 ms)
    rf12_sleep(RF12_SLEEP);
    Sleepy::loseSomeTime(2000);
    rf12_sleep(RF12_WAKEUP);
    
    // read the temperature after wake-up
    sensors.requestTemperatures(); // Send the command to get temperatures
    float temp = sensors.getTempCByIndex(0);
    
    rf12_sendStart(RF12_HDR_ACK, &temp, sizeof(float));
    ack_done = false;
    recvTimer.set(0);
  }
}
