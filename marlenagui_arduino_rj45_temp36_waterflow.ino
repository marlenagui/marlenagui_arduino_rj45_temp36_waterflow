/*
  Pachube sensor client
 
 This sketch connects an analog sensor to Pachube (http://www.pachube.com)
 using a Wiznet Ethernet shield. You can use the Arduino Ethernet shield, or
 the Adafruit Ethernet shield, either one will work, as long as it's got
 a Wiznet Ethernet module on board.
 
 Circuit:
 * Temperature Analog sensor attached to analog in 2
 * Water Flow digital Sensor attached to pin Din 2
 * Ethernet shield attached to pins 10, 11, 12, 13
 
 created 15 March 2010
 updated 26 Oct 2011
 by Tom Igoe
 
 http://www.tigoe.net/pcomp/code/category/arduinowiring/873
 This code is in the public domain.
 
 */

#include <SPI.h>
#include <Ethernet.h>

// assign a MAC address for the ethernet controller.
// Newer Ethernet shields have a MAC address printed on a sticker on the shield
// fill in your address here:
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};

// fill in an available IP address on your network here,
// for manual configuration:
IPAddress ip(192,168,0,120);
// initialize the library instance:
EthernetClient client;

int temperaturepin= 2;
byte WaterFlowInterrupt = 0;
int WaterFlow = 2;
int noAvg = 0;

long lastConnectionTime = 0;        // last time you connected to the server, in milliseconds
long lastReadingTime = 0;           // last time you read
long lastReadWaterFlow = 0;                   // last time you read the water flow
boolean lastConnected = false;      // state of the connection last time through the main loop
const int postingInterval = 10000;  // delay between updates to Pachube.com
const int readingInterval = 10000;  // delay before reading again
float AvgTemp = 0;                  //To average the mesured temp, will remove picks

// The hall-effect flow sensor outputs approximately 7.5 pulses per second per litre/minute of flow.
float calibrationFactor = 7.5;
volatile byte pulseCount;
float flowRate;

void setup() {
  //************************************************
  // start serial port:
  Serial.begin(57600);
  //************************************************
  // start the Ethernet connection:
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    // no point in carrying on, so do nothing forevermore:
    for(;;)
      ;
  }
  // give the ethernet module time to boot up:
  delay(1000);
  // start the Ethernet connection:
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    // Configure manually:
    Ethernet.begin(mac, ip);
  }
  
  //************************************************
  //Set the Water flow variables
  // The Hall-effect sensor is connected to pin 2 which uses interrupt 0.
  // Configured to trigger on a RAISING state change 
  pinMode(WaterFlow, INPUT);        //initializes digital pin 2 as an input
  pulseCount = 0;
  attachInterrupt(WaterFlowInterrupt, pulseCounter, RAISING);
}

void loop() {
//****************************************************************************************************************************************    
// read the Water flow sensor once every second
//****************************************************************************************************************************************
  if ( millis() - lastReadWaterFlow > 1000 ) {      // Only process the counter once per seconde
    detachInterrupt(WaterFlowInterrupt);            // Disable the interrupt while calculating flow rate and sending the value to the host
    // Because this loop may not complete in exactly 1 second intervals we calculate the number of milliseconds that have passed since the last execution and use
    // that to scale the output. the calibrationFactor to scale the output based on the number of pulses per second per units of measure (litres/minute in
    // this case) coming from the sensor is applied on the web side.
    Serial.print("Pulse since last time:"); // Output separator
    Serial.println(pulsecount);
    pulseCount = ((1000.0 / (millis() - lastReadWaterFlow)) * pulseCount) 
    Serial.print("Pulse per seconde: ");
    Serial.println(pulseCount);
    
    // Enable the interrupt again now that we've finished sending output
    attachInterrupt(WaterFlowInterrupt, pulseCounter, FALLING);
  }

//****************************************************************************************************************************************    
// read the Temp analog sensor once every readingInterval
//****************************************************************************************************************************************
  if ( millis() - lastReadingTime > readingInterval) {
    float temperature = getvoltage(temperaturepin);
    temperature = (temperature - .5) * 100;
    //following if avoid the AvgTemp value to start from tempature / 5 will work only at first loop of by any chance temperature = 0
    if ( AvgTemp == 0 ) {
      Serial.println("Set AvgTemp to temperature");
      AvgTemp = temperature;
    }
    else {
      AvgTemp = (4 * AvgTemp + temperature) / 5;
    }
    Serial.print ("\nSensor Reading Value : ");
    Serial.println (temperature);
    Serial.print ("Avr Value : ");
    Serial.println (AvgTemp);
    lastReadingTime = millis();
  }

  // if there's incoming data from the net connection.
  // send it out the serial port.  This is for debugging
  // purposes only:
  //if (client.available()) {
  //  char c = client.read();
  //  Serial.print(c);
  //}

  // if there's no net connection, but there was one last time
  // through the loop, then stop the client:
  if (!client.connected() && lastConnected) {
    Serial.println();
    Serial.println("disconnecting.");
    client.stop();
  }

  // if you're not connected, and ten seconds have passed since
  // your last connection, then connect again and send data:
  long time=millis();
  if(!client.connected() && (millis() - lastConnectionTime > postingInterval)) {
    sendDataCosm(AvgTemp);
    sendDataEmoncms(AvgTemp);
    sendDataEmoncms(pulseCount);
    // Reset the pulse counter so we can start incrementing again
    pulseCount = 0;
    // note the time that the connection was made: 
    lastConnectionTime = millis();
  }
  // store the state of the connection for next time through
  // the loop:
  lastConnected = client.connected();
}

