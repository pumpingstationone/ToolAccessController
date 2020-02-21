//  RFID reader controls a relay - j brink Feb 2020

#include <SoftwareSerial.h>
#include <LiquidCrystal.h>
#include <SPI.h>
#include <Ethernet.h>


const char* TOOL = "LeBlond%20Lathe";

/* Theoretically you do not need to change anything else below this line */

// Network stuff

const char* baseURL = "GET /authcheck?device=%s&tag=%ld HTTP/1.1";

// What we're going to talk to; there is only one
// server we care about, and that's glue
IPAddress server(10, 10, 1, 224); // glue
int GLUE_PORT = 8080;
IPAddress ip(10, 100, 3, 0); // fallback if dhcp doesn't work
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// initialize the library by associating any needed LCD interface pin
// with the arduino pin number it is connected to
const int rs = 7, en = 6, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
const int a3Pin = A3;//  off switch
const int BUFFER_SIZE = 14; // RFID DATA FRAME FORMAT: 1byte head (value: 2), 10byte data (2byte version + 8byte tag), 2byte checksum, 1byte tail (value: 3)
const int DATA_SIZE = 10; // 10byte data (2byte version + 8byte tag)
const int DATA_VERSION_SIZE = 2; // 2byte version (actual meaning of these two bytes may vary)
const int DATA_TAG_SIZE = 8; // 8byte tag
const int CHECKSUM_SIZE = 2; // 2byte checksum
int a3Value = 0;        // variable to store the value coming from the a3 pin
SoftwareSerial ssrfid = SoftwareSerial(8,1); //   RFID reader input
uint8_t buffer[BUFFER_SIZE];                //   used to store an incoming data frame
int buffer_index = 0;
long tag1 = 0;
long tag2 = 0;
long save_tag = 0;
unsigned auth = 0;
int relay1Pin = 9 ;      // Output relay - controls high current relay
long startTime;          // value returned from millis when captured
long duration;           // variable to store duration
long waitTime = 3600000;   // time to wait before turning off relay in millis 1000 milli = 1 sec
int relayOn = 0 ;        // indicates relay is on
long oldRemainingTime = 60;

void setup() {
    /*
     * Ethernet setup
     */

    // Intitialize the way this board expects it
    Ethernet.init(10);

    // Open serial communications and wait for port to open:
    Serial.begin(9600);
    while (!Serial) {
        ; // wait for serial port to connect. Needed for native USB port only
    }

    // set up the LCD's number of columns and rows:
    lcd.begin(16, 2);

    lcd.setCursor(0, 0);
    lcd.print("Networking:");
    lcd.setCursor(0, 1);

    // start the Ethernet connection:
    Serial.println("Initialize Ethernet with DHCP:");
    if (Ethernet.begin(mac) == 0) {
        Serial.println("Failed to configure Ethernet using DHCP");
        lcd.print("No DHCP");

        if (Ethernet.linkStatus() == LinkOFF) {
            Serial.println("Ethernet cable is not connected.");
            lcd.setCursor(0, 1);
            lcd.print("Hey! No cable!");
        }
        // try to congifure using IP address instead of DHCP:
        Ethernet.begin(mac, ip);
    } else {
        Serial.print("  DHCP assigned IP ");
        Serial.println(Ethernet.localIP());
        lcd.print("Got: " + Ethernet.localIP());
    }

    // give the Ethernet shield a second to initialize:
    delay(1000);

    /*
     * Now we begin the rest of the initialization
     */

    ssrfid.begin(9600);
    ssrfid.listen();


    pinMode(relay1Pin, OUTPUT);

    Serial.println("INIT DONE");

    // Print a message to the LCD.
    lcd.setCursor(0, 0);
    lcd.print("LeBlond Lathe   ");
    lcd.setCursor(0, 1);
    lcd.print("Please hit reset");
    lcd.setCursor(0, 0);   
}

