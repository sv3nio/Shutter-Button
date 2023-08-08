#include <Arduino.h>
#include <SPI.h>
#include "Adafruit_BLE.h"
#include "Adafruit_BluefruitLE_SPI.h"
#include "BluefruitConfig.h"
#include "keycode.h"

// ###### DECLARATIONS ######
#define W_LED 16        // LED pin (A2)
#define VBATPIN A7      // Battery pin
#define ShutterBtn 14   // Shutter button pin

bool FACTORYRESET_ENABLE = 0;
Adafruit_BluefruitLE_SPI ble(BLUEFRUIT_SPI_CS, BLUEFRUIT_SPI_IRQ, BLUEFRUIT_SPI_RST);

typedef struct
{
  uint8_t modifier;   // Keyboard modifier keys
  uint8_t reserved;   // Reserved for OEM use, always set to 0.
  uint8_t keycode[6]; // Key codes of the currently pressed keys. <-- IMPORTANT!
} hid_keyboard_report_t;

// Report that send to Central every scanning period
hid_keyboard_report_t keyReport = { 0, 0, { 0 } };

// Report sent previously. This is used to prevent sending the same report over time.
// Notes: HID Central intepretes no new report as no changes, which is the same as
// sending very same report multiple times. This will help to reduce traffic especially
// when most of the time there is no keys pressed. This is initialized with different
// data from keyReport.
hid_keyboard_report_t previousReport = { 0, 0, { 1 } };


// ###### FUNCTIONS ######
// A small helper
void error(const __FlashStringHelper*err) {
  setLED(5);
  Serial.println(err);
  while (1);
}

// Status LED
//    1 = Low Battery          = Slow Blink
//    2 = Disconnected         = 3x Fast Blink
//    3 = Connected & Ready    = Steady
//    4 = Error                = Fast Blink

void setLED(int cond) {
  // Low Battery (Slow Blink)
  if ( cond == 1 ) {
    digitalWrite(W_LED, HIGH);
    delay(700);
    digitalWrite(W_LED, LOW);
    delay(650);
  }

  // Disconnected/Pairing (Fast Blink)
  if ( cond == 2 ) {
    digitalWrite(W_LED, HIGH);
    delay(100);
    digitalWrite(W_LED, LOW);
    delay(100);
  }

  // Connected & Ready (Steady)
  if ( cond == 3 ) {
    digitalWrite(W_LED, HIGH);
  }

  // Error (3x fast blink)
  if ( cond == 4 ) {
    digitalWrite(W_LED, HIGH);
    delay(75);
    digitalWrite(W_LED, LOW);
    delay(75);
    digitalWrite(W_LED, HIGH);
    delay(75);
    digitalWrite(W_LED, LOW);
    delay(75);
    digitalWrite(W_LED, HIGH);
    delay(75);
    digitalWrite(W_LED, LOW);
    delay(500);
  }
}

// ###### SETUP ######
void setup(void)
{
  // Configure pins
  pinMode(W_LED, OUTPUT);
  pinMode(ShutterBtn, INPUT_PULLUP);

  Serial.begin(115200);
  delay(500);
  Serial.println( F("Setting up...") );

  // Initialize BLE module
  if ( !ble.begin(VERBOSE_MODE) )
  {
    error(F("ERROR: Couldn't find Bluefruit, make sure it's in CoMmanD mode & check wiring?"));
  }
  Serial.println( F("  --> BLE initialized.") );

  /*

  Pairing Mode / Reset

  This is a hack. The BLE module does have AT commands for disconnecting and
  forgetting devices, however it is simpler to intercept a keypress on bootup
  and initiate a reset to wipe the pairing history (this saves on processing
  during the main loop). The reset leaves the unit ready for pairing. To
  initiate, the user must hold the SHUTTER button for 3 seconds while turning
  the remote on. The LED will pulse to indicate it is ready for pairing.

  */
  if ( digitalRead(ShutterBtn) == LOW ) {
    delay(2500);
    if ( digitalRead(ShutterBtn) == LOW ) {
      FACTORYRESET_ENABLE = 1;
    }
  }

  // Factory Reset. Used pairing, dev or troubleshooting.
  if ( FACTORYRESET_ENABLE )
  {
    ble.factoryReset();
    ble.sendCommandCheckOK(F( "AT+GAPDEVNAME=Shutter Button" ));
    ble.reset();
    Serial.println( F("  --> Factory reset complete. Ready to pair.") );
  }

  // Disable command echo from Bluefruit
  ble.echo(false);

  // Enable HID Service (if not already running)
  int32_t hid_en = 0;
  ble.sendCommandWithIntReply( F("AT+BleHIDEn"), &hid_en);

  if ( !hid_en )
  {
    ble.sendCommandCheckOK(F( "AT+BleHIDEn=On" ));
    !ble.reset();
    Serial.println(F("  --> HID service enabled."));
  }

  Serial.println(F("  --> Controller configured."));
  Serial.println(F("  --> READY!"));
}

// ###### LOOP ######
void loop(void)
{

  // Get BATT voltage
  float measuredvbat = analogRead(VBATPIN);
  measuredvbat *= 2;    // we divided by 2, so multiply back
  measuredvbat *= 3.3;  // Multiply by 3.3V, our reference voltage
  measuredvbat /= 1024; // convert to voltage

  if ( ble.isConnected() )
  {
    // Set LED
    if (measuredvbat < 3.37) {
      setLED(1);    // Low battery
    } else {
      setLED(3);    // Ready & connected
    }

    // Once connected, we need to make sure that the LED doesn't accidentally
    // show pairing mode in the event of a lost signal after a successful
    // pairing.
    if ( FACTORYRESET_ENABLE ) {
      FACTORYRESET_ENABLE = 0;
    }

    // Loop through all 6 keycode slots
    for(int i=0; i<6; i++) {
      keyReport.keycode[i] = 0; // ...and fill them with zeros
    }
    // However, if the pin is LOW (button is pressed)...
    if ( digitalRead(ShutterBtn) == LOW )
    {
      // Output the event to serial...
      Serial.print(" Pressed!  ||  "); Serial.println(measuredvbat);

      // And replace the 0 in the first slot with the appropriate code.
      keyReport.keycode[1] = HID_KEY_RETURN; // Return key is used to trigger the shutter on Android.
    }

    // Compare the new keyReport with the previousReport so that we don't
    // send the same report twice. This prevents restarting key presses that
    // should be continious and saves on airtime, battery and things.
    if ( memcmp(&previousReport, &keyReport, 8) ) // Returns "0" if they match.
    {
      // Send the keyReport
      ble.atcommand("AT+BLEKEYBOARDCODE", (uint8_t*) &keyReport, 8);

      // Copy to previousReport
      memcpy(&previousReport, &keyReport, 8);
    }
  } else {                          // BLE not connected...
    if ( FACTORYRESET_ENABLE ) {    // ...because of pairing mode
      setLED(2);
    } else {                        // ...or some other reason
      setLED(4);
    }
  }
}