//****************************************************************************************************************************************    
// this method makes a HTTP connection to the server cosm:
void sendDataCosm(float thisData) {
  // if there's a successful connection:
  if (client.connect("www.cosm.com", 80)) {
    Serial.println("connecting to Cosm...");
    // send the HTTP PUT request. 
    // fill in your feed address here:
    client.print("PUT /api/38973.csv HTTP/1.1\n");
    client.print("Host: www.cosm.com\n");
    // fill in your Pachube API key here:
    client.print("X-PachubeApiKey: -7w6tS_ZpHnFa-zNMIoDdAxZ8rX00EW16KjrMD18rwE\n");
    client.print("Content-Length: ");

    // calculate the length of the sensor reading in bytes:
    int thisLength = getLength(thisData);
    client.println(thisLength, DEC);

    // last pieces of the HTTP PUT request:
    client.print("Content-Type: text/csv\n");
    client.println("Connection: close\n");

    // here's the actual content of the PUT request:
    client.println(thisData, DEC);
    Serial.println("disconnecting from Cosm.");
    client.stop();
    
  } 
  else {
    // if you couldn't make a connection:
    Serial.println("Connection failed");
  }
}

//****************************************************************************************************************************************    
// this method makes a HTTP connection to the server marlenagui:
void sendDataEmoncms(float thisData) {
  // if there's a successful connection:
  if (client.connect("www.marlenagui.com", 80)) {
    Serial.println("connected to Marlenagui...");
    // send the HTTP PUT request. 
    client.print("PUT /Energies_Monitor/api/post?apikey=bc6e7c688d881750b70130f78308a546&json={TempHeatingOut:");
    client.print(thisData);
    client.println("} HTTP/1.0");
    client.println("Host: www.marlenagui.com");
    client.println();
    Serial.println("disconnecting from Marlenagui.");
    client.stop();
  } 
  else {
    // if you couldn't make a connection:
    Serial.println("Connection failed");
  }
}

//****************************************************************************************************************************************    
// This method calculates the number of digits in the sensor reading.  Since each digit of the ASCII decimal
// representation is a byte, the number of digits equals the number of bytes:
int getLength(int someValue) {
  // there's at least one byte:
  int digits = 1;
  // continually divide the value by ten, 
  // adding one to the digit count for each
  // time you divide, until you're at 0:
  int dividend = someValue /10;
  while (dividend > 0) {
    dividend = dividend /10;
    digits++;
  }
  // return the number of digits:
  return digits;
}

//****************************************************************************************************************************************    
// Invoked by interrupt0 once per rotation of the hall-effect sensor. Interrupt
// handlers should be kept as small as possible so they return quickly.
void pulseCounter()
{
  // Increment the pulse counter
  pulseCount++;
}
//****************************************************************************************************************************************    
// Read the temp sensor analog ouput
float getvoltage(int pin){
  return (analogRead(pin)* .004882814);
}
