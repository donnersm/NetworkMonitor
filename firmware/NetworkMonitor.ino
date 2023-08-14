/**********************************************************************************************
 * Project: Network Monitor - Sends telegram with IP,MAC, Device NAME ( if available ) of any
 * new device that connects to the same Network
 * Version: 1.0
 * 
 * 
 * 
 * Mark Donners
 * The Electronic Engineer
 * Website:   www.theelectronicengineer.nl
 * facebook:  https://www.facebook.com/TheelectronicEngineer
 * youtube:   https://www.youtube.com/channel/UCm5wy-2RoXGjG2F9wpDFF3w
 * github:    https://github.com/donnersm
 ***********************************************************************************************
* Telegram Functions based on libary by Brian Lough
* All credits for the Telegram library go to him
* https://github.com/witnessmenow/ESP32-WiFi-Manager-Examples
* 
* IPsniffer code sections are based on an example done by Bitluni
* orignal code here: https://github.com/bitluni/IPSnifferPrinter
* 

* important note: Wifimanager will only succeed in saving all parameters
* if nothing is left blank and the connection to your WIFI access point is succesfully completed!
* If it cannot connect because, for instance, the password is wrong..saving will fail!
* It is also important to enter your valid WIFI credentials each
* time you exit the wifimanager after changing a parameter
* 
* You will also have to create your own Telegram BOT using BotFather
* open your telegram messenger on your phone and search for "botfather"
* Type /newbot and follow the instructions to create your bot. Give it a name and username.
* If your bot is successfully created, you’ll receive a message with a link to access the bot and 
* the bot token. Save the bot token because you’ll need it so that the ESP32 can interact with the bot.
* To get our Chat ID, search for "IDBot" and type /getid
* 
* Use the wifi manager to enter the Chat ID and Bot token. Remember to also enter your WIFI credentials
* each and every time! without it, your new data will not be saved.
* To force the WIFI manager to start you can press and hold the trigger button ( pin 13) 
* while you press the reboot button.
* 
* check out https://en.wikipedia.org/wiki/List_of_tz_database_time_zones to find a valid country code
* 
* ****************************************************************************
* Functions used in this program:
* void setup() // initialize the esp32
* void loop()  // the actual continuesly running program
******
* Stuff we need to use data from the SPIFF table / read/write
* We are using this to store the settings from the wifi manager and to retrieve them
* void saveConfigFile()
* bool loadConfigFile()
* void saveConfigCallback()
* void configModeCallback(WiFiManager *myWiFiManager)
******
* Network monitoring
* void setupUDP()
* void parsePacket(char* data, int length)
*******
* Function to print IP address in correct format with dots in between numbers
* void printIP(char *data)
********
* Function to handle incomming messages from Telegram
* void handleNewMessages(int numNewMessages)
* **********************************************************************************/



// Libraries
#include <WiFi.h>                               // WiFi Library part of the esp32 framework V1.0.0
#include <ESPmDNS.h>                            // part of the ESP32 framework V1.0
#include <WiFiUdp.h>                            // part of the ESP32 framework v1.0
#include <AsyncUDP.h>                           // https://github.com/me-no-dev/ESPAsyncUDP
#include <FS.h>                                 // File System Library part of the esp32 framework v1.0
#include <ezTime.h>                             // https://github.com/ropg/ezTime V0.8.3
#include <WiFiManager.h>                        // https://github.com/tzapu/WiFiManager V2.0.3-alpha
#include <ArduinoJson.h>                        // https://github.com/arduino-libraries/Arduino_JSON V6.19.1
#include <UniversalTelegramBot.h>               // https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot V1.3.0
#include <WiFiClientSecure.h>                   // part of the esp32 framework V1.0
#include <SPIFFS.h>                             // SPI Flash System Library part of the esp32 framework V1.0

