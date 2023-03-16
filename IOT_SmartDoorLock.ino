#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "config.h"
#include <ESP_Mail_Client.h>


//Init WifiClient for connecting to Wifi and MQTT
WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;

// Init devices
#define DOOR_SENSOR_PIN 16
#define DOOR_LOCK_PIN 17

// input and output state for doorSensor
bool current = false;
bool last = false;

// set the LCD number of columns and rows
int lcdColumns = 16;
int lcdRows = 2;

// set LCD address, number of columns and rows
// if you don't know your display address, run an I2C scanner sketch
LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);  

// set up the 'doorsensor' feed
AdafruitIO_Feed* doorsensor = io.feed("DoorSensor");

// set up the 'doorlock' feed
AdafruitIO_Feed* doorlock = io.feed("DoorLock");

/* The SMTP Session object used for Email sending */
SMTPSession smtp;
/* Declare the message class */
SMTP_Message message;
/* Declare the session config data */
ESP_Mail_Session session;

//Unsigned longs used for email reminder system
//Change "wait" variable for preferred email reminder
unsigned long wait = 10000;
unsigned long doorOpen = 0;

//Boolean for only sending email once
bool needToDoIt = true;

/* Callback function to get the Email sending status */
void smtpCallback(SMTP_Status status);

void setup() {

  // initialize LCD
  lcd.init();                    
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("SmartDoorLock");
  
  // set pins as input pullup and output
  pinMode(DOOR_SENSOR_PIN, INPUT_PULLUP);
  pinMode(DOOR_LOCK_PIN, OUTPUT);

  // start the serial connection
  Serial.begin(9600);

  //setup_wifi method for wifi connection
  setup_wifi();

  //Setting mqtt server with variable from config.h 
  client.setServer(MQTT_SERVER, 1883);

  // connect to io.adafruit.com
  Serial.print("Connecting to Adafruit IO");
  io.connect();

  // set up a message handler for the 'digital' feed.
  doorlock->onMessage(handleMessage);

  // wait for a connection
  while (io.status() < AIO_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  // we are connected
  Serial.println();
  Serial.println(io.statusText());
  doorlock->get();

  //Setup email system using SMTP client
  setupEmail();
}

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  //Print successful connection and Ip address
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266Client", MQTT_KEY, MQTT_KEY)) {
      Serial.println("connected");
      // Subscribe
      client.subscribe("esp32/output");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void loop() {
  
  //Messages for our MQTT publish (doorSensor)
  char* doorSensorOpen = "Door is now open";
  char* doorSensorClosed = "Door is now closed";

  // io.run() keeps the client connected to
  // io.adafruit.com, and processes any incoming data.
  io.run();

  //If statement for reconnecting if connnection is lost
  if (!client.connected()) {
    reconnect();
  }

  //Making sure our client is also looping
  client.loop();

  //Millis variable for keeping track of time
  long now = millis();

  //If statement for checking latest change
  //If door is opened or closed, it reads the state every 1.25 second
  if (now - lastMsg > 1250) {
    lastMsg = now;

    // Taking the current state of the doorSensor.
    if (digitalRead(DOOR_SENSOR_PIN) == HIGH) {
      current = true;
    } else {
      current = false;
    }

    // Return if the value hasn't changed
    if (current == last)
      return;

    // If statement for when the door is open
    if (current == 1) {
      needToDoIt = true;
      doorOpen = millis();
      client.publish("esp32/doorsensor", doorSensorOpen);
      displayLowerLcd("Unlocked");
    }

    // If statement for when the door is closed
    if (current == 0) {
      client.publish("esp32/doorsensor", doorSensorClosed);
      digitalWrite(DOOR_LOCK_PIN, LOW);
      doorlock->get();
      doorlock->save(LOW);
      displayLowerLcd("Locked");
    }

    // save the current state to the 'digital' feed on adafruit io
    Serial.print("sending button -> ");
    Serial.println(current);
    doorsensor->save(current);

    // store last sensor state
    last = current;
    delay(250);
  }

  //If statement for checking how long door has been opened
  //When door is left open for a specific time, send email via SMTP
  if(current == 1 && doorOpen >0){
    unsigned long difference = millis()-doorOpen;
    if(difference >=wait && needToDoIt == true){
      sendEmail();
      needToDoIt = false;
    }
  }
}


// Function is called whenever a 'digital' feed message is received from Adafruit IO. 
void handleMessage(AdafruitIO_Data* data) {
  char* doorUnlocked = "Door is now unlocked";
  char* doorLocked = "Door is now locked";

  Serial.print("received <- ");

  if (data->toPinLevel() == HIGH) {
    Serial.println("HIGH");
    client.publish("esp32/doorlock", doorUnlocked);
      displayLowerLcd("Unlocked");
  } else {
    Serial.println("LOW");
    client.publish("esp32/doorlock", doorLocked);
  }

  digitalWrite(DOOR_LOCK_PIN, data->toPinLevel());
}

// Function for displaying to LCD screen
// I2C library
void displayLowerLcd(String status){
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("SmartDoorLock");
  lcd.setCursor(0,1);
  lcd.print("Door : " + status);
}

// Function to setup email when ESP32 is reset or new code is uploaded
void setupEmail(){

  /** Enable the debug via Serial port
   * none debug or 0
   * basic debug or 1
  */
  smtp.debug(1);

  // Set the callback function to get the sending results 
  smtp.callback(smtpCallback);

  // Set the session config 
  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;
  session.login.email = AUTHOR_EMAIL;
  session.login.password = AUTHOR_PASSWORD;
  session.login.user_domain = "";

  // Set the message headers
  message.sender.name = "SmartDoorLock - ESP32";
  message.sender.email = AUTHOR_EMAIL;
  message.subject = "DoorStatus - Spangsbjerg Kirkevej 42";
  message.addRecipient("Bruger", RECIPIENT_EMAIL);

  //Send HTML message
  String htmlMsg = "<div style=\"color:#2f4468;\"><h1>Door has now been unlocked</h1><p> Smart door has now been unlocked and opened at Spangsbjerg Kirkevej 42.</p><h3>- Email sent from SmartDoorLock </h3></div>";
  message.html.content = htmlMsg.c_str();
  message.html.content = htmlMsg.c_str();
  message.text.charSet = "us-ascii";
  message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  
  // Connect to server with the session config 
  if (!smtp.connect(&session))
    return; 
}

// Function for sending email.
// Starts sending email and closes session after
void sendEmail(){
  if (!MailClient.sendMail(&smtp, &message))
    Serial.println("Error sending Email, " + smtp.errorReason());
}

// Callback function to get the Email sending status 
void smtpCallback(SMTP_Status status){

  // Print the current status 
  Serial.println(status.info());

  // Print the sending result 
  if (status.success()){
    Serial.println("----------------");
    ESP_MAIL_PRINTF("Message sent success: %d\n", status.completedCount());
    ESP_MAIL_PRINTF("Message sent failled: %d\n", status.failedCount());
    Serial.println("----------------\n");
    struct tm dt;

    for (size_t i = 0; i < smtp.sendingResult.size(); i++){
      // Get the result item 
      SMTP_Result result = smtp.sendingResult.getItem(i);
      time_t ts = (time_t)result.timestamp;
      localtime_r(&ts, &dt);

      ESP_MAIL_PRINTF("Message No: %d\n", i + 1);
      ESP_MAIL_PRINTF("Status: %s\n", result.completed ? "success" : "failed");
      ESP_MAIL_PRINTF("Date/Time: %d/%d/%d %d:%d:%d\n", dt.tm_year + 1900, dt.tm_mon + 1, dt.tm_mday, dt.tm_hour, dt.tm_min, dt.tm_sec);
      ESP_MAIL_PRINTF("Recipient: %s\n", result.recipients.c_str());
      ESP_MAIL_PRINTF("Subject: %s\n", result.subject.c_str());
    }
    Serial.println("----------------\n");
  }
}
