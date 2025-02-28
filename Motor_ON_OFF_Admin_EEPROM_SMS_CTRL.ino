#include <SoftwareSerial.h>
#include <EEPROM.h>

// ---------------------------------------------------------------------------
// CONFIGURATION
// ---------------------------------------------------------------------------
const int GSM_RX_PIN = 9;    // Arduino RX (connect to GSM TX)
const int GSM_TX_PIN = 10;    // Arduino TX (connect to GSM RX)
const int MOTOR_PIN  = 13;   // Use pin 13 for the motor

// Admin number in the exact format the GSM module provides
String adminNumber = "+918543053029";  

// EEPROM addresses
const int EEPROM_MOTOR_STATE_ADDR = 0;     // Store motor state at address 0
const int EEPROM_USER_START_ADDR  = 1;     // Secondary user number starts at 1
const int EEPROM_USER_MAX_LEN     = 20;    // Up to 20 chars for phone number

// ---------------------------------------------------------------------------
// GLOBALS
// ---------------------------------------------------------------------------
SoftwareSerial gsmSerial(GSM_RX_PIN, GSM_TX_PIN);

bool motorState = false;            
String secondaryUserNumber = "";    

// ---------------------------------------------------------------------------
// FUNCTION DECLARATIONS
// ---------------------------------------------------------------------------
void initGSMModule();
void checkForIncomingData();
void handleIncomingCall(String callerNumber);
void handleIncomingSMS(int smsIndex);
void processSMSCommand(String senderNumber, String smsContent);

void toggleMotor();
void setMotor(bool state);
void sendSMS(String number, String message);

String readPhoneNumberFromEEPROM();
void storePhoneNumberInEEPROM(String phoneNumber);
void loadMotorStateFromEEPROM();
void storeMotorStateToEEPROM(bool state);

void sendStartupSMS();

// ---------------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(9600);        
  gsmSerial.begin(9600);

  pinMode(MOTOR_PIN, OUTPUT);

  // Load motor state from EEPROM
  loadMotorStateFromEEPROM();
  digitalWrite(MOTOR_PIN, motorState ? HIGH : LOW);

  // Load secondary user phone number from EEPROM
  secondaryUserNumber = readPhoneNumberFromEEPROM();
  Serial.print("After reading EEPROM, secondaryUserNumber = '");
  Serial.print(secondaryUserNumber);
  Serial.println("'");

  // Initialize GSM
  initGSMModule();

  Serial.println("System initialized.");
  Serial.print("Admin number: ");
  Serial.println(adminNumber);
  Serial.print("Secondary user number: ");
  Serial.println(secondaryUserNumber);
  Serial.print("MOTOR initial state: ");
  Serial.println(motorState ? "ON" : "OFF");

  // Give module time to register on the network
  delay(5000);

  // Send startup SMS
  sendStartupSMS();
}

// ---------------------------------------------------------------------------
// LOOP
// ---------------------------------------------------------------------------
void loop() {
  checkForIncomingData();
}

// ---------------------------------------------------------------------------
// Initialize the GSM module with basic commands
// ---------------------------------------------------------------------------
void initGSMModule() {
  delay(1000);
  gsmSerial.println("AT");         
  delay(1000);

  gsmSerial.println("AT+CLIP=1");  
  delay(1000);

  gsmSerial.println("AT+CMGF=1");  
  delay(1000);

  // Optional: Clear all old SMS
  // gsmSerial.println("AT+CMGD=1,4");
  // delay(1000);

  delay(1000);
}