/*********************************************************************************************************************
//                                        some stuff don't change
**********************************************************************************************************************/
#define ESP_DRD_USE_SPIFFS true
const char* ssid = "Element";
const char* password = "ForElement14";
#define JSON_CONFIG_FILE "/test_configb.json"    // JSON configuration file
#define SwitchInput 13
bool shouldSaveConfig = false;                  // Flag for saving data
AsyncUDP udp;

boolean mute=0;
// Variables to hold data from custom textboxes
// content will be overwritten by wifi manager
//har TelegramBOTtoken[50] = "XXXXXXXXXX:XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";
char TelegramBOTtoken[50] = "5696012387:AAEe_FDdD81N4UPGTtKub22yyEQSgfxLryRY";
//char Chat_ID[15] = "1234567890";
char Chat_ID[15] = "11215771304";
char Country[30] = "Europe/Amsterdam";
char chatId[15] = "";

WiFiClientSecure clientTCP;
WiFiManager wm; // Define WiFiManager Object
Timezone myTZ;

UniversalTelegramBot bot(TelegramBOTtoken, clientTCP); // setup the Telgram bot
bool flashState = LOW; //By Default it is off
int botRequestDelay = 1000;   // mean time between scan messages
long lastTimeBotRan;          // last time messages' scan has been done
void handleNewMessages(int numNewMessages);
const int DHCP_PACKET_CLIENT_ADDR_LEN_OFFSET = 2;
const int DHCP_PACKET_CLIENT_ADDR_OFFSET = 28;

enum State
{
	READY,
	RECEIVING,
	RECIEVED
};

volatile State state = READY;
String newMAC;
String newIP;
String newName;
const char *hexDigits = "0123456789ABCDEF";
String DetectorMessage;

/*********************************************************************************************************************
//                                        Now the coding begins
**********************************************************************************************************************/


/********  function to safe the configuration file, created by *****
 *         the WIFI manager to the SPIFF table
********************************************************************/
//
void saveConfigFile() // Save Config in JSON format
{
  Serial.println(F("----> Saving configuration..."));
  // Create a JSON document
  StaticJsonDocument<512> json;
  json["TelegramBOTtoken"] = TelegramBOTtoken;
  //json["testNumber"] = testNumber;
  json["Chat_ID"] = Chat_ID;
  json["Country"] = Country;
  // Open config file
  File configFile = SPIFFS.open(JSON_CONFIG_FILE, "w");
  if (!configFile)
  {
    // Error, file did not open
    Serial.println("----> Failed to open config file for writing");
  }

  // Serialize JSON data to write to file
  serializeJsonPretty(json, Serial);
  if (serializeJson(json, configFile) == 0)
  {
    // Error writing file
    Serial.println(F("----> Failed to write to file"));
  }
  // Close file
  configFile.close();
}

/******** function to load the config file from SPIFF  ******
so we can use the saved settings like network credentials 
*************************************************************/
bool loadConfigFile()
// Load existing configuration file
{
  // Uncomment if we need to format filesystem
  //SPIFFS.format();

  // Read configuration from FS json
  Serial.println("----> Mounting File System...");

  // May need to make it begin(true) first time you are using SPIFFS
  if (SPIFFS.begin(false) || SPIFFS.begin(true))
  {
    Serial.println("----> Mounted file system");
    if (SPIFFS.exists(JSON_CONFIG_FILE))
    {
      // The file exists, reading and loading
      Serial.println("----> Reading config file");
      File configFile = SPIFFS.open(JSON_CONFIG_FILE, "r");
      if (configFile)
      {
        Serial.println("----> Opened configuration file");
        StaticJsonDocument<512> json;
        DeserializationError error = deserializeJson(json, configFile);
        serializeJsonPretty(json, Serial);
        if (!error)
        {
          Serial.println("----> Parsing JSON");
          strcpy(TelegramBOTtoken, json["TelegramBOTtoken"]);
          //   testNumber = json["testNumber"].as<int>();
          strcpy(Chat_ID, json["Chat_ID"]);
          strcpy(Country, json["Country"]);
          return true;
        }
        else
        {
          // Error loading JSON data
          Serial.println("----> Failed to load json config");
        }
      }
    }
  }
  else
  {
    // Error mounting file system
    Serial.println("----> Failed to mount FS");
  }
  return false;
}