void loop() {
    a3Value = analogRead(a3Pin);
    if (a3Value > 500) {             // if input > 3v, button pressed
        if ( (millis() - startTime ) >  20000 ) {
            startTime = millis();           //   reset time relay was turned on
            delay(200);
        } else {                         //    button was pressed twice in < so turn off
            digitalWrite(relay1Pin, LOW);  //    turn relay off
            relayOn = 0 ;                  //    indicate relay is off
            lcd.setCursor(0, 0);
            lcd.print("LeBlond Lathe   ");
            lcd.setCursor(0, 1);
            lcd.print("Please scan tag ");
            lcd.setCursor(0, 0);
            save_tag = 0;
        }
    }
    if (relayOn == 1) {
        //Serial.print( "  millis - st = " );
        //Serial.println(( millis() - startTime ));        
        lcd.setCursor(0,0);
        lcd.print("Machine is on   ");
        lcd.setCursor(0,1);
        lcd.print("Time left:");
        lcd.setCursor(11,1);
        long remainingTime = (( waitTime ) - (millis() - startTime ))  / 60000;
       
        if(remainingTime < oldRemainingTime)
        {          
          lcd.setCursor(11,1);          
          lcd.print(remainingTime);
       
          oldRemainingTime = remainingTime;
        }
        if ((millis() - startTime) > waitTime ) {      // check to see if wait time is exceeded
            digitalWrite(relay1Pin, LOW);                   // if so      turn relay off
            lcd.setCursor(0,0);
            lcd.print("SHUTTING DOWN!  ");
            lcd.setCursor(0,1);
            lcd.print("                ");
            save_tag = 0;
            relayOn  = 0;
            startTime = millis();            // set start time to current time
        }
    }
    //  Serial.println("---check RFID ");
    if (ssrfid.available() > 0) {
        bool call_extract_tag = false;

        int ssvalue = ssrfid.read(); // read
        if (ssvalue == -1) { // no data was read
            save_tag = 0;
            return;
        }
        if (ssvalue == 2) { // RDM630/RDM6300 found a tag => tag incoming
            buffer_index = 0;
        } else if (ssvalue == 3) { // tag has been fully transmitted
            call_extract_tag = true; // extract tag at the end of the function call
        }
        if (buffer_index >= BUFFER_SIZE) { // checking for a buffer overflow (It's very unlikely that an buffer overflow comes up!)
            Serial.println("Error: Buffer overflow detected!");
            return;
        }

        buffer[buffer_index++] = ssvalue; // everything is alright => copy current value to buffer
        if (call_extract_tag == true) {
            if (buffer_index == BUFFER_SIZE) {
                unsigned tag = extract_tag();
            } else { // something is wrong... start again looking for preamble (value: 2)
                buffer_index = 0;
                return;
            }
            if (tag1 != save_tag) {              //  at this point you have a new tag
                lcd.setCursor(0, 1);
                lcd.print("Read ");
                unsigned tag2 = display_tag();

                unsigned auth = check_auth();      // check for authorization

                lcd.setCursor(6, 1);
                lcd.print(" NOT auth");
                if (auth == 1 )  {
                    lcd.setCursor(6, 1);
                    lcd.print("Tag OK    ");
                    digitalWrite(relay1Pin, HIGH);  //   actvate relay
                    relayOn  = 1 ;                  //   indicate relay is on
                    startTime = millis();           //   set time relay was turned on
                    Serial.print("auth : ");
                    Serial.print(auth);
                    Serial.println("   ledPin on");
                    Serial.println("--");
                    Serial.print( "startTime :");
                    Serial.println(startTime);
                }

            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////

unsigned check_auth() {
    //      Serial.println("-=============-");
    //      Serial.print("save_tag a : "); Serial.println(save_tag);
    Serial.print("tag1 a : ");
    Serial.println(tag1);
    Serial.print("tag2 a : ");
    Serial.println(tag2);
    Serial.println("--");
    long auth = 0;


    auth = checkAccess(tag1);
    delay(1000);

    Serial.println("-exit-");
    Serial.print("Authorization: ");
    Serial.println(auth);

    return auth;
}

unsigned extract_tag() {
    uint8_t *msg_data = buffer + 1; // 10 byte => data contains 2byte version + 8byte tag
    uint8_t *msg_data_tag = msg_data + 2;
    // print message that was sent from RDM630/RDM6300
    Serial.println("--");
    long tag = hexstr_to_value(msg_data_tag, DATA_TAG_SIZE);
    Serial.print("Extracted Tag: ");
    Serial.println(tag);
    tag1 = tag;
    return tag;
}


unsigned display_tag() {
    uint8_t msg_head = buffer[0];
    uint8_t *msg_data = buffer + 1; // 10 byte => data contains 2byte version + 8byte tag
    uint8_t *msg_data_version = msg_data;
    uint8_t *msg_data_tag = msg_data + 2;
    uint8_t *msg_checksum = buffer + 11; // 2 byte
    uint8_t msg_tail = buffer[13];
    // print message that was sent from RDM630/RDM6300
    Serial.println("--------");
    Serial.print("Message-Head: ");
    Serial.println(msg_head);
    Serial.println("Message-Data (HEX): ");
    for (int i = 0; i < DATA_VERSION_SIZE; ++i) {
        Serial.print(char(msg_data_version[i]));
    }
    Serial.println(" (version)");
    for (int i = 0; i < DATA_TAG_SIZE; ++i) {
        Serial.print(char(msg_data_tag[i]));
    }
    Serial.println(" (tag)");
    Serial.print("Message-Checksum (HEX): ");
    for (int i = 0; i < CHECKSUM_SIZE; ++i) {
        Serial.print(char(msg_checksum[i]));
    }
    Serial.println("");
    Serial.print("Message-Tail: ");
    Serial.println(msg_tail);
    Serial.println("--");
    long tag2 = hexstr_to_value(msg_data_tag, DATA_TAG_SIZE);
    Serial.print("Extracted Tag: ");
    Serial.println(tag2);
    save_tag = tag2 ;
    long checksum = 0;
    for (int i = 0; i < DATA_SIZE; i+= CHECKSUM_SIZE) {
        long val = hexstr_to_value(msg_data + i, CHECKSUM_SIZE);
        checksum ^= val;
    }
    Serial.print("Extracted Checksum (HEX): ");
    Serial.print(checksum, HEX);
    if (checksum == hexstr_to_value(msg_checksum, CHECKSUM_SIZE)) { // compare calculated checksum to retrieved checksum
        Serial.print(" (OK)"); // calculated checksum corresponds to transmitted checksum!
    } else {
        Serial.print(" (NOT OK)"); // checksums do not match
    }
    Serial.println("");
    Serial.println("--------+++");
    return tag2;
}


long hexstr_to_value(char *str, unsigned int length) { // converts a hexadecimal value (encoded as ASCII string) to a numeric value
    char* copy = malloc((sizeof(char) * length) + 1);
    memcpy(copy, str, sizeof(char) * length);
    copy[length] = '\0';
    // the variable "copy" is a copy of the parameter "str". "copy" has an additional '\0' element to make sure that "str" is null-terminated.
    long value = strtol(copy, NULL, 16);  // strtol converts a null-terminated string to a long value
    free(copy); // clean up
    return value;
}

int checkAccess(long tag) {
    EthernetClient client;
    Serial.print("connecting to ");
    Serial.print(server);
    Serial.println("...");
    int connectedOk = 0;

    lcd.setCursor(0, 0);
    lcd.print("Checking...     ");
    lcd.setCursor(0, 1);
    lcd.print("                ");
    
    client.setConnectionTimeout(2000);
    // if you get a connection, report back via serial:
    int rc = client.connect(server, GLUE_PORT);
    Serial.print("Connect RC is ");
    Serial.println(rc);

    if (rc == 1) {
        Serial.print("connected to ");
        Serial.println(client.remoteIP());

        // Put our request together and submit it
        char finalURL[200] = {0};
        sprintf(finalURL, baseURL, TOOL, tag);
        Serial.print("Our URL: ");
        Serial.println(finalURL);

        client.println(finalURL);
        client.println("Host:10.10.1.224:80");
        client.println("Connection: close");
        client.println();
        connectedOk = 1;
    } else {
        // if you didn't get a connection to the server:
        Serial.println("connection failed");
        lcd.print("No connection :(");
    }



    if (connectedOk == 0) {
        Serial.println("Did not connect, so not allowing access");
        lcd.print("No connection :(");
        return 0;
    }

    // If we got here, we were able to talk to the server...
    //delay(1000);

    // Now get our response which, given that it's HTTP,
    // will include things like the headers, etc. Since
    // we know the payload, we are safe allocating only
    // 200 bytes to get all of it
    byte response[200] = {0};
    while (client.connected()) {
        Serial.println("now reading...");
        int len = client.available();
        Serial.println("len is ");
        Serial.println(len);
        Serial.println("\n");
        if (len > 0) {
            Serial.println("got data!");

            client.read(response, 200);
        }
    }

    client.flush();
    client.stop();
    delay(2000);
    if (client.connected())
        Serial.println("Client still connected????");

    Serial.println("\nFinal:");
    Serial.write(response, 200);
    Serial.println("\nOur value:");
    int resplen = strlen(response);

    // The final go/no-go as to whether the tag
    // has the proper access is returned by the
    // server as a 1 (okay, proceed) or 0 (do not).
    char ok[2] = {0};
    char* p = &response[resplen - 1];
    memcpy(ok, p, 1);
    Serial.println(ok);

    return atoi(ok);
}