// ---------------------------------------------------------------------------
// Continuously read lines from the GSM module, looking for calls or SMS
// ---------------------------------------------------------------------------
void checkForIncomingData() {
  while (gsmSerial.available()) {
    String line = gsmSerial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      Serial.println("GSM >> " + line);
    }

    if (line.startsWith("RING")) {
      // Wait for +CLIP
    }
    else if (line.startsWith("+CLIP:")) {
      int startQuote = line.indexOf('"');
      int endQuote   = line.indexOf('"', startQuote + 1);
      if (startQuote >= 0 && endQuote > startQuote) {
        String callerNumber = line.substring(startQuote + 1, endQuote);
        handleIncomingCall(callerNumber);
      }
    }
    else if (line.startsWith("+CMTI:")) {
      int commaIndex = line.indexOf(',');
      if (commaIndex > 0) {
        int smsIndex = line.substring(commaIndex + 1).toInt();
        handleIncomingSMS(smsIndex);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Handle an incoming call
// ---------------------------------------------------------------------------
void handleIncomingCall(String callerNumber) {
  Serial.println("Incoming call from: " + callerNumber);

  if (callerNumber == adminNumber || callerNumber == secondaryUserNumber) {
    toggleMotor();
    String feedback = "MOTOR is now " + String(motorState ? "ON" : "OFF");
    sendSMS(callerNumber, feedback);
    gsmSerial.println("ATH"); // Hang up
  } else {
    Serial.println("Caller not authorized.");
    gsmSerial.println("ATH");
  }
}

// ---------------------------------------------------------------------------
// Handle an incoming SMS at a given index
// ---------------------------------------------------------------------------
void handleIncomingSMS(int smsIndex) {
  gsmSerial.print("AT+CMGR=");
  gsmSerial.println(smsIndex);

  String senderNumber = "";
  String smsContent   = "";

  String allLines = "";
  unsigned long startTime = millis();
  const unsigned long TIMEOUT_MS = 10000; 
  bool finishedReading = false;

  while (!finishedReading && (millis() - startTime < TIMEOUT_MS)) {
    while (gsmSerial.available()) {
      String line = gsmSerial.readStringUntil('\n');
      line.trim();

      if (line.length() > 0) {
        Serial.println("SMS >> " + line);
      }
      allLines += line + "\n";

      if (line.startsWith("+CMGR:")) {
        int firstQuote = line.indexOf('"');
        for (int i = 0; i < 2; i++) {
          firstQuote = line.indexOf('"', firstQuote + 1);
        }
        int secondQuote = line.indexOf('"', firstQuote + 1);
        if (firstQuote >= 0 && secondQuote > firstQuote) {
          senderNumber = line.substring(firstQuote + 1, secondQuote);
        }
      }
      else if (line.startsWith("OK")) {
        finishedReading = true;
        break;
      }
      else if (line.startsWith("ERROR")) {
        finishedReading = true;
        break;
      }
    }
  }

  int cmgrIndex = allLines.indexOf("+CMGR:");
  int okIndex   = allLines.indexOf("\nOK");

  if (cmgrIndex != -1 && okIndex != -1 && okIndex > cmgrIndex) {
    String possibleBody = allLines.substring(cmgrIndex, okIndex);
    int nextLineBreak   = possibleBody.indexOf('\n');
    if (nextLineBreak != -1) {
      smsContent = possibleBody.substring(nextLineBreak);
      smsContent.trim();
    }
  }

  Serial.println("Parsed Sender: " + senderNumber);
  Serial.println("Parsed SMS content: " + smsContent);

  gsmSerial.print("AT+CMGD=");
  gsmSerial.println(smsIndex);
  delay(500);

  if (senderNumber.length() > 0 && smsContent.length() > 0) {
    processSMSCommand(senderNumber, smsContent);
  }
}

// ---------------------------------------------------------------------------
// Parse the SMS command
// ---------------------------------------------------------------------------
void processSMSCommand(String senderNumber, String smsContent) {
  Serial.println("SMS from: " + senderNumber + " => " + smsContent);

  String command = smsContent;
  command.toUpperCase();

  bool isAdmin = (senderNumber == adminNumber);
  bool isAuthorized = isAdmin || (senderNumber == secondaryUserNumber);

  if (!isAuthorized) {
    Serial.println("Sender not authorized. Ignoring SMS.");
    return;
  }

  // Admin-only: SETUSER
  if (isAdmin && command.startsWith("SETUSER")) {
    int spaceIndex = smsContent.indexOf(' ');
    if (spaceIndex > 0) {
      String newUser = smsContent.substring(spaceIndex + 1);
      newUser.trim();
      storePhoneNumberInEEPROM(newUser);
      secondaryUserNumber = newUser;
      Serial.println("Secondary user is now '" + secondaryUserNumber + "'");
      sendSMS(senderNumber, "Secondary user updated to: " + newUser);
    } else {
      sendSMS(senderNumber, "Error: Missing phone number. Use: SETUSER <number>");
    }
    return;
  }

  // Other commands
  if (command == "ON") {
    setMotor(true);
    sendSMS(senderNumber, "MOTOR is ON");
  }
  else if (command == "OFF") {
    setMotor(false);
    sendSMS(senderNumber, "MOTOR is OFF");
  }
  else if (command == "TOGGLE") {
    toggleMotor();
    sendSMS(senderNumber, String("MOTOR is now ") + (motorState ? "ON" : "OFF"));
  }
  else if (command == "STATUS") {
    String reply = "MOTOR is " + String(motorState ? "ON" : "OFF");
    sendSMS(senderNumber, reply);
  }
  else {
    sendSMS(senderNumber, "Unrecognized command. Use ON, OFF, TOGGLE, STATUS, or SETUSER (admin only).");
  }
}

// ---------------------------------------------------------------------------
// Toggle the motor
// ---------------------------------------------------------------------------
void toggleMotor() {
  motorState = !motorState;
  digitalWrite(MOTOR_PIN, motorState ? HIGH : LOW);
  storeMotorStateToEEPROM(motorState);
  Serial.println("MOTOR toggled -> " + String(motorState ? "ON" : "OFF"));
}

// ---------------------------------------------------------------------------
// Explicitly set motor ON/OFF
// ---------------------------------------------------------------------------
void setMotor(bool state) {
  motorState = state;
  digitalWrite(MOTOR_PIN, state ? HIGH : LOW);
  storeMotorStateToEEPROM(state);
  Serial.println("MOTOR set -> " + String(motorState ? "ON" : "OFF"));
}

// ---------------------------------------------------------------------------
// Send an SMS
// ---------------------------------------------------------------------------
void sendSMS(String number, String message) {
  Serial.println("Sending SMS to " + number + ": " + message);
  gsmSerial.print("AT+CMGS=\"");
  gsmSerial.print(number);
  gsmSerial.println("\"");
  delay(1000);  // longer delay for reliability
  gsmSerial.print(message);
  delay(500);
  gsmSerial.write(26);  // Ctrl+Z
  delay(2000);  // wait a bit for sending
}

// ---------------------------------------------------------------------------
// Read the secondary user phone number from EEPROM
// ---------------------------------------------------------------------------
String readPhoneNumberFromEEPROM() {
  String phone = "";
  for (int i = 0; i < EEPROM_USER_MAX_LEN; i++) {
    char c = (char)EEPROM.read(EEPROM_USER_START_ADDR + i);
    if (c == '\0' || c == 0xFF) {
      break;
    }
    phone += c;
  }
  return phone;
}

// ---------------------------------------------------------------------------
// Store the secondary user phone number into EEPROM
// ---------------------------------------------------------------------------
void storePhoneNumberInEEPROM(String phoneNumber) {
  if (phoneNumber.length() > (EEPROM_USER_MAX_LEN - 1)) {
    phoneNumber = phoneNumber.substring(0, EEPROM_USER_MAX_LEN - 1);
  }
  for (int i = 0; i < phoneNumber.length(); i++) {
    EEPROM.write(EEPROM_USER_START_ADDR + i, phoneNumber[i]);
  }
  EEPROM.write(EEPROM_USER_START_ADDR + phoneNumber.length(), '\0');
}

// ---------------------------------------------------------------------------
// Load motor state from EEPROM (0 = OFF, 1 = ON)
// ---------------------------------------------------------------------------
void loadMotorStateFromEEPROM() {
  byte val = EEPROM.read(EEPROM_MOTOR_STATE_ADDR);
  motorState = (val == 1);
}

// ---------------------------------------------------------------------------
// Store motor state into EEPROM
// ---------------------------------------------------------------------------
void storeMotorStateToEEPROM(bool state) {
  EEPROM.write(EEPROM_MOTOR_STATE_ADDR, state ? 1 : 0);
}

// ---------------------------------------------------------------------------
// Send a startup SMS to admin and (if set) secondary user
// ---------------------------------------------------------------------------
void sendStartupSMS() {
  String onlineMsg = "System is online now. MOTOR is ";
  onlineMsg += (motorState ? "ON" : "OFF");

  // Always send to admin
  Serial.println("Sending startup SMS to admin...");
  sendSMS(adminNumber, onlineMsg);

  // Extra delay so the module finishes sending the first SMS
  delay(2000);

  // Send to secondary user if not empty
  Serial.print("Attempting to send startup SMS to secondary: '");
  Serial.print(secondaryUserNumber);
  Serial.println("'");
  if (secondaryUserNumber.length() > 0) {
    sendSMS(secondaryUserNumber, onlineMsg);
  } else {
    Serial.println("Secondary user is empty. Skipping secondary SMS.");
  }
}