/*  Function to remind us to save our files when needed  ****
*************************************************************/

void saveConfigCallback()
// Callback notifying us of the need to save configuration
{
  Serial.println("----> Should save config");
  shouldSaveConfig = true;
}


/*****  function that is called whenever we have to  ***
 ****   enter the configuration mode                 ***
 *******************************************************/
void configModeCallback(WiFiManager *myWiFiManager)
// Called when config mode launched
{
  Serial.println("----> Entered Configuration Mode");
  Serial.print("----> Config SSID: ");
  Serial.println(myWiFiManager->getConfigPortalSSID());
  Serial.print("----> Config IP Address: ");
  Serial.println(WiFi.softAPIP());
}


/*** This function desects the package data from the network. ****
 *   we get alle the info we need from it and discard the rest ***
 *****************************************************************/
void parsePacket(char* data, int length)
{
	if(state == RECIEVED) return;
	String tempName;
	String tempIP;
	String tempMAC;

	Serial.println("Just signed in:");
	//printHex(data, length);
	Serial.print("MAC address: ");
 // DetectorMessage+="MAC address:\n";
	for(int i = 0; i < data[DHCP_PACKET_CLIENT_ADDR_LEN_OFFSET]; i++)
		if(i < data[DHCP_PACKET_CLIENT_ADDR_LEN_OFFSET] - 1)
			Serial.printf("%02X:", (int)data[DHCP_PACKET_CLIENT_ADDR_OFFSET + i]);
      
    //  DetectorMessage+= (int)data[DHCP_PACKET_CLIENT_ADDR_OFFSET + i];
		else
			Serial.printf("%02X", (int)data[DHCP_PACKET_CLIENT_ADDR_OFFSET + i]);
	for(int i = 0; i < data[DHCP_PACKET_CLIENT_ADDR_LEN_OFFSET]; i++)
	{
		tempMAC += hexDigits[(int)data[DHCP_PACKET_CLIENT_ADDR_OFFSET + i] >> 4];
		tempMAC += hexDigits[(int)data[DHCP_PACKET_CLIENT_ADDR_OFFSET + i] & 15];
		if(i < data[DHCP_PACKET_CLIENT_ADDR_LEN_OFFSET] - 1)
			tempMAC += ":";
	}

	Serial.println();
	//parse options
	int opp = 240;
	while(opp < length)
	{
		switch(data[opp])
		{
			case 0x0C:
			{
				Serial.print("Device name: ");
   //     DetectorMessage+="Device name:\n";
				for(int i = 0; i < data[opp + 1]; i++)
				{
					Serial.print(data[opp + 2 + i]);
       //   DetectorMessage+=(data[opp + 2 + i]);
					tempName += data[opp + 2 + i];
				}
				Serial.println();
				break;
			}
			case 0x35:
			{
				Serial.print("Packet Type: ");
				switch(data[opp + 2])
				{
					case 0x01:
						Serial.println("Discover");
					break;
					case 0x02:
						Serial.println("Offer");
					break;
					case 0x03:
						Serial.println("Request");
						if(state == READY)
							state = RECEIVING;
					break;
					case 0x05:
						Serial.println("ACK");
					break;
					default:
						Serial.println("Unknown");
				}
				break;
			}
			case 0x32:
			{
				Serial.print("Device IP: ");
				printIP(&data[opp + 2]);
				Serial.println();
				for(int i = 0; i < 4; i++)
				{
					tempIP += (int)data[opp + 2 + i];
					if(i < 3) tempIP += '.';
				}
				break;
			}
			case 0x36:
			{
				Serial.print("Server IP: ");
				printIP(&data[opp + 2]);
				Serial.println();
				break;
			}

			case 0xff:
			{
				opp = length; 
				continue;
			}
			default:
			{

			}
		}

		opp += data[opp + 1] + 2;
	}
	if(state == RECEIVING)
	{
		newName = tempName;
		newIP = tempIP;
		newMAC = tempMAC;
//		Serial.println("Stored data.");
		state = RECIEVED;
	}
	Serial.println();
 DetectorMessage="Just accessed your network:\n";
 DetectorMessage+=newName;
 DetectorMessage+="\nIP Address: ";
 DetectorMessage+= newIP;
 DetectorMessage+="\nMac address: ";
 DetectorMessage+=newMAC;
 DetectorMessage+="\n";
 
}


