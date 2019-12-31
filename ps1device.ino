/*
 * PS1 Device Access Controller
 * 12/30/2019 - v0.1 - Ron Olson and Jim Brink
 */

#include <SPI.h>
#include <Ethernet.h>
#include <SoftwareSerial.h>

/*************************************************************
 * Networking stuff
 *************************************************************/

/*
 * This is the name of the tool we want to check access for.
 * N O T E ! ! ! ! !
 * This name must match the OU in Active Directory
 * *EXACTLY*. This is also the exact same name as the
 * authorized tool entry in Wild Apricot.
 *
 * Theoretically this is the only thing that needs to be
 * changed from tool to tool, but also note the ip variable
 * below that is set in the event that DHCP did not give us
 * an address.
 */
const char* TOOL = "LeBlond Lathe";

/* Theoretically you do not need to change anything else below this line */

/* This is the request we're going to send to the server */
const char* baseURL = "GET /authcheck?device=%s&tag=%ld HTTP/1.1";

// What we're going to talk to; there is only one
// server we care about, and that's glue
IPAddress server(10, 10, 1, 224); // glue
IPAddress ip(10, 200, 1, 169); // fallback if dhcp doesn't work
EthernetClient client;
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

/*************************************************************
 * Controller stuff
 *************************************************************/
const int BUFFER_SIZE = 14; // RFID DATA FRAME FORMAT: 1byte head (value: 2), 10byte data (2byte version + 8byte tag), 2byte checksum, 1byte tail (value: 3)
const int DATA_SIZE = 10; // 10byte data (2byte version + 8byte tag)
const int DATA_VERSION_SIZE = 2; // 2byte version (actual meaning of these two bytes may vary)
const int DATA_TAG_SIZE = 8; // 8byte tag
const int CHECKSUM_SIZE = 2; // 2byte checksum
const int ledPin = 13; //  indicates relay switched on Arduino
const int inputPin = 9;//  off switch

SoftwareSerial ssrfid = SoftwareSerial(5,8);
uint8_t buffer[BUFFER_SIZE]; // used to store an incoming data frame
int buffer_index = 0;
long tag1 = 0;
long tag2 = 0;
long save_tag = 0;
unsigned auth = 0;
long authTags[] = {5631946,1760257,11710121,0000000000,00000000,0000000};// authorized tags
int relay1Pin = 3 ;  //   Output relay - controls high current relay
int relay2Pin = 4;

/*************************************************************
 * C O D E  B E G I N S  H E R E
 *************************************************************/

/*
 * Usual Arduino functions
 */

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

    // start the Ethernet connection:
    Serial.println("Initialize Ethernet with DHCP:");
    if (Ethernet.begin(mac) == 0) {
        Serial.println("Failed to configure Ethernet using DHCP");

        if (Ethernet.linkStatus() == LinkOFF) {
            Serial.println("Ethernet cable is not connected.");
        }
        // try to congifure using IP address instead of DHCP:
        Ethernet.begin(mac, ip);
    } else {
        Serial.print("  DHCP assigned IP ");
        Serial.println(Ethernet.localIP());
    }
    // give the Ethernet shield a second to initialize:
    delay(1000);

    /*
     * Controller setup
     */
    ssrfid.begin(9600);
    ssrfid.listen();

    pinMode(ledPin, OUTPUT) ;
    pinMode(inputPin, INPUT);
    pinMode(relay1Pin, OUTPUT);
    pinMode(relay2Pin, OUTPUT);

    Serial.println("All setup is done");

}

void loop() {
    int val = digitalRead(inputPin);   //    read switch
//      Serial.print("---switch  : ");Serial.println(val);
    if (val == HIGH) {
        digitalWrite(ledPin,LOW);
        digitalWrite(relay1Pin, LOW);  //      turn relay off
        save_tag = 0;
        Serial.print("---switch on : ");
        Serial.println(val);
    }
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
            if (tag1 != save_tag) {    //  at this point you have a new tag
                unsigned tag2 = display_tag();
                unsigned auth = check_auth();  // check for authorization
                if (auth == 1 )  {
                    digitalWrite(relay1Pin, HIGH); //  actvate relay
                    digitalWrite(ledPin, HIGH);     //   turn led on
                    Serial.print("auth : ");
                    Serial.print(auth);
                    Serial.println("   ledPin on");
                    Serial.println("--");
                }

            }
        }
    }


}


/*
 * Our custom functions
 */


/*
 * This function makes the remote call to the server for the
 * given tag. It returns either a 1 (ok) or 0 (not ok)
 */
int checkAccess(long tag) {
    Serial.print("connecting to ");
    Serial.print(server);
    Serial.println("...");

    // if you get a connection, report back via serial:
    if (client.connect(server, 8080)) {
        Serial.print("connected to ");
        Serial.println(client.remoteIP());

        // Put our request together and submit it
        char finalURL[200] = {0};
        sprintf(finalURL, baseURL, TOOL, tag);
        client.println(finalURL);
        client.println("Host:glue.pumpingstationone.org:8080");
        client.println("Connection: close");
        client.println();
    } else {
        // if you didn't get a connection to the server:
        Serial.println("connection failed");
    }

    // Now get our response which, given that it's HTTP,
    // will include things like the headers, etc. Since
    // we know the payload, we are safe allocating only
    // 200 bytes to get all of it
    byte response[200] = {0};
    while(client.connected()) {
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

    Serial.println("\nFinal:");
    Serial.write(response, 200);
    Serial.println("\nOur value:");
    int resplen = strlen(response);

    // The final go/no-go as to whether the tag
    // has the proper access is returned by the
    // server as a 1 (okay, proceed) or 0 (do not).
    char ok[2] = {0};
    char* p = &response[resplen-1];
    memcpy(ok, p, 1);
    Serial.println(ok);

    return atoi(ok);
}

unsigned check_auth() {
    //      Serial.println("-=============-");
    //      Serial.print("save_tag a : "); Serial.println(save_tag);
    Serial.print("tag1 a : ");
    Serial.println(tag1);
    Serial.print("tag2 a : ");
    Serial.println(tag2);
    Serial.println("--");
    // And now we're going to call the web service to see whether
    // this tag is valid for this tool...
    auth = 0;
    auth = checkAccess(tag1)
   
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