/****  This is the setup function to initialize the UDP package monitor  ***
 **************************************************************************/
void setupUDP()
{
	if(udp.listen(67)) 
	{
		Serial.print("UDP Listening on IP: ");
		Serial.println(WiFi.localIP());
		udp.onPacket([](AsyncUDPPacket packet) 
		{
			char *data = (char *)packet.data();
			int length = packet.length();
			parsePacket(data, length);
		});
	};
}





/****  This is the setup function to initialize our microcontroller  ***
 ****  and start the needed services
 **********************************************************************/
void setup() 
{
 Serial.begin(115200);
 delay(10);
  // Change to true when testing to force configuration every time we run
  bool forceConfig = false;
  bool spiffsSetup = loadConfigFile();
  if (!spiffsSetup)
  {
    Serial.println(F("----> Forcing config mode as there is no saved config"));
    forceConfig = true;
  }
  pinMode(SwitchInput, INPUT_PULLUP);
  WiFi.mode(WIFI_STA); // Explicitly set WiFi mode

  // Set config save notify callback
  wm.setSaveConfigCallback(saveConfigCallback);

  // Set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wm.setAPCallback(configModeCallback);

  // Custom elements
  // Text box (String) - 50 characters maximum
  WiFiManagerParameter custom_text_box("key_text", "Enter your Telegram Token here", TelegramBOTtoken, 50);
  // Text box (String) - 15 characters maximum
  WiFiManagerParameter custom_text_box2("key_text2", "Enter your Telegram Chat ID here", Chat_ID, 15);
  // Text box (String) - 50 characters maximum
  WiFiManagerParameter custom_text_box3("key_text3", "Enter your Timezone here example: Europe/Amsterdam", Country, 30);
  // Add all defined parameters
  wm.addParameter(&custom_text_box);
  //wm.addParameter(&custom_text_box_num);
  wm.addParameter(&custom_text_box2);
  //wm.addParameter(&custom_text_box_num);
  wm.addParameter(&custom_text_box3);

  if (digitalRead(SwitchInput) == 0) {
    Serial.println("----> Push button pressed and hold during startup, entering config mode...");
    forceConfig = true;
  }

  if (forceConfig)
    // Run if we need a configuration
  {
    if (!wm.startConfigPortal("ELEMENT_14", "password"))
    {
      Serial.println("----> Failed to connect and hit timeout");
      delay(3000);
      //reset and try again, or maybe put it to deep sleep
      ESP.restart();
      delay(5000);
    }
  }
  else
  {
    if (!wm.autoConnect("ELEMENT_14", "password"))
    {
      Serial.println("----> failed to connect and hit timeout");
      delay(3000);
      // if we still have not connected restart and try all over again
      ESP.restart();
      delay(5000);
    }
  }
  WiFi.mode(WIFI_STA);
  clientTCP.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Add root certificate for api.telegram.org
  // If we get here, we are connected to the WiFi

  Serial.println("");
  Serial.println("----> WiFi connected");
  Serial.print("----> IP address: ");
  Serial.println(WiFi.localIP());

  // Wait for ezTime to get its time synchronized
  waitForSync();
  Serial.println();
  Serial.println("UTC:             " + UTC.dateTime());

  // Lets deal with the user config values
  // Copy the string value
  strncpy(TelegramBOTtoken, custom_text_box.getValue(), sizeof(TelegramBOTtoken));
  Serial.print("----> TelegramBOTtoken: ");
  Serial.println(TelegramBOTtoken);

  strncpy(Chat_ID, custom_text_box2.getValue(), sizeof(Chat_ID));
  Serial.print("----> ChatID: ");
  Serial.println(Chat_ID);

  strncpy(Country, custom_text_box3.getValue(), sizeof(Country));
  Serial.print("----> Timezone/Country: ");
  Serial.println(Country);

  // Provide official timezone names
  // https://en.wikipedia.org/wiki/List_of_tz_database_time_zones
  // myTZ.setLocation(F("de"));
  myTZ.setLocation(Country);
  Serial.printf("----> Timezone set to:%s\n", Country);
  Serial.println(myTZ.dateTime());

  // now lets also update the token en chat Id we are working with from memory
  strncpy(chatId, Chat_ID, sizeof(Chat_ID));

  bot.updateToken(String (TelegramBOTtoken));

  // Save the custom parameters to FS
  if (shouldSaveConfig)
  {
    saveConfigFile();
  }
esp_err_t err;

  bot.sendMessage(chatId, "Element 14 Network Monitor Started\n Type help for help\n", "Markdown");

	Serial.println("Ready");
  	Serial.print("IP address of this monitor: ");
  	Serial.println(WiFi.localIP());
	setupUDP();
	Serial.println();
}

/* This is the main loop and it does 3 things:                        ****
 *******  1. check if the flag is set that a new device has           ****
 *           entered the network and send a telegram with the data    ****
 *        2. monitor for incomming messages from Telegram             ****
 *           and answer if applicable                                 ****
 *************************************************************************/          

void loop() 
{ 
	delay(20);
	if(state == RECIEVED)
	{	
	 if (mute==0) bot.sendMessage(chatId, DetectorMessage, "Markdown");
	 state = READY;
	}


  if (millis() > lastTimeBotRan + botRequestDelay) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      Serial.println("got response");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTimeBotRan = millis();
  }

 
}
/* This function is a little tool to print the IP digits seperated with d dot. ****
 **********************************************************************************/
void printIP(char *data)
{
  for(int i = 0; i < 4; i++)
  {
    Serial.print((int)data[i]);
 
    if(i < 3)
      Serial.print('.');
  }
}


/********** Function to handle incomming telegram messages   ************
 ************************************************************************/
void handleNewMessages(int numNewMessages) {
  Serial.print("Handle New Messages: ");
  Serial.println(numNewMessages);

  for (int i = 0; i < numNewMessages; i++) {
    // Chat id of the requester
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != chatId) {
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }

    // Print the received message
    String text = bot.messages[i].text;
    Serial.println(text);

    String fromName = bot.messages[i].from_name;
    if (text == "mute"){
      mute = 1;
      String answermute = "Device is silenced until reboot or unmute command\n";
      bot.sendMessage(chatId, answermute, "Markdown");
    }

    if (text == "unmute"){
      mute=0;
      String answermute2 = "Device is no longer silenced until mute command\n";
      bot.sendMessage(chatId, answermute2, "Markdown");
    }
 
    if (text == "help") {
      String  welcome = "This is the Element 14 Network Monitor.\n";
      welcome += "Type mute to mute all messages\n";
      welcome += "Type unmute to unmute all messages\n";
      welcome += "Created bij The Electronic Engineer.\n\n";
      welcome += "https://github.com/donnersm \n";
      welcome +=  " *www.theelectronicengineer.nl*\n\n";
      welcome += "Based on code written by Bitluni's IPSnifferPrinter\n";
      welcome += "Re-written to include wifi Manager\n";
      welcome += "and Telegram communication\n";
      welcome += "version 1.0\n\n";
     // welcome += "original code here:\n";
     // welcome += "https://github.com/bitluni/IPSnifferPrinter\n";
      bot.sendMessage(chatId, welcome, "Markdown");
    }
  }
};
