/*
  ============================================================
       VRCHAT CYD PAGER - STABLE DIRECT PIPELINE
       ESP32-2432S028R Cheap Yellow Display
  ============================================================

  STABILITY PASS

  - Direct VRChat login
  - Saved auth session
  - TOTP / email OTP
  - Direct VRChat Pipeline WSS
  - notification
  - notification-v2
  - boops / invites / friends / system alerts
  - quiet background reconnect
  - NO client heartbeat
  - NO full-screen redraw on Pipeline state changes
  - Pipeline error packet logging
  - reconnect statistics
  - RGB LED + GPIO 26 audio

  Libraries:

    TFT_eSPI
    XPT2046_Touchscreen
    ArduinoJson
    ArduinoWebsockets by Gil Maimon

  ============================================================
*/

#include <Arduino.h>
#include <time.h>

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include <Preferences.h>

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#include <ArduinoJson.h>
#include <ArduinoWebsockets.h>

using namespace websockets;

#include <esp_arduino_version.h>
#include <mbedtls/base64.h>


// Shared application identifier. Wi-Fi credentials are selected
// on the touchscreen and stored in ESP32 Preferences.

const char* USER_AGENT =
    "MellishVRChatPager/0.5.1 (https://www.mellishpenthouse.com/)";

// Use public resolvers instead of relying on the DNS server supplied by the
// local router. DHCP still supplies the pager's IP address, gateway and mask.
const IPAddress PRIMARY_DNS(
  8, 8, 8, 8
);

const IPAddress SECONDARY_DNS(
  1, 1, 1, 1
);


// ============================================================
//                       VRCHAT API
// ============================================================

const char* API_AUTH_USER =
  "https://api.vrchat.cloud/api/1/auth/user";

const char* API_TOTP =
  "https://api.vrchat.cloud/api/1/auth/twofactorauth/totp/verify";

const char* API_EMAIL_OTP =
  "https://api.vrchat.cloud/api/1/auth/twofactorauth/emailotp/verify";

const char* API_LOGOUT =
  "https://api.vrchat.cloud/api/1/logout";


// ============================================================
//                     VRCHAT PIPELINE
// ============================================================

const char* PIPELINE_HOST =
  "pipeline.vrchat.cloud";

const uint16_t PIPELINE_PORT =
  443;

const unsigned long PIPELINE_RECONNECT_MS =
  5000;


// Turn the LCD backlight off after three minutes without
// a touch. A notification wakes it for at least 30 seconds.

const unsigned long SCREEN_TIMEOUT_MS =
  3UL * 60UL * 1000UL;

const unsigned long NOTIFICATION_WAKE_MS =
  30UL * 1000UL;


// Sectigo Public Server Authentication Root E46.
// Trust anchor for pipeline.vrchat.cloud TLS.

const char VRCHAT_ROOT_CA[] PROGMEM =
  "-----BEGIN CERTIFICATE-----\n"
  "MIICOjCCAcGgAwIBAgIQQvLM2htpN0RfFf51KBC49DAKBggqhkjOPQQDAzBfMQsw\n"
  "CQYDVQQGEwJHQjEYMBYGA1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1T\n"
  "ZWN0aWdvIFB1YmxpYyBTZXJ2ZXIgQXV0aGVudGljYXRpb24gUm9vdCBFNDYwHhcN\n"
  "MjEwMzIyMDAwMDAwWhcNNDYwMzIxMjM1OTU5WjBfMQswCQYDVQQGEwJHQjEYMBYG\n"
  "A1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1TZWN0aWdvIFB1YmxpYyBT\n"
  "ZXJ2ZXIgQXV0aGVudGljYXRpb24gUm9vdCBFNDYwdjAQBgcqhkjOPQIBBgUrgQQA\n"
  "IgNiAAR2+pmpbiDt+dd34wc7qNs9Xzjoq1WmVk/WSOrsfy2qw7LFeeyZYX8QeccC\n"
  "WvkEN/U0NSt3zn8gj1KjAIns1aeibVvjS5KToID1AZTc8GgHHs3u/iVStSBDHBv+\n"
  "6xnOQ6OjQjBAMB0GA1UdDgQWBBTRItpMWfFLXyY4qp3W7usNw/upYTAOBgNVHQ8B\n"
  "Af8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAwNnADBkAjAn7qRa\n"
  "qCG76UeXlImldCBteU/IvZNeWBj7LRoAasm4PdCkT0RHlAFWovgzJQxC36oCMB3q\n"
  "4S6ILuH5px0CMk7yn2xVdOOurvulGu7t0vzCAxHrRVxgED1cf5kDW21USAGKcw==\n"
  "-----END CERTIFICATE-----\n";


// ============================================================
//                        HARDWARE
// ============================================================

#define SCREEN_WIDTH   320
#define SCREEN_HEIGHT  240

#define TFT_BACKLIGHT  21

#define LED_RED_PIN     4
#define LED_GREEN_PIN  16
#define LED_BLUE_PIN   17

#define AUDIO_PIN      26


// ============================================================
//                       TOUCHSCREEN
// ============================================================

#define TOUCH_CS    33
#define TOUCH_IRQ   36
#define TOUCH_MOSI  32
#define TOUCH_MISO  39
#define TOUCH_CLK   25

#define TOUCH_MIN_X 200
#define TOUCH_MAX_X 3800

#define TOUCH_MIN_Y 200
#define TOUCH_MAX_Y 3800


// ============================================================
//                         OBJECTS
// ============================================================

TFT_eSPI tft =
  TFT_eSPI();

SPIClass touchSPI =
  SPIClass(VSPI);

XPT2046_Touchscreen ts(
  TOUCH_CS,
  TOUCH_IRQ
);

Preferences preferences;

WebsocketsClient pipelineSocket;


// ============================================================
//                         COLOURS
// ============================================================

#define COL_BG         0x0841
#define COL_PANEL      0x1082
#define COL_PANEL2     0x18E3
#define COL_BORDER     0x39E7

#define COL_WHITE      TFT_WHITE
#define COL_BLACK      TFT_BLACK

#define COL_GREY       0x8410
#define COL_LIGHTGREY  0xC618
#define COL_DARKGREY   0x4208

#define COL_BLUE       0x051F
#define COL_LIGHTBLUE  0x4D7F

#define COL_GREEN      0x07E0
#define COL_RED        0xF800

#define COL_ORANGE     0xFD20
#define COL_YELLOW     0xFFE0
#define COL_PURPLE     0xA81F

#define COL_BUTTON     0x2145


// ============================================================
//                           AUDIO
// ============================================================

#define AUDIO_BASE_FREQ 2000
#define AUDIO_RESOLUTION 8

#if ESP_ARDUINO_VERSION_MAJOR < 3
#define AUDIO_CHANNEL 0
#endif


// ============================================================
//                         UI STATE
// ============================================================

enum ScreenMode {

  SCREEN_BOOT,

  SCREEN_WIFI_SCAN,

  SCREEN_LOGIN,

  SCREEN_KEYBOARD,

  SCREEN_2FA,

  SCREEN_HOME,

  SCREEN_NOTIFICATIONS,

  SCREEN_EVENTS,

  SCREEN_GROUPS,

  SCREEN_DETAIL,

  SCREEN_TIME_SETTINGS,

  SCREEN_SESSION
};


ScreenMode currentScreen =
  SCREEN_BOOT;


// ============================================================
//                         KEYBOARD
// ============================================================

enum KeyboardMode {

  KB_LOWER,

  KB_UPPER,

  KB_SYMBOL1,

  KB_SYMBOL2
};


KeyboardMode keyboardMode =
  KB_LOWER;


enum InputTarget {

  INPUT_USERNAME,

  INPUT_PASSWORD,

  INPUT_WIFI_PASSWORD,

  INPUT_WIFI_SSID
};


InputTarget inputTarget =
  INPUT_USERNAME;


String loginUsername = "";
String loginPassword = "";
String keyboardBuffer = "";


// ============================================================
//                         WI-FI SETUP
// ============================================================

String savedWiFiSSID = "";
String savedWiFiPassword = "";
String selectedWiFiSSID = "";

const int MAX_WIFI_NETWORKS =
  12;

String scannedWiFiSSIDs[
  MAX_WIFI_NETWORKS
];

int32_t scannedWiFiRSSI[
  MAX_WIFI_NETWORKS
];

bool scannedWiFiSecured[
  MAX_WIFI_NETWORKS
];

int scannedWiFiCount =
  0;

int wifiListOffset =
  0;

bool wifiSetupFromAdvanced =
  false;


// ============================================================
//                            2FA
// ============================================================

enum TwoFactorType {

  TWOFA_NONE,

  TWOFA_TOTP,

  TWOFA_EMAIL
};


TwoFactorType required2FA =
  TWOFA_NONE;


String twoFactorCode = "";


// ============================================================
//                         SESSION
// ============================================================

String authCookie = "";
String twoFactorCookie = "";

String currentDisplayName = "";

bool sessionValid =
  false;


// ============================================================
//                         PIPELINE
// ============================================================

bool pipelineStarted =
  false;

bool pipelineConnected =
  false;


// Has the WebSocket EVER successfully connected this boot?

bool pipelineEverConnected =
  false;


unsigned long pipelineConnectedAt =
  0;

unsigned long pipelineLastMessage =
  0;

unsigned long pipelineLastDisconnect =
  0;


unsigned long pipelineConnectCount =
  0;

unsigned long pipelineDisconnectCount =
  0;


String pipelinePath = "";
String pipelineHeaders = "";

String lastPipelineError = "";


// ============================================================
//                       NOTIFICATIONS
// ============================================================

enum NotificationClass {

  NOTICE_INVITE,

  NOTICE_FRIEND,

  NOTICE_BOOP,

  NOTICE_SYSTEM
};


struct PagerNotification {

  NotificationClass noticeClass;

  String pipelineType;

  String subtype;

  String sender;

  String message;

  time_t receivedEpoch;

  unsigned long receivedAt;

  bool unread;
};


const int MAX_NOTIFICATIONS =
  8;


PagerNotification notifications[
  MAX_NOTIFICATIONS
];


int notificationCount =
  0;


// ============================================================
//                    EVENT TIMELINES
// ============================================================

struct TimelineEntry {

  String type;

  String subject;

  String detail;

  time_t receivedEpoch;

  unsigned long receivedAt;
};


const int MAX_TIMELINE_EVENTS =
  15;

const int MAX_GROUP_EVENTS =
  15;


TimelineEntry timelineEvents[
  MAX_TIMELINE_EVENTS
];

TimelineEntry groupEvents[
  MAX_GROUP_EVENTS
];


int timelineEventCount =
  0;

int groupEventCount =
  0;

int eventScrollOffset =
  0;

int groupScrollOffset =
  0;


// ============================================================
//                    DETAIL VIEW STATE
// ============================================================

ScreenMode detailReturnScreen =
  SCREEN_HOME;

String detailType = "";
String detailSubject = "";
String detailMessage = "";

time_t detailEpoch =
  0;

unsigned long detailReceivedAt =
  0;

int detailScrollOffset =
  0;

int detailLineCount =
  0;

String detailLines[24];


// ============================================================
//                         TIME ZONES
// ============================================================

struct TimeZoneOption {

  const char* name;
  const char* rule;
};


const TimeZoneOption TIME_ZONES[] = {

  {"UNITED KINGDOM", "GMT0BST,M3.5.0/1,M10.5.0"},
  {"UTC", "UTC0"},
  {"US EASTERN", "EST5EDT,M3.2.0,M11.1.0"},
  {"US CENTRAL", "CST6CDT,M3.2.0,M11.1.0"},
  {"US MOUNTAIN", "MST7MDT,M3.2.0,M11.1.0"},
  {"US PACIFIC", "PST8PDT,M3.2.0,M11.1.0"},
  {"CENTRAL EUROPE", "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"AUSTRALIA EAST", "AEST-10AEDT,M10.1.0,M4.1.0/3"}
};


const int TIME_ZONE_COUNT =

  sizeof(TIME_ZONES) /
  sizeof(TIME_ZONES[0]);


int selectedTimeZone =
  0;

int timeZoneListOffset =
  0;


// Remember recently seen friend IDs so an offline packet that
// contains only userId can still show the friend's name.

const int MAX_FRIEND_NAME_CACHE =
  32;

String friendCacheIds[
  MAX_FRIEND_NAME_CACHE
];

String friendCacheNames[
  MAX_FRIEND_NAME_CACHE
];

int friendCacheCount =
  0;


// ============================================================
//                         TOUCH
// ============================================================

bool touchWasDown =
  false;

int touchStartX =
  0;

int touchStartY =
  0;

int touchLastX =
  0;

int touchLastY =
  0;

ScreenMode touchStartScreen =
  SCREEN_BOOT;

bool touchWakeConsumed =
  false;

bool screenBacklightOn =
  true;

unsigned long lastScreenTouch =
  0;

unsigned long notificationWakeUntil =
  0;

bool logoutArmed =
  false;


// ============================================================
//                         BUTTON
// ============================================================

struct Button {

  int x;

  int y;

  int w;

  int h;
};


// ============================================================
//                      BUTTON LAYOUT
// ============================================================

Button usernameButton =
  {15, 55, 290, 42};

Button passwordButton =
  {15, 109, 290, 42};

Button loginButton =
  {165, 172, 140, 48};

Button clearLoginButton =
  {15, 172, 140, 48};


// Home

Button notificationsButton =
  {10, 130, 145, 43};

Button eventsButton =
  {165, 130, 145, 43};

Button groupsButton =
  {10, 184, 145, 43};

Button advancedButton =
  {165, 184, 145, 43};


// Info / Advanced

Button reconnectButton =
  {7, 163, 145, 32};

Button changeWiFiButton =
  {168, 163, 145, 32};

Button timeSettingsButton =
  {7, 202, 145, 32};

Button logoutButton =
  {168, 202, 145, 32};


// Generic back

Button backButton =
  {5, 4, 58, 22};


// ============================================================
//                     FORWARD DECLARATIONS
// ============================================================

void drawHomeScreen();
void drawNotificationsScreen();
void drawEventsScreen();
void drawGroupsScreen();
void drawSessionScreen();
void drawWiFiScanScreen();
void drawDetailScreen();
void drawTimeSettingsScreen();

void startPipeline();
bool connectWiFi();
void beginWiFiScan(bool fromAdvanced);
bool connectWiFiCredentials(
  const String &ssid,
  const String &password
);
void continueAfterWiFiConnection();
void applyTimeSettings();


// ============================================================
//                       DRAW HELPERS
// ============================================================

void centeredText(
  const String &text,
  int x,
  int y,
  int font,
  uint16_t colour
) {

  tft.setTextDatum(
    MC_DATUM
  );


  tft.setTextColor(
    colour
  );


  tft.drawString(
    text,
    x,
    y,
    font
  );


  tft.setTextDatum(
    TL_DATUM
  );
}


void drawButton(
  Button button,
  const String &label,
  uint16_t colour,
  uint16_t textColour = COL_WHITE,
  int font = 2
) {

  tft.fillRoundRect(
    button.x,
    button.y,
    button.w,
    button.h,
    5,
    colour
  );


  tft.drawRoundRect(
    button.x,
    button.y,
    button.w,
    button.h,
    5,
    COL_BORDER
  );


  centeredText(
    label,
    button.x + button.w / 2,
    button.y + button.h / 2,
    font,
    textColour
  );
}


bool hit(
  int x,
  int y,
  Button button
) {

  return
    x >= button.x &&
    x <= button.x + button.w &&
    y >= button.y &&
    y <= button.y + button.h;
}


void drawBackButton() {

  drawButton(
    backButton,
    "< BACK",
    COL_BUTTON,
    COL_WHITE,
    1
  );
}


// ============================================================
//                         RGB LED
// ============================================================

void rgbOff() {

  digitalWrite(
    LED_RED_PIN,
    HIGH
  );

  digitalWrite(
    LED_GREEN_PIN,
    HIGH
  );

  digitalWrite(
    LED_BLUE_PIN,
    HIGH
  );
}


void rgb(
  bool red,
  bool green,
  bool blue
) {

  digitalWrite(
    LED_RED_PIN,
    red ? LOW : HIGH
  );

  digitalWrite(
    LED_GREEN_PIN,
    green ? LOW : HIGH
  );

  digitalWrite(
    LED_BLUE_PIN,
    blue ? LOW : HIGH
  );
}


void flashNotificationLED(
  NotificationClass type
) {

  for (
    int i = 0;
    i < 2;
    i++
  ) {

    switch (type) {

      case NOTICE_INVITE:

        rgb(
          false,
          false,
          true
        );

        break;


      case NOTICE_FRIEND:

        rgb(
          false,
          true,
          false
        );

        break;


      case NOTICE_BOOP:

        rgb(
          true,
          false,
          true
        );

        break;


      case NOTICE_SYSTEM:

        rgb(
          true,
          true,
          false
        );

        break;
    }


    delay(80);

    rgbOff();

    delay(40);
  }
}


// ============================================================
//                          AUDIO
// ============================================================

void setupAudio() {

#if ESP_ARDUINO_VERSION_MAJOR >= 3

  ledcAttach(
    AUDIO_PIN,
    AUDIO_BASE_FREQ,
    AUDIO_RESOLUTION
  );

#else

  ledcSetup(
    AUDIO_CHANNEL,
    AUDIO_BASE_FREQ,
    AUDIO_RESOLUTION
  );


  ledcAttachPin(
    AUDIO_PIN,
    AUDIO_CHANNEL
  );

#endif
}


void setToneFrequency(
  uint16_t frequency
) {

#if ESP_ARDUINO_VERSION_MAJOR >= 3

  ledcWriteTone(
    AUDIO_PIN,
    frequency
  );

#else

  ledcWriteTone(
    AUDIO_CHANNEL,
    frequency
  );

#endif
}


void pagerTone(
  uint16_t frequency,
  uint16_t duration
) {

  setToneFrequency(
    frequency
  );


  delay(
    duration
  );


  setToneFrequency(
    0
  );


  delay(
    15
  );
}


void playNotificationSound(
  NotificationClass type
) {

  switch (type) {

    case NOTICE_INVITE:

      pagerTone(
        1050,
        80
      );


      pagerTone(
        1320,
        130
      );

      break;


    case NOTICE_FRIEND:

      pagerTone(
        780,
        70
      );


      pagerTone(
        990,
        70
      );


      pagerTone(
        1320,
        130
      );

      break;


    case NOTICE_BOOP:

      pagerTone(
        1550,
        50
      );


      pagerTone(
        1950,
        45
      );

      break;


    case NOTICE_SYSTEM:

      pagerTone(
        880,
        90
      );

      break;
  }
}


// ============================================================
//                       TOUCH READING
// ============================================================

bool readTouch(
  int &x,
  int &y
) {

  if (
    !ts.touched()
  ) {

    return false;
  }


  TS_Point point =
    ts.getPoint();


  x =
    constrain(

      map(

        point.x,

        TOUCH_MIN_X,

        TOUCH_MAX_X,

        0,

        319

      ),

      0,

      319
    );


  y =
    constrain(

      map(

        point.y,

        TOUCH_MIN_Y,

        TOUCH_MAX_Y,

        0,

        239

      ),

      0,

      239
    );


  return true;
}


// ============================================================
//                       URL ENCODE
// ============================================================

String urlEncode(
  const String &input
) {

  const char hex[] =
    "0123456789ABCDEF";


  String output;


  output.reserve(
    input.length() * 3
  );


  for (
    size_t i = 0;
    i < input.length();
    i++
  ) {

    uint8_t c =
      (uint8_t)input[i];


    bool safe =

      (c >= 'A' && c <= 'Z') ||

      (c >= 'a' && c <= 'z') ||

      (c >= '0' && c <= '9') ||

      c == '-' ||

      c == '_' ||

      c == '.' ||

      c == '~';


    if (safe) {

      output +=
        (char)c;

    } else {

      output +=
        '%';


      output +=
        hex[
          (c >> 4) & 0x0F
        ];


      output +=
        hex[
          c & 0x0F
        ];
    }
  }


  return output;
}


// ============================================================
//                         BASE64
// ============================================================

String base64Encode(
  const String &input
) {

  size_t outputLength =
    0;


  size_t bufferSize =

    4 *

    (
      (input.length() + 2) / 3
    )

    + 1;


  unsigned char* output =

    new unsigned char[
      bufferSize
    ];


  if (!output) {

    return "";
  }


  int result =

    mbedtls_base64_encode(

      output,

      bufferSize,

      &outputLength,

      (const unsigned char*)
        input.c_str(),

      input.length()
    );


  if (
    result != 0
  ) {

    delete[] output;

    return "";
  }


  output[
    outputLength
  ] = '\0';


  String resultString =
    String(
      (char*)output
    );


  delete[] output;


  return resultString;
}


// ============================================================
//                         COOKIES
// ============================================================

String extractCookie(
  const String &header,
  const String &cookieName
) {

  String needle =
    cookieName + "=";


  int start =
    header.indexOf(
      needle
    );


  if (
    start < 0
  ) {

    return "";
  }


  start +=
    needle.length();


  int end =
    header.indexOf(
      ';',
      start
    );


  if (
    end < 0
  ) {

    end =
      header.length();
  }


  return header.substring(
    start,
    end
  );
}


String buildCookieHeader() {

  String result;


  if (
    authCookie.length()
  ) {

    result =
      "auth=" +
      authCookie;
  }


  if (
    twoFactorCookie.length()
  ) {

    if (
      result.length()
    ) {

      result +=
        "; ";
    }


    result +=

      "twoFactorAuth=" +

      twoFactorCookie;
  }


  return result;
}


// ============================================================
//                      SESSION STORAGE
// ============================================================

void loadWiFiSettings() {

  preferences.begin(
    "vrcpager",
    false
  );


  savedWiFiSSID =

    preferences.getString(
      "wifi_ssid",
      ""
    );


  savedWiFiPassword =

    preferences.getString(
      "wifi_pass",
      ""
    );


  selectedTimeZone =

    preferences.getUInt(
      "timezone",
      0
    );


  selectedTimeZone =

    constrain(
      selectedTimeZone,
      0,
      TIME_ZONE_COUNT - 1
    );


  preferences.end();
}


void saveWiFiSettings() {

  preferences.begin(
    "vrcpager",
    false
  );


  preferences.putString(
    "wifi_ssid",
    savedWiFiSSID
  );


  preferences.putString(
    "wifi_pass",
    savedWiFiPassword
  );


  preferences.end();
}


void saveTimeSettings() {

  preferences.begin(
    "vrcpager",
    false
  );


  preferences.putUInt(
    "timezone",
    selectedTimeZone
  );


  preferences.end();
}


void applyTimeSettings() {

  configTzTime(
    TIME_ZONES[
      selectedTimeZone
    ].rule,
    "pool.ntp.org",
    "time.nist.gov"
  );


  Serial.print(
    "[TIME] Zone: "
  );


  Serial.println(
    TIME_ZONES[
      selectedTimeZone
    ].name
  );
}


bool clockIsValid() {

  return
    time(nullptr) >
    1700000000;
}


String formatTimestamp(
  time_t epoch,
  const char* format
) {

  if (
    epoch <=
    1700000000
  ) {

    return "";
  }


  struct tm localTime;


  localtime_r(
    &epoch,
    &localTime
  );


  char buffer[32];


  strftime(
    buffer,
    sizeof(buffer),
    format,
    &localTime
  );


  return String(
    buffer
  );
}


String eventTimeLabel(
  time_t epoch,
  unsigned long receivedAt
) {

  String timestamp =
    formatTimestamp(
      epoch,
      "%H:%M"
    );


  if (
    timestamp.length()
  ) {

    return timestamp;
  }


  return
    String(
      (
        millis() -
        receivedAt
      ) / 1000
    ) + "s";
}


void clearWiFiSettings() {

  preferences.begin(
    "vrcpager",
    false
  );


  preferences.remove(
    "wifi_ssid"
  );


  preferences.remove(
    "wifi_pass"
  );


  preferences.end();


  savedWiFiSSID = "";
  savedWiFiPassword = "";
  selectedWiFiSSID = "";
}

void loadSession() {

  preferences.begin(
    "vrcpager",
    false
  );


  authCookie =

    preferences.getString(
      "auth",
      ""
    );


  twoFactorCookie =

    preferences.getString(
      "twofa",
      ""
    );


  loginUsername =

    preferences.getString(
      "username",
      ""
    );


  preferences.end();
}


void saveSession() {

  preferences.begin(
    "vrcpager",
    false
  );


  preferences.putString(
    "auth",
    authCookie
  );


  preferences.putString(
    "twofa",
    twoFactorCookie
  );


  preferences.putString(
    "username",
    loginUsername
  );


  preferences.end();
}


void clearStoredSession() {

  preferences.begin(
    "vrcpager",
    false
  );


  preferences.remove(
    "auth"
  );


  preferences.remove(
    "twofa"
  );


  preferences.end();


  authCookie = "";

  twoFactorCookie = "";

  currentDisplayName = "";

  sessionValid = false;
}


// ============================================================
//                     SENSITIVE MEMORY
// ============================================================

void wipePassword() {

  for (
    size_t i = 0;
    i < loginPassword.length();
    i++
  ) {

    loginPassword.setCharAt(
      i,
      '\0'
    );
  }


  loginPassword = "";
}


void wipe2FA() {

  for (
    size_t i = 0;
    i < twoFactorCode.length();
    i++
  ) {

    twoFactorCode.setCharAt(
      i,
      '\0'
    );
  }


  twoFactorCode = "";
}


// ============================================================
//                         STATUS UI
// ============================================================

void showStatus(
  String title,
  String message,
  uint16_t colour
) {

  tft.fillScreen(
    COL_BG
  );


  centeredText(
    title,
    160,
    78,
    3,
    colour
  );


  centeredText(
    message,
    160,
    125,
    2,
    COL_WHITE
  );


  centeredText(
    "Please wait...",
    160,
    166,
    1,
    COL_GREY
  );
}


// ============================================================
//                       WI-FI SETUP UI
// ============================================================

void drawWiFiScanScreen() {

  currentScreen =
    SCREEN_WIFI_SCAN;


  tft.fillScreen(
    COL_BG
  );


  centeredText(
    "SELECT WI-FI",
    160,
    14,
    2,
    COL_LIGHTBLUE
  );


  if (
    wifiSetupFromAdvanced
  ) {

    drawBackButton();
  }


  if (
    scannedWiFiCount ==
    0
  ) {

    centeredText(
      "NO NETWORKS FOUND",
      160,
      105,
      2,
      COL_GREY
    );
  }


  int visibleRows =
    4;


  int maximumOffset =

    max(
      0,
      scannedWiFiCount -
        visibleRows
    );


  wifiListOffset =

    constrain(
      wifiListOffset,
      0,
      maximumOffset
    );


  for (
    int row = 0;
    row < visibleRows;
    row++
  ) {

    int index =
      wifiListOffset +
      row;


    if (
      index >=
      scannedWiFiCount
    ) {

      break;
    }


    int y =
      35 +
      row * 41;


    tft.fillRoundRect(
      8,
      y,
      304,
      36,
      5,
      COL_PANEL
    );


    String name =
      scannedWiFiSSIDs[index];


    if (
      name.length() >
      28
    ) {

      name =
        name.substring(
          0,
          25
        ) + "...";
    }


    tft.setTextColor(
      COL_WHITE
    );


    tft.drawString(
      name,
      16,
      y + 6,
      2
    );


    String strength =
      String(
        scannedWiFiRSSI[index]
      ) + " dBm";


    if (
      scannedWiFiSecured[index]
    ) {

      strength +=
        "  LOCK";
    }


    tft.setTextDatum(
      TR_DATUM
    );


    tft.setTextColor(
      COL_GREY
    );


    tft.drawString(
      strength,
      304,
      y + 12,
      1
    );


    tft.setTextDatum(
      TL_DATUM
    );
  }


  drawButton(
    {5, 204, 72, 31},
    "RESCAN",
    COL_BLUE,
    COL_WHITE,
    1
  );


  drawButton(
    {82, 204, 75, 31},
    "MANUAL",
    COL_DARKGREY,
    COL_WHITE,
    1
  );


  drawButton(
    {162, 204, 73, 31},
    "NEWER",
    COL_BUTTON,
    COL_WHITE,
    1
  );


  drawButton(
    {240, 204, 75, 31},
    "OLDER",
    COL_BUTTON,
    COL_WHITE,
    1
  );
}


void beginWiFiScan(
  bool fromAdvanced
) {

  wifiSetupFromAdvanced =
    fromAdvanced;


  wifiListOffset =
    0;


  showStatus(
    "WI-FI SETUP",
    "Scanning networks...",
    COL_LIGHTBLUE
  );


  WiFi.mode(
    WIFI_STA
  );


  int found =

    WiFi.scanNetworks(
      false,
      true
    );


  scannedWiFiCount =
    0;


  for (
    int i = 0;
    i < found &&
      scannedWiFiCount <
        MAX_WIFI_NETWORKS;
    i++
  ) {

    String ssid =
      WiFi.SSID(i);


    if (
      ssid.length() ==
      0
    ) {

      continue;
    }


    bool duplicate =
      false;


    for (
      int j = 0;
      j < scannedWiFiCount;
      j++
    ) {

      if (
        scannedWiFiSSIDs[j] ==
        ssid
      ) {

        duplicate =
          true;

        break;
      }
    }


    if (
      duplicate
    ) {

      continue;
    }


    scannedWiFiSSIDs[
      scannedWiFiCount
    ] = ssid;


    scannedWiFiRSSI[
      scannedWiFiCount
    ] = WiFi.RSSI(i);


    scannedWiFiSecured[
      scannedWiFiCount
    ] =

      WiFi.encryptionType(i) !=
      WIFI_AUTH_OPEN;


    scannedWiFiCount++;
  }


  WiFi.scanDelete();


  drawWiFiScanScreen();
}


// ============================================================
//                         LOGIN UI
// ============================================================

String maskedPassword() {

  String result;


  for (
    size_t i = 0;
    i < loginPassword.length();
    i++
  ) {

    result += '*';
  }


  return result;
}


void drawLoginScreen() {

  currentScreen =
    SCREEN_LOGIN;


  tft.fillScreen(
    COL_BG
  );


  centeredText(
    "VRCHAT LOGIN",
    160,
    20,
    2,
    COL_LIGHTBLUE
  );


  tft.setTextColor(
    COL_GREY
  );


  tft.drawString(
    "Username",
    18,
    43,
    1
  );


  drawButton(

    usernameButton,

    loginUsername.length()
      ? loginUsername
      : "TAP TO ENTER",

    COL_PANEL
  );


  tft.setTextColor(
    COL_GREY
  );


  tft.drawString(
    "Password",
    18,
    97,
    1
  );


  drawButton(

    passwordButton,

    loginPassword.length()
      ? maskedPassword()
      : "TAP TO ENTER",

    COL_PANEL
  );


  drawButton(
    clearLoginButton,
    "CLEAR",
    COL_DARKGREY
  );


  drawButton(
    loginButton,
    "LOGIN",
    COL_BLUE
  );
}


// ============================================================
//                     KEYBOARD DRAWING
// ============================================================

void drawCharacterKey(
  int x,
  int y,
  int w,
  int h,
  char character
) {

  Button key =
    {x, y, w, h};


  String label;

  label +=
    character;


  drawButton(
    key,
    label,
    COL_BUTTON
  );
}


void drawKeyboardRow(
  const String &characters,
  int y
) {

  int count =
    characters.length();


  if (
    count == 0
  ) {

    return;
  }


  int keyWidth =
    29;


  int gap =
    2;


  int totalWidth =

    count * keyWidth +

    (count - 1) * gap;


  int startX =

    (
      SCREEN_WIDTH -
      totalWidth
    ) / 2;


  for (
    int i = 0;
    i < count;
    i++
  ) {

    drawCharacterKey(

      startX +
        i *
        (
          keyWidth +
          gap
        ),

      y,

      keyWidth,

      31,

      characters[i]
    );
  }
}


String currentKeyboardValue() {

  if (
    inputTarget ==
      INPUT_PASSWORD ||

    inputTarget ==
      INPUT_WIFI_PASSWORD
  ) {

    String masked;


    for (
      size_t i = 0;
      i < keyboardBuffer.length();
      i++
    ) {

      masked += '*';
    }


    return masked;
  }


  return keyboardBuffer;
}


void drawKeyboard() {

  currentScreen =
    SCREEN_KEYBOARD;


  tft.fillScreen(
    COL_BG
  );


  String keyboardTitle;


  if (
    inputTarget ==
    INPUT_USERNAME
  ) {

    keyboardTitle =
      "ENTER USERNAME";

  } else if (
    inputTarget ==
    INPUT_WIFI_SSID
  ) {

    keyboardTitle =
      "ENTER WI-FI NAME";

  } else if (
    inputTarget ==
    INPUT_WIFI_PASSWORD
  ) {

    keyboardTitle =
      "WI-FI PASSWORD";

  } else {

    keyboardTitle =
      "ENTER PASSWORD";
  }


  centeredText(

    keyboardTitle,

    160,
    12,
    2,
    COL_LIGHTBLUE
  );


  tft.fillRoundRect(
    8,
    29,
    304,
    34,
    4,
    COL_PANEL
  );


  String shown =
    currentKeyboardValue();


  if (
    shown.length() >
    29
  ) {

    shown =
      shown.substring(
        shown.length() - 29
      );
  }


  tft.setTextColor(
    COL_WHITE
  );


  tft.drawString(
    shown + "_",
    14,
    40,
    2
  );


  String row1;
  String row2;
  String row3;


  switch (
    keyboardMode
  ) {

    case KB_LOWER:

      row1 =
        "qwertyuiop";

      row2 =
        "asdfghjkl";

      row3 =
        "zxcvbnm";

      break;


    case KB_UPPER:

      row1 =
        "QWERTYUIOP";

      row2 =
        "ASDFGHJKL";

      row3 =
        "ZXCVBNM";

      break;


    case KB_SYMBOL1:

      row1 =
        "1234567890";

      row2 =
        "!@#$%^&*()";

      row3 =
        "-_=+[]{}";

      break;


    case KB_SYMBOL2:

      row1 =
        "`~\\|/<>?";

      row2 =
        ".,:;\"'";

      row3 =
        "";

      break;
  }


  drawKeyboardRow(
    row1,
    70
  );


  drawKeyboardRow(
    row2,
    106
  );


  drawKeyboardRow(
    row3,
    142
  );


  Button modeButton =
    {5, 183, 58, 48};

  Button spaceButton =
    {67, 183, 94, 48};

  Button deleteButton =
    {165, 183, 69, 48};

  Button okButton =
    {238, 183, 77, 48};


  String modeLabel;


  switch (
    keyboardMode
  ) {

    case KB_LOWER:

      modeLabel =
        "SHIFT";

      break;


    case KB_UPPER:

      modeLabel =
        "123";

      break;


    case KB_SYMBOL1:

      modeLabel =
        "#+=";

      break;


    case KB_SYMBOL2:

      modeLabel =
        "abc";

      break;
  }


  drawButton(
    modeButton,
    modeLabel,
    COL_DARKGREY,
    COL_WHITE,
    1
  );


  drawButton(
    spaceButton,
    "SPACE",
    COL_BUTTON,
    COL_WHITE,
    1
  );


  drawButton(
    deleteButton,
    "DEL",
    COL_RED,
    COL_WHITE,
    1
  );


  drawButton(
    okButton,
    "OK",
    COL_GREEN,
    COL_BLACK,
    2
  );
}
// ============================================================
//                    KEYBOARD TOUCH
// ============================================================

bool processKeyboardRowTouch(
  String characters,
  int rowY,
  int x,
  int y
) {

  if (
    y < rowY ||
    y > rowY + 31
  ) {

    return false;
  }


  int count =
    characters.length();


  if (
    count == 0
  ) {

    return false;
  }


  int keyWidth =
    29;


  int gap =
    2;


  int totalWidth =

    count * keyWidth +

    (count - 1) * gap;


  int startX =

    (
      SCREEN_WIDTH -
      totalWidth
    ) / 2;


  for (
    int i = 0;
    i < count;
    i++
  ) {

    int keyX =

      startX +

      i *
      (
        keyWidth +
        gap
      );


    if (
      x >= keyX &&
      x <= keyX + keyWidth
    ) {

      if (
        keyboardBuffer.length() <
        80
      ) {

        keyboardBuffer +=
          characters[i];
      }


      drawKeyboard();


      return true;
    }
  }


  return false;
}


// ============================================================
//                          2FA UI
// ============================================================

void draw2FAScreen() {

  currentScreen =
    SCREEN_2FA;


  tft.fillScreen(
    COL_BG
  );


  centeredText(

    required2FA ==
      TWOFA_EMAIL
      ?
      "EMAIL VERIFICATION"
      :
      "TWO-FACTOR LOGIN",

    160,
    16,
    2,
    COL_LIGHTBLUE
  );


  String shown;


  for (
    size_t i = 0;
    i < twoFactorCode.length();
    i++
  ) {

    shown +=
      twoFactorCode[i];


    if (
      i == 2
    ) {

      shown += ' ';
    }
  }


  centeredText(

    shown.length()
      ? shown
      : "_ _ _   _ _ _",

    160,
    49,
    3,
    COL_WHITE
  );


  const char keys[9] = {

    '1','2','3',
    '4','5','6',
    '7','8','9'
  };


  int index =
    0;


  for (
    int row = 0;
    row < 3;
    row++
  ) {

    for (
      int column = 0;
      column < 3;
      column++
    ) {

      Button key = {

        54 +
          column * 72,

        74 +
          row * 43,

        62,

        36
      };


      String label;

      label +=
        keys[index++];


      drawButton(
        key,
        label,
        COL_BUTTON
      );
    }
  }


  drawButton(
    {18,205,84,30},
    "DEL",
    COL_RED,
    COL_WHITE,
    1
  );


  drawButton(
    {118,205,84,30},
    "0",
    COL_BUTTON,
    COL_WHITE,
    2
  );


  drawButton(
    {218,205,84,30},
    "VERIFY",
    COL_GREEN,
    COL_BLACK,
    1
  );
}


// ============================================================
//                    NOTIFICATION HELPERS
// ============================================================

uint16_t notificationColour(
  NotificationClass type
) {

  switch (type) {

    case NOTICE_INVITE:

      return COL_LIGHTBLUE;


    case NOTICE_FRIEND:

      return COL_GREEN;


    case NOTICE_BOOP:

      return COL_PURPLE;


    case NOTICE_SYSTEM:

      return COL_YELLOW;
  }


  return COL_WHITE;
}


String notificationName(
  NotificationClass type
) {

  switch (type) {

    case NOTICE_INVITE:

      return "INVITE";


    case NOTICE_FRIEND:

      return "FRIEND";


    case NOTICE_BOOP:

      return "BOOP";


    case NOTICE_SYSTEM:

      return "SYSTEM";
  }


  return "NOTICE";
}


int unreadCount() {

  int total =
    0;


  for (
    int i = 0;
    i < notificationCount;
    i++
  ) {

    if (
      notifications[i].unread
    ) {

      total++;
    }
  }


  return total;
}


// ============================================================
//               SMALL PIPELINE STATUS BADGE
// ============================================================

void drawPipelineBadge() {

  // Only alter this small region.
  // This is the main anti-flicker change.

  if (
    currentScreen !=
    SCREEN_HOME
  ) {

    return;
  }


  tft.fillRect(
    210,
    25,
    105,
    20,
    COL_BG
  );


  uint16_t colour;

  String label;


  if (
    WiFi.status() !=
    WL_CONNECTED
  ) {

    colour =
      COL_RED;

    label =
      "NO WI-FI";

  } else if (
    pipelineConnected
  ) {

    colour =
      COL_GREEN;

    label =
      "PIPELINE";

  } else {

    colour =
      COL_ORANGE;

    label =
      "RECONNECTING";
  }


  tft.fillCircle(
    304,
    34,
    5,
    colour
  );


  tft.setTextDatum(
    TR_DATUM
  );


  tft.setTextColor(
    colour,
    COL_BG
  );


  tft.drawString(
    label,
    294,
    29,
    1
  );


  tft.setTextDatum(
    TL_DATUM
  );
}


// ============================================================
//                      TIMELINE STORAGE
// ============================================================

void insertTimelineEntry(
  TimelineEntry entries[],
  int &count,
  int maximum,
  String type,
  String subject,
  String detail
) {

  int endIndex =
    min(
      count,
      maximum - 1
    );


  for (
    int i = endIndex;
    i > 0;
    i--
  ) {

    entries[i] =
      entries[i - 1];
  }


  entries[0].type =
    type;

  entries[0].subject =
    subject;

  entries[0].detail =
    detail;

  entries[0].receivedEpoch =
    clockIsValid()
      ? time(nullptr)
      : 0;

  entries[0].receivedAt =
    millis();


  if (
    count <
    maximum
  ) {

    count++;
  }
}


bool isGroupActivity(
  String value
) {

  value.toLowerCase();


  return
    value.indexOf(
      "group"
    ) >= 0;
}


// ============================================================
//                    SCREEN PROTECTION
// ============================================================

bool timeReached(
  unsigned long now,
  unsigned long target
) {

  return
    (long)(
      now - target
    ) >= 0;
}


void setScreenBacklight(
  bool enabled
) {

  if (
    screenBacklightOn ==
    enabled
  ) {

    return;
  }


  screenBacklightOn =
    enabled;


  digitalWrite(
    TFT_BACKLIGHT,
    enabled
      ? HIGH
      : LOW
  );


  Serial.println(
    enabled
      ? "[SCREEN] Backlight on"
      : "[SCREEN] Backlight off"
  );
}


void wakeScreenForNotification() {

  unsigned long now =
    millis();


  notificationWakeUntil =
    now +
    NOTIFICATION_WAKE_MS;


  setScreenBacklight(
    true
  );
}


void serviceScreenTimeout() {

  if (
    !screenBacklightOn
  ) {

    return;
  }


  unsigned long now =
    millis();


  bool touchTimeoutReached =

    now -
      lastScreenTouch >=
      SCREEN_TIMEOUT_MS;


  bool notificationHoldFinished =

    notificationWakeUntil ==
      0 ||

    timeReached(
      now,
      notificationWakeUntil
    );


  if (
    touchTimeoutReached &&
    notificationHoldFinished
  ) {

    notificationWakeUntil =
      0;


    setScreenBacklight(
      false
    );
  }
}


void recordTimelineEvent(
  String type,
  String subject,
  String detail,
  bool groupRelated
) {

  if (
    groupRelated
  ) {

    insertTimelineEntry(
      groupEvents,
      groupEventCount,
      MAX_GROUP_EVENTS,
      type,
      subject,
      detail
    );

  } else {

    insertTimelineEntry(
      timelineEvents,
      timelineEventCount,
      MAX_TIMELINE_EVENTS,
      type,
      subject,
      detail
    );
  }


  if (
    currentScreen ==
    SCREEN_EVENTS
  ) {

    drawEventsScreen();

  } else if (
    currentScreen ==
    SCREEN_GROUPS
  ) {

    drawGroupsScreen();
  }
}


// ============================================================
//                     ADD NOTIFICATION
// ============================================================

void addNotification(
  NotificationClass noticeClass,
  String pipelineType,
  String subtype,
  String sender,
  String message
) {

  wakeScreenForNotification();


  bool groupRelated =

    isGroupActivity(
      pipelineType +
      " " +
      subtype +
      " " +
      message
    );


  if (
    groupRelated
  ) {

    recordTimelineEvent(
      subtype,
      sender,
      message,
      true
    );


    Serial.println();
    Serial.println(
      "======================================"
    );
    Serial.println(
      "VRCHAT GROUP ACTIVITY"
    );
    Serial.print(
      "Subtype: "
    );
    Serial.println(
      subtype
    );
    Serial.print(
      "Group: "
    );
    Serial.println(
      sender
    );
    Serial.print(
      "Message: "
    );
    Serial.println(
      message
    );
    Serial.println(
      "======================================"
    );


    flashNotificationLED(
      noticeClass
    );


    playNotificationSound(
      noticeClass
    );


    return;
  }

  int endIndex =

    min(

      notificationCount,

      MAX_NOTIFICATIONS - 1
    );


  for (
    int i = endIndex;
    i > 0;
    i--
  ) {

    notifications[i] =
      notifications[
        i - 1
      ];
  }


  notifications[0].noticeClass =
    noticeClass;


  notifications[0].pipelineType =
    pipelineType;


  notifications[0].subtype =
    subtype;


  notifications[0].sender =
    sender;


  notifications[0].message =
    message;


  notifications[0].receivedEpoch =
    clockIsValid()
      ? time(nullptr)
      : 0;


  notifications[0].receivedAt =
    millis();


  notifications[0].unread =
    true;


  if (
    notificationCount <
    MAX_NOTIFICATIONS
  ) {

    notificationCount++;
  }


  recordTimelineEvent(
    subtype,
    sender,
    message,
    false
  );


  Serial.println();

  Serial.println(
    "======================================"
  );

  Serial.println(
    "VRCHAT PIPELINE NOTIFICATION"
  );


  Serial.print(
    "Pipeline type: "
  );

  Serial.println(
    pipelineType
  );


  Serial.print(
    "Subtype: "
  );

  Serial.println(
    subtype
  );


  Serial.print(
    "Sender: "
  );

  Serial.println(
    sender
  );


  Serial.print(
    "Message: "
  );

  Serial.println(
    message
  );


  Serial.println(
    "======================================"
  );


  flashNotificationLED(
    noticeClass
  );


  playNotificationSound(
    noticeClass
  );


  if (
    currentScreen ==
    SCREEN_HOME
  ) {

    drawHomeScreen();

  } else if (
    currentScreen ==
    SCREEN_NOTIFICATIONS
  ) {

    drawNotificationsScreen();
  }
}


// ============================================================
//                       HOME SCREEN
// ============================================================

void drawHomeScreen() {

  currentScreen =
    SCREEN_HOME;


  tft.fillScreen(
    COL_BG
  );


  centeredText(
    "VRCHAT PAGER",
    160,
    14,
    2,
    COL_LIGHTBLUE
  );


  tft.setTextColor(
    COL_GREY
  );


  String displayName =
    currentDisplayName;


  if (
    displayName.length() >
    25
  ) {

    displayName =
      displayName.substring(
        0,
        22
      ) + "...";
  }


  tft.drawString(
    displayName,
    12,
    31,
    1
  );


  drawPipelineBadge();


  tft.fillRoundRect(
    10,
    49,
    300,
    69,
    7,
    COL_PANEL
  );


  tft.drawRoundRect(
    10,
    49,
    300,
    69,
    7,
    COL_BORDER
  );


  if (
    notificationCount ==
    0
  ) {

    centeredText(

      pipelineConnected
        ?
        "WAITING FOR NOTIFICATIONS"
        :
        "PIPELINE RECONNECTING",

      160,

      75,

      2,

      pipelineConnected
        ?
        COL_GREEN
        :
        COL_ORANGE
    );


    centeredText(
      "VRChat does not need to be running",
      160,
      97,
      1,
      COL_GREY
    );

  } else {

    PagerNotification &n =
      notifications[0];


    tft.setTextColor(
      notificationColour(
        n.noticeClass
      )
    );


    tft.drawString(
      notificationName(
        n.noticeClass
      ),
      19,
      56,
      2
    );


    String sender =
      n.sender;


    if (
      sender.length() >
      28
    ) {

      sender =
        sender.substring(
          0,
          25
        ) + "...";
    }


    tft.setTextColor(
      COL_WHITE
    );


    tft.drawString(
      sender,
      19,
      77,
      2
    );


    String message =
      n.message;


    if (
      message.length() >
      43
    ) {

      message =
        message.substring(
          0,
          40
        ) + "...";
    }


    tft.setTextColor(
      COL_LIGHTGREY
    );


    tft.drawString(
      message,
      19,
      100,
      1
    );


    int unread =
      unreadCount();


    if (
      unread >
      0
    ) {

      tft.fillCircle(
        286,
        75,
        16,
        COL_RED
      );


      centeredText(
        String(
          unread
        ),
        286,
        75,
        2,
        COL_WHITE
      );
    }
  }


  drawButton(
    notificationsButton,
    "NOTIFICATIONS",
    COL_BUTTON,
    COL_WHITE,
    1
  );


  drawButton(
    eventsButton,
    "EVENTS",
    COL_BUTTON,
    COL_WHITE,
    1
  );


  drawButton(
    groupsButton,
    "GROUPS",
    COL_BUTTON,
    COL_WHITE,
    1
  );


  drawButton(
    advancedButton,
    "INFO / ADVANCED",
    COL_BUTTON,
    COL_WHITE,
    1
  );
}


// ============================================================
//                    NOTIFICATIONS SCREEN
// ============================================================

void drawNotificationsScreen() {

  currentScreen =
    SCREEN_NOTIFICATIONS;


  tft.fillScreen(
    COL_BG
  );


  centeredText(
    "NOTIFICATIONS",
    160,
    15,
    2,
    COL_LIGHTBLUE
  );


  drawBackButton();


  if (
    notificationCount ==
    0
  ) {

    centeredText(
      "NO NOTIFICATIONS",
      160,
      112,
      2,
      COL_GREY
    );


    return;
  }


  int y =
    37;


  int rows =
    min(
      notificationCount,
      5
    );


  for (
    int i = 0;
    i < rows;
    i++
  ) {

    PagerNotification &n =
      notifications[i];


    tft.fillRoundRect(
      7,
      y,
      306,
      36,
      4,
      n.unread
        ?
        COL_PANEL2
        :
        COL_PANEL
    );


    tft.fillRect(
      7,
      y,
      5,
      36,
      notificationColour(
        n.noticeClass
      )
    );


    tft.setTextColor(
      notificationColour(
        n.noticeClass
      )
    );


    tft.drawString(
      notificationName(
        n.noticeClass
      ),
      17,
      y + 3,
      1
    );


    String line =

      n.sender +

      ": " +

      n.message;


    if (
      line.length() >
      44
    ) {

      line =
        line.substring(
          0,
          41
        ) + "...";
    }


    tft.setTextColor(
      COL_WHITE
    );


    tft.drawString(
      line,
      17,
      y + 18,
      1
    );


    n.unread =
      false;


    y +=
      39;
  }
}


// ============================================================
//                     TIMELINE DRAWING
// ============================================================

void drawTimelineRows(
  TimelineEntry entries[],
  int count,
  String emptyMessage,
  int scrollOffset
) {

  if (
    count ==
    0
  ) {

    centeredText(
      emptyMessage,
      160,
      112,
      2,
      COL_GREY
    );


    return;
  }


  int y =
    37;


  int rows =
    min(
      count -
        scrollOffset,
      5
    );


  for (
    int i = 0;
    i < rows;
    i++
  ) {

    TimelineEntry &entry =
      entries[
        i +
        scrollOffset
      ];


    tft.fillRoundRect(
      7,
      y,
      306,
      36,
      4,
      COL_PANEL
    );


    String type =
      entry.type;


    if (
      type.length() >
      24
    ) {

      type =
        type.substring(
          0,
          21
        ) + "...";
    }


    tft.setTextColor(
      COL_LIGHTBLUE
    );


    tft.drawString(
      type,
      14,
      y + 3,
      1
    );


    String ageText =

      eventTimeLabel(
        entry.receivedEpoch,
        entry.receivedAt
      );


    tft.setTextDatum(
      TR_DATUM
    );


    tft.setTextColor(
      COL_GREY
    );


    tft.drawString(
      ageText,
      306,
      y + 3,
      1
    );


    tft.setTextDatum(
      TL_DATUM
    );


    String line =

      entry.subject +

      ": " +

      entry.detail;


    if (
      line.length() >
      46
    ) {

      line =
        line.substring(
          0,
          43
        ) + "...";
    }


    tft.setTextColor(
      COL_WHITE
    );


    tft.drawString(
      line,
      14,
      y + 18,
      1
    );


    y +=
      39;
  }
}


void drawEventsScreen() {

  currentScreen =
    SCREEN_EVENTS;


  tft.fillScreen(
    COL_BG
  );


  centeredText(
    "EVENT TIMELINE",
    160,
    15,
    2,
    COL_LIGHTBLUE
  );


  drawBackButton();


  drawTimelineRows(
    timelineEvents,
    timelineEventCount,
    "NO EVENTS YET",
    eventScrollOffset
  );
}


void drawGroupsScreen() {

  currentScreen =
    SCREEN_GROUPS;


  tft.fillScreen(
    COL_BG
  );


  centeredText(
    "GROUP ACTIVITY",
    160,
    15,
    2,
    COL_PURPLE
  );


  drawBackButton();


  drawTimelineRows(
    groupEvents,
    groupEventCount,
    "NO GROUP ACTIVITY",
    groupScrollOffset
  );
}


// ============================================================
//                    READ-ONLY DETAIL VIEW
// ============================================================

void prepareDetailLines() {

  detailLineCount =
    0;


  String remaining =
    detailMessage;


  remaining.replace(
    "\r",
    ""
  );


  const int charactersPerLine =
    42;


  while (
    remaining.length() &&
    detailLineCount < 24
  ) {

    int newline =
      remaining.indexOf('\n');


    int limit =

      min(
        charactersPerLine,
        (int)remaining.length()
      );


    if (
      newline >= 0 &&
      newline < limit
    ) {

      limit =
        newline;

    } else if (
      remaining.length() >
      charactersPerLine
    ) {

      int space =
        remaining.lastIndexOf(
          ' ',
          charactersPerLine
        );


      if (
        space > 0
      ) {

        limit =
          space;
      }
    }


    detailLines[
      detailLineCount++
    ] = remaining.substring(
      0,
      limit
    );


    int removeCount =
      limit;


    while (
      removeCount <
        (int)remaining.length() &&
      (
        remaining[removeCount] == ' ' ||
        remaining[removeCount] == '\n'
      )
    ) {

      removeCount++;
    }


    remaining.remove(
      0,
      removeCount
    );
  }


  if (
    detailLineCount ==
    0
  ) {

    detailLines[0] =
      "No additional message.";


    detailLineCount =
      1;
  }
}


void openDetail(
  ScreenMode returnScreen,
  String type,
  String subject,
  String message,
  time_t epoch,
  unsigned long receivedAt
) {

  detailReturnScreen =
    returnScreen;

  detailType =
    type;

  detailSubject =
    subject;

  detailMessage =
    message;

  detailEpoch =
    epoch;

  detailReceivedAt =
    receivedAt;

  detailScrollOffset =
    0;


  prepareDetailLines();


  drawDetailScreen();
}


void drawDetailScreen() {

  currentScreen =
    SCREEN_DETAIL;


  tft.fillScreen(
    COL_BG
  );


  centeredText(
    "DETAILS",
    160,
    15,
    2,
    COL_LIGHTBLUE
  );


  drawBackButton();


  String typeText =
    detailType;


  if (
    typeText.length() > 34
  ) {

    typeText =
      typeText.substring(0, 31) + "...";
  }


  tft.setTextColor(
    COL_LIGHTBLUE
  );


  tft.drawString(
    typeText,
    10,
    39,
    2
  );


  String subjectText =
    detailSubject;


  if (
    subjectText.length() > 40
  ) {

    subjectText =
      subjectText.substring(0, 37) + "...";
  }


  tft.setTextColor(
    COL_WHITE
  );


  tft.drawString(
    subjectText,
    10,
    61,
    2
  );


  String timestamp =

    formatTimestamp(
      detailEpoch,
      "%d/%m/%Y %H:%M:%S"
    );


  if (
    timestamp.length() == 0
  ) {

    timestamp =
      eventTimeLabel(
        detailEpoch,
        detailReceivedAt
      ) + " ago";
  }


  tft.setTextColor(
    COL_GREY
  );


  tft.drawString(
    timestamp,
    10,
    83,
    1
  );


  tft.fillRoundRect(
    7,
    98,
    306,
    135,
    5,
    COL_PANEL
  );


  int visibleLines =
    8;


  int maximumOffset =

    max(
      0,
      detailLineCount - visibleLines
    );


  detailScrollOffset =

    constrain(
      detailScrollOffset,
      0,
      maximumOffset
    );


  tft.setTextColor(
    COL_WHITE
  );


  for (
    int i = 0;
    i < visibleLines &&
      i + detailScrollOffset <
        detailLineCount;
    i++
  ) {

    tft.drawString(
      detailLines[
        i + detailScrollOffset
      ],
      14,
      105 + i * 15,
      1
    );
  }
}


// ============================================================
//                       TIME SETTINGS
// ============================================================

void drawTimeSettingsScreen() {

  currentScreen =
    SCREEN_TIME_SETTINGS;


  tft.fillScreen(
    COL_BG
  );


  centeredText(
    "TIME SETTINGS",
    160,
    15,
    2,
    COL_LIGHTBLUE
  );


  drawBackButton();


  String currentTime =

    formatTimestamp(
      time(nullptr),
      "%d/%m/%Y  %H:%M:%S"
    );


  centeredText(
    currentTime.length()
      ? currentTime
      : "WAITING FOR INTERNET TIME",
    160,
    37,
    1,
    currentTime.length()
      ? COL_GREEN
      : COL_ORANGE
  );


  for (
    int row = 0;
    row < 4;
    row++
  ) {

    int index =
      timeZoneListOffset + row;


    if (
      index >= TIME_ZONE_COUNT
    ) {

      break;
    }


    drawButton(
      {12, 55 + row * 36, 296, 31},
      TIME_ZONES[index].name,
      index == selectedTimeZone
        ? COL_GREEN
        : COL_PANEL,
      index == selectedTimeZone
        ? COL_BLACK
        : COL_WHITE,
      1
    );
  }


  drawButton(
    {50, 207, 100, 28},
    "NEWER",
    COL_BUTTON,
    COL_WHITE,
    1
  );


  drawButton(
    {170, 207, 100, 28},
    "OLDER",
    COL_BUTTON,
    COL_WHITE,
    1
  );
}


// ============================================================
//                    INFO / ADVANCED SCREEN
// ============================================================

void drawSessionScreen() {

  currentScreen =
    SCREEN_SESSION;


  tft.fillScreen(
    COL_BG
  );


  centeredText(
    "INFO / ADVANCED",
    160,
    15,
    2,
    COL_LIGHTBLUE
  );


  drawBackButton();


  int y =
    42;


  tft.setTextColor(
    COL_GREY
  );


  tft.drawString(
    "Account:",
    12,
    y,
    1
  );


  String accountName =
    currentDisplayName;


  if (
    accountName.length() >
    30
  ) {

    accountName =
      accountName.substring(
        0,
        27
      ) + "...";
  }


  tft.setTextColor(
    COL_WHITE
  );


  tft.drawString(
    accountName,
    90,
    y,
    1
  );


  y +=
    20;


  tft.setTextColor(
    COL_GREY
  );


  tft.drawString(
    "Session:",
    12,
    y,
    1
  );


  tft.setTextColor(
    authCookie.length()
      ?
      COL_GREEN
      :
      COL_RED
  );


  tft.drawString(
    authCookie.length()
      ?
      "AUTH STORED"
      :
      "NO AUTH",
    90,
    y,
    1
  );


  y +=
    20;


  tft.setTextColor(
    COL_GREY
  );


  tft.drawString(
    "Pipeline:",
    12,
    y,
    1
  );


  tft.setTextColor(
    pipelineConnected
      ?
      COL_GREEN
      :
      COL_ORANGE
  );


  tft.drawString(
    pipelineConnected
      ?
      "CONNECTED"
      :
      "RECONNECTING",
    90,
    y,
    1
  );


  y +=
    20;


  tft.setTextColor(
    COL_GREY
  );


  tft.drawString(
    "History:",
    12,
    y,
    1
  );


  tft.setTextColor(
    COL_WHITE
  );


  tft.drawString(
    String(
      timelineEventCount
    ) +
    " events / " +
    String(
      groupEventCount
    ) +
    " groups",
    90,
    y,
    1
  );


  y +=
    20;


  tft.setTextColor(
    COL_GREY
  );


  tft.drawString(
    "Socket:",
    12,
    y,
    1
  );


  tft.setTextColor(
    COL_WHITE
  );


  tft.drawString(
    String(
      pipelineConnectCount
    ) +
    " connects / " +
    String(
      pipelineDisconnectCount
    ) +
    " drops",
    90,
    y,
    1
  );


  y +=
    20;


  tft.setTextColor(
    COL_GREY
  );


  tft.drawString(
    "Last error:",
    12,
    y,
    1
  );


  String errorText =
    lastPipelineError;


  if (
    errorText.length() ==
    0
  ) {

    errorText =
      "NONE";
  }


  if (
    errorText.length() >
    32
  ) {

    errorText =
      errorText.substring(
        0,
        29
      ) + "...";
  }


  tft.setTextColor(
    lastPipelineError.length()
      ?
      COL_RED
      :
      COL_GREEN
  );


  tft.drawString(
    errorText,
    90,
    y,
    1
  );


  drawButton(
    reconnectButton,
    pipelineConnected
      ?
      "PIPELINE"
      :
      "CONNECT",
    COL_ORANGE,
    COL_BLACK,
    1
  );


  drawButton(
    changeWiFiButton,
    "CHANGE WI-FI",
    COL_BLUE,
    COL_WHITE,
    1
  );


  drawButton(
    timeSettingsButton,
    "TIME SETTINGS",
    COL_PURPLE,
    COL_WHITE,
    1
  );


  drawButton(
    logoutButton,
    logoutArmed
      ?
      "TAP AGAIN: LOGOUT"
      :
      "LOGOUT",
    COL_RED,
    COL_WHITE,
    1
  );
}
// ============================================================
//                    HTTP PREPARATION
// ============================================================

void prepareHTTP(
  HTTPClient &http,
  WiFiClientSecure &client,
  const char* url
) {

  client.setInsecure();


  http.begin(
    client,
    url
  );


  http.setTimeout(
    15000
  );


  http.addHeader(
    "User-Agent",
    USER_AGENT
  );


  http.addHeader(
    "Accept",
    "application/json"
  );


  const char* headers[] = {

    "Set-Cookie"
  };


  http.collectHeaders(
    headers,
    1
  );
}


// ============================================================
//                   CURRENT USER PARSER
// ============================================================

bool parseCurrentUser(
  const String &body
) {

  JsonDocument doc;


  if (
    deserializeJson(
      doc,
      body
    )
    !=
    DeserializationError::Ok
  ) {

    return false;
  }


  const char* name =
    doc["displayName"];


  if (
    name &&
    strlen(name)
  ) {

    currentDisplayName =
      name;


    return true;
  }


  return false;
}


// ============================================================
//                       2FA PARSER
// ============================================================

TwoFactorType parse2FARequirement(
  const String &body
) {

  JsonDocument doc;


  if (
    deserializeJson(
      doc,
      body
    )
    !=
    DeserializationError::Ok
  ) {

    return TWOFA_NONE;
  }


  JsonArray methods =

    doc[
      "requiresTwoFactorAuth"
    ].as<JsonArray>();


  for (
    JsonVariant method :
    methods
  ) {

    String value =
      method.as<String>();


    if (
      value ==
      "totp"
    ) {

      return TWOFA_TOTP;
    }


    if (
      value ==
      "emailOtp"
    ) {

      return TWOFA_EMAIL;
    }
  }


  return TWOFA_NONE;
}


// ============================================================
//                  VERIFY SAVED SESSION
// ============================================================

bool verifyStoredSession() {

  if (
    authCookie.length() ==
    0
  ) {

    return false;
  }


  showStatus(
    "VRCHAT",
    "Checking saved session",
    COL_LIGHTBLUE
  );


  WiFiClientSecure client;

  HTTPClient http;


  prepareHTTP(
    http,
    client,
    API_AUTH_USER
  );


  http.addHeader(
    "Cookie",
    buildCookieHeader()
  );


  int code =
    http.GET();


  String body =
    http.getString();


  http.end();


  Serial.print(
    "Saved session HTTP: "
  );


  Serial.println(
    code
  );


  if (
    code ==
    200 &&
    parseCurrentUser(
      body
    )
  ) {

    sessionValid =
      true;


    return true;
  }


  sessionValid =
    false;


  return false;
}
// ============================================================
//                           LOGIN
// ============================================================

void performLogin() {

  if (
    loginUsername.length() ==
      0 ||
    loginPassword.length() ==
      0
  ) {

    return;
  }


  showStatus(
    "VRCHAT LOGIN",
    "Contacting VRChat",
    COL_LIGHTBLUE
  );


  String credentials =

    loginUsername +
    ":" +
    loginPassword;


  String encoded =
    base64Encode(
      credentials
    );


  credentials =
    "";


  WiFiClientSecure client;

  HTTPClient http;


  prepareHTTP(
    http,
    client,
    API_AUTH_USER
  );


  http.addHeader(
    "Authorization",
    "Basic " + encoded
  );


  int code =
    http.GET();


  String body =
    http.getString();


  String setCookie =
    http.header(
      "Set-Cookie"
    );


  http.end();


  encoded =
    "";


  wipePassword();


  Serial.print(
    "Login HTTP: "
  );


  Serial.println(
    code
  );


  String newAuth =

    extractCookie(
      setCookie,
      "auth"
    );


  if (
    newAuth.length()
  ) {

    authCookie =
      newAuth;
  }


  if (
    authCookie.length() ==
    0
  ) {

    JsonDocument doc;


    if (
      deserializeJson(
        doc,
        body
      )
      ==
      DeserializationError::Ok
    ) {

      String token =
        doc["authToken"] | "";


      if (
        token.length()
      ) {

        authCookie =
          token;
      }
    }
  }


  required2FA =
    parse2FARequirement(
      body
    );


  if (
    required2FA !=
    TWOFA_NONE
  ) {

    twoFactorCode =
      "";


    draw2FAScreen();


    return;
  }


  if (
    code ==
    200 &&
    parseCurrentUser(
      body
    )
  ) {

    sessionValid =
      true;


    saveSession();


    drawHomeScreen();


    startPipeline();


    return;
  }


  String failureMessage;


  if (
    code ==
    401
  ) {

    failureMessage =
      "HTTP 401: credentials rejected";

  } else if (
    code ==
    403
  ) {

    failureMessage =
      "HTTP 403: login blocked";

  } else if (
    code ==
    429
  ) {

    failureMessage =
      "HTTP 429: wait and retry";

  } else if (
    code <
    0
  ) {

    failureMessage =
      "Network request failed";

  } else {

    failureMessage =
      "HTTP " +
      String(
        code
      ) +
      ": see Serial Monitor";
  }


  showStatus(
    "LOGIN FAILED",
    failureMessage,
    COL_RED
  );


  delay(
    1500
  );


  drawLoginScreen();
}


// ============================================================
//                       VERIFY 2FA
// ============================================================

void verify2FACode() {

  if (
    twoFactorCode.length() <
    4
  ) {

    return;
  }


  showStatus(
    "VERIFYING",
    "Checking security code",
    COL_LIGHTBLUE
  );


  const char* endpoint =

    required2FA ==
      TWOFA_EMAIL

      ?

      API_EMAIL_OTP

      :

      API_TOTP;


  WiFiClientSecure client;

  HTTPClient http;


  prepareHTTP(
    http,
    client,
    endpoint
  );


  http.addHeader(
    "Content-Type",
    "application/json"
  );


  http.addHeader(
    "Cookie",
    buildCookieHeader()
  );


  JsonDocument requestDoc;


  requestDoc[
    "code"
  ] =
    twoFactorCode;


  String requestBody;


  serializeJson(
    requestDoc,
    requestBody
  );


  int code =
    http.POST(
      requestBody
    );


  String body =
    http.getString();


  String setCookie =
    http.header(
      "Set-Cookie"
    );


  http.end();


  wipe2FA();


  String new2FA =

    extractCookie(
      setCookie,
      "twoFactorAuth"
    );


  if (
    new2FA.length()
  ) {

    twoFactorCookie =
      new2FA;
  }


  JsonDocument resultDoc;


  bool verified =
    false;


  if (
    deserializeJson(
      resultDoc,
      body
    )
    ==
    DeserializationError::Ok
  ) {

    verified =
      resultDoc[
        "verified"
      ] | false;
  }


  if (
    code !=
      200 ||
    !verified
  ) {

    showStatus(
      "2FA FAILED",
      "Invalid code",
      COL_RED
    );


    delay(
      1400
    );


    draw2FAScreen();


    return;
  }


  if (
    verifyStoredSession()
  ) {

    saveSession();


    required2FA =
      TWOFA_NONE;


    drawHomeScreen();


    startPipeline();


    return;
  }


  showStatus(
    "SESSION ERROR",
    "Verification failed",
    COL_RED
  );


  delay(
    1500
  );


  drawLoginScreen();
}


// ============================================================
//                 NOTIFICATION CLASSIFIER
// ============================================================

NotificationClass classifyNotification(
  String subtype,
  String message
) {

  subtype.toLowerCase();

  message.toLowerCase();


  if (
    subtype.indexOf(
      "boop"
    ) >=
      0 ||

    message.indexOf(
      "boop"
    ) >=
      0
  ) {

    return NOTICE_BOOP;
  }


  if (
    subtype.indexOf(
      "friend"
    ) >=
      0
  ) {

    return NOTICE_FRIEND;
  }


  if (
    subtype.indexOf(
      "invite"
    ) >=
      0
  ) {

    return NOTICE_INVITE;
  }


  return NOTICE_SYSTEM;
}


// ============================================================
//                    SAFE JSON STRING
// ============================================================

String objectString(
  JsonObject object,
  const char* key
) {

  if (
    object.isNull()
  ) {

    return "";
  }


  if (
    object[key].is<const char*>()
  ) {

    return String(
      object[key]
        .as<const char*>()
    );
  }


  return "";
}


String nestedObjectString(
  JsonObject object,
  const char* parentKey,
  const char* childKey
) {

  if (
    object[
      parentKey
    ].is<JsonObject>()
  ) {

    return objectString(
      object[
        parentKey
      ].as<JsonObject>(),
      childKey
    );
  }


  return "";
}


void rememberFriendName(
  const String &userId,
  const String &displayName
) {

  if (
    userId.length() ==
      0 ||
    displayName.length() ==
      0
  ) {

    return;
  }


  for (
    int i = 0;
    i < friendCacheCount;
    i++
  ) {

    if (
      friendCacheIds[i] ==
      userId
    ) {

      friendCacheNames[i] =
        displayName;


      return;
    }
  }


  int endIndex =

    min(
      friendCacheCount,
      MAX_FRIEND_NAME_CACHE - 1
    );


  for (
    int i = endIndex;
    i > 0;
    i--
  ) {

    friendCacheIds[i] =
      friendCacheIds[i - 1];


    friendCacheNames[i] =
      friendCacheNames[i - 1];
  }


  friendCacheIds[0] =
    userId;


  friendCacheNames[0] =
    displayName;


  if (
    friendCacheCount <
    MAX_FRIEND_NAME_CACHE
  ) {

    friendCacheCount++;
  }
}


String knownFriendName(
  const String &userId
) {

  for (
    int i = 0;
    i < friendCacheCount;
    i++
  ) {

    if (
      friendCacheIds[i] ==
      userId
    ) {

      return friendCacheNames[i];
    }
  }


  return "";
}


void recordGeneralPipelineEvent(
  String pipelineType,
  JsonObject object
) {

  String userId =

    nestedObjectString(
      object,
      "user",
      "id"
    );


  if (
    userId.length() ==
    0
  ) {

    userId =
      objectString(
        object,
        "userId"
      );
  }

  String subject =
    nestedObjectString(
      object,
      "user",
      "displayName"
    );


  if (
    subject.length() ==
    0
  ) {

    subject =
      objectString(
        object,
        "displayName"
      );
  }


  if (
    subject.length()
  ) {

    rememberFriendName(
      userId,
      subject
    );

  } else if (
    userId.length()
  ) {

    subject =
      knownFriendName(
        userId
      );
  }


  if (
    subject.length() ==
    0
  ) {

    subject =
      objectString(
        object,
        "userDisplayName"
      );
  }


  String groupName =
    nestedObjectString(
      object,
      "group",
      "name"
    );


  if (
    groupName.length() ==
    0
  ) {

    groupName =
      objectString(
        object,
        "groupName"
      );
  }


  bool groupRelated =

    isGroupActivity(
      pipelineType
    ) ||

    groupName.length();


  if (
    groupRelated &&
    groupName.length()
  ) {

    subject =
      groupName;
  }


  if (
    subject.length() ==
    0
  ) {

    subject =
      groupRelated
        ?
        "Group"
        :
        "Friend";
  }


  String status =
    nestedObjectString(
      object,
      "user",
      "status"
    );


  if (
    status.length() ==
    0
  ) {

    status =
      objectString(
        object,
        "status"
      );
  }


  String location =
    nestedObjectString(
      object,
      "user",
      "location"
    );


  if (
    location.length() ==
    0
  ) {

    location =
      objectString(
        object,
        "location"
      );
  }


  String worldName =
    nestedObjectString(
      object,
      "world",
      "name"
    );


  String detail;


  if (
    pipelineType ==
    "friend-online"
  ) {

    detail =
      "Came online";

  } else if (
    pipelineType ==
    "friend-offline"
  ) {

    detail =
      "Went offline";

  } else if (
    pipelineType ==
    "friend-active"
  ) {

    detail =
      "Active";

  } else if (
    pipelineType.indexOf(
      "location"
    ) >=
    0
  ) {

    String lowerLocation =
      location;

    lowerLocation.toLowerCase();


    if (
      worldName.length()
    ) {

      detail =
        "At " +
        worldName;

    } else if (
      location.length() ==
        0 ||
      lowerLocation.indexOf(
        "private"
      ) >=
        0
    ) {

      detail =
        "Location private";

    } else if (
      lowerLocation ==
      "offline"
    ) {

      detail =
        "Offline";

    } else {

      detail =
        "Location public";
    }

  } else if (
    status.length()
  ) {

    detail =
      "Status: " +
      status;

  } else {

    detail =
      objectString(
        object,
        "message"
      );


    if (
      detail.length() ==
      0
    ) {

      detail =
        objectString(
          object,
          "title"
        );
    }


    if (
      detail.length() ==
      0
    ) {

      detail =
        pipelineType;
    }
  }


  recordTimelineEvent(
    pipelineType,
    subject,
    detail,
    groupRelated
  );
}


// ============================================================
//                     DETAILS PARSER
// ============================================================

void parseDetailsField(
  String details,
  String &message
) {

  if (
    details.length() <
    2
  ) {

    return;
  }


  if (
    details[0] !=
    '{'
  ) {

    if (
      message.length() ==
      0
    ) {

      message =
        details;
    }


    return;
  }


  JsonDocument detailsDoc;


  if (
    deserializeJson(
      detailsDoc,
      details
    )
    !=
    DeserializationError::Ok
  ) {

    return;
  }


  String detailsMessage =
    detailsDoc[
      "message"
    ] | "";


  String worldName =
    detailsDoc[
      "worldName"
    ] | "";


  String name =
    detailsDoc[
      "name"
    ] | "";


  if (
    message.length() ==
      0 &&
    detailsMessage.length()
  ) {

    message =
      detailsMessage;
  }


  if (
    message.length() ==
      0 &&
    worldName.length()
  ) {

    message =
      worldName;
  }


  if (
    message.length() ==
      0 &&
    name.length()
  ) {

    message =
      name;
  }
}


// ============================================================
//                    PIPELINE PARSER
// ============================================================

void processPipelineMessage(
  const String &payload
) {

  pipelineLastMessage =
    millis();


  JsonDocument outer;


  DeserializationError error =

    deserializeJson(
      outer,
      payload
    );


  if (error) {

    Serial.print(
      "[PIPELINE] JSON error: "
    );


    Serial.println(
      error.c_str()
    );


    return;
  }


  // ----------------------------------------------------------
  // VRChat Pipeline error packet
  // ----------------------------------------------------------

  String pipelineError =
    outer[
      "err"
    ] | "";


  if (
    pipelineError.length()
  ) {

    lastPipelineError =
      pipelineError;


    Serial.println();
    Serial.println(
      "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    );

    Serial.print(
      "[PIPELINE ERROR] "
    );

    Serial.println(
      pipelineError
    );

    Serial.println(
      "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    );


    return;
  }


  String pipelineType =
    outer[
      "type"
    ] | "";


  if (
    pipelineType.length() ==
    0
  ) {

    Serial.println(
      "[PIPELINE] Packet without type"
    );


    return;
  }


  bool notificationPacket =

    pipelineType ==
      "notification" ||

    pipelineType ==
      "notification-v2";


  // ----------------------------------------------------------
  // Decode nested content.
  // ----------------------------------------------------------

  JsonDocument contentDoc;


  JsonVariant contentVariant =
    outer[
      "content"
    ];


  if (
    contentVariant.is<
      const char*
    >()
  ) {

    const char* embedded =

      contentVariant
        .as<const char*>();


    DeserializationError innerError =

      deserializeJson(
        contentDoc,
        embedded
      );


    if (innerError) {

      Serial.print(
        "[PIPELINE] Inner JSON error: "
      );


      Serial.println(
        innerError.c_str()
      );


      return;
    }

  } else {

    String normalised;


    serializeJson(
      contentVariant,
      normalised
    );


    DeserializationError innerError =

      deserializeJson(
        contentDoc,
        normalised
      );


    if (innerError) {

      Serial.print(
        "[PIPELINE] Content error: "
      );


      Serial.println(
        innerError.c_str()
      );


      return;
    }
  }


  JsonObject object;


  if (
    contentDoc[
      "notification"
    ].is<JsonObject>()
  ) {

    object =
      contentDoc[
        "notification"
      ].as<JsonObject>();

  } else {

    object =
      contentDoc.as<JsonObject>();
  }


  if (
    object.isNull()
  ) {

    Serial.println(
      "[PIPELINE] Notification object missing"
    );


    return;
  }


  if (
    !notificationPacket
  ) {

    Serial.print(
      "[PIPELINE] Event: "
    );


    Serial.println(
      pipelineType
    );


    recordGeneralPipelineEvent(
      pipelineType,
      object
    );


    return;
  }


  // ----------------------------------------------------------
  // Subtype
  // ----------------------------------------------------------

  String subtype =

    objectString(
      object,
      "type"
    );


  if (
    subtype.length() ==
    0
  ) {

    subtype =

      objectString(
        object,
        "notificationType"
      );
  }


  if (
    subtype.length() ==
    0
  ) {

    subtype =
      pipelineType;
  }


  // ----------------------------------------------------------
  // Sender
  // ----------------------------------------------------------

  String sender =

    objectString(
      object,
      "senderUsername"
    );


  if (
    sender.length() ==
    0
  ) {

    sender =

      objectString(
        object,
        "senderDisplayName"
      );
  }


  if (
    sender.length() ==
    0
  ) {

    sender =

      objectString(
        object,
        "senderName"
      );
  }


  if (
    sender.length() ==
      0 &&
    object[
      "sender"
    ].is<JsonObject>()
  ) {

    JsonObject senderObject =

      object[
        "sender"
      ].as<JsonObject>();


    sender =

      objectString(
        senderObject,
        "displayName"
      );


    if (
      sender.length() ==
      0
    ) {

      sender =

        objectString(
          senderObject,
          "username"
        );
    }
  }


  if (
    sender.length() ==
      0 &&
    object[
      "user"
    ].is<JsonObject>()
  ) {

    JsonObject userObject =

      object[
        "user"
      ].as<JsonObject>();


    sender =

      objectString(
        userObject,
        "displayName"
      );
  }


  if (
    sender.length() ==
    0
  ) {

    String groupName =
      objectString(
        object,
        "groupName"
      );


    if (
      groupName.length() ==
        0 &&
      object[
        "data"
      ].is<JsonObject>()
    ) {

      JsonObject dataObject =

        object[
          "data"
        ].as<JsonObject>();


      groupName =

        objectString(
          dataObject,
          "groupName"
        );


      if (
        groupName.length() ==
        0
      ) {

        groupName =

          objectString(
            dataObject,
            "name"
          );
      }
    }


    if (
      groupName.length() ==
        0 &&
      object[
        "data"
      ].is<const char*>()
    ) {

      JsonDocument dataDoc;


      if (
        deserializeJson(
          dataDoc,
          object[
            "data"
          ].as<const char*>()
        )
        ==
        DeserializationError::Ok
      ) {

        groupName =

          dataDoc[
            "groupName"
          ] | "";
      }
    }


    if (
      groupName.length() ==
        0 &&
      object[
        "group"
      ].is<JsonObject>()
    ) {

      groupName =

        objectString(
          object[
            "group"
          ].as<JsonObject>(),
          "name"
        );
    }


    if (
      groupName.length()
    ) {

      sender =
        groupName;
    }
  }


  if (
    sender.length() ==
    0
  ) {

    sender =
      "VRChat";
  }


  // ----------------------------------------------------------
  // Message
  // ----------------------------------------------------------

  String message =

    objectString(
      object,
      "message"
    );


  if (
    message.length() ==
    0
  ) {

    message =

      objectString(
        object,
        "title"
      );
  }


  if (
    message.length() ==
    0
  ) {

    message =

      objectString(
        object,
        "body"
      );
  }


  if (
    message.length() ==
    0
  ) {

    message =

      objectString(
        object,
        "text"
      );
  }


  String details =

    objectString(
      object,
      "details"
    );


  parseDetailsField(
    details,
    message
  );


  if (
    message.length() ==
    0
  ) {

    message =
      subtype;
  }


  NotificationClass noticeClass =

    classifyNotification(
      subtype,
      message
    );


  addNotification(

    noticeClass,

    pipelineType,

    subtype,

    sender,

    message
  );
}


// ============================================================
//              ARDUINOWEBSOCKETS CALLBACKS
// ============================================================

void pipelineMessageEvent(
  WebsocketsMessage message
) {

  pipelineLastMessage =
    millis();


  processPipelineMessage(
    message.data()
  );
}


void pipelineConnectionEvent(
  WebsocketsEvent event,
  String data
) {

  if (
    event ==
    WebsocketsEvent::ConnectionOpened
  ) {

    pipelineConnected =
      true;


    pipelineEverConnected =
      true;


    pipelineConnectedAt =
      millis();


    pipelineConnectCount++;


    lastPipelineError =
      "";


    Serial.println();
    Serial.print(
      "[PIPELINE] CONNECTED #"
    );

    Serial.println(
      pipelineConnectCount
    );


    drawPipelineBadge();


    return;
  }


  if (
    event ==
    WebsocketsEvent::ConnectionClosed
  ) {

    unsigned long disconnectedAt =
      millis();


    if (
      pipelineConnected
    ) {

      pipelineDisconnectCount++;
    }


    pipelineConnected =
      false;


    pipelineLastDisconnect =
      disconnectedAt;


    Serial.print(
      "[PIPELINE] Disconnected. Total: "
    );

    Serial.println(
      pipelineDisconnectCount
    );


    if (
      pipelineConnectedAt
    ) {

      Serial.print(
        "[PIPELINE] Connection duration: "
      );

      Serial.print(
        disconnectedAt - pipelineConnectedAt
      );

      Serial.println(
        " ms"
      );
    }


    Serial.print(
      "[PIPELINE] Close detail: "
    );


    if (
      data.length()
    ) {

      Serial.println(
        data
      );

    } else {

      Serial.println(
        "(none)"
      );
    }


    drawPipelineBadge();


    return;
  }


  if (
    event ==
    WebsocketsEvent::GotPing
  ) {

    return;
  }


  if (
    event ==
    WebsocketsEvent::GotPong
  ) {

    return;
  }
}


// ============================================================
//                  CONNECT PIPELINE NOW
// ============================================================

bool connectPipelineNow() {

  if (
    WiFi.status() !=
    WL_CONNECTED
  ) {

    return false;
  }


  String pipelineURL =

    "wss://" +

    String(
      PIPELINE_HOST
    ) +

    pipelinePath;


  Serial.println();
  Serial.println(
    "[PIPELINE] Connecting with ArduinoWebsockets"
  );


  bool connected =
    pipelineSocket.connect(
      pipelineURL
    );


  if (
    !connected
  ) {

    pipelineConnected =
      false;


    pipelineLastDisconnect =
      millis();


    Serial.println(
      "[PIPELINE] Connection attempt failed"
    );
  }


  return connected;
}


// ============================================================
//                     START PIPELINE
// ============================================================

void startPipeline() {

  if (
    !sessionValid ||
    authCookie.length() == 0 ||
    WiFi.status() != WL_CONNECTED ||
    pipelineStarted
  ) {

    return;
  }


  pipelineStarted =
    true;


  pipelineConnected =
    false;


  pipelinePath =

    "/?authToken=" +

    urlEncode(
      authCookie
    );


  pipelineSocket.onMessage(
    pipelineMessageEvent
  );


  pipelineSocket.onEvent(
    pipelineConnectionEvent
  );


  pipelineSocket.addHeader(
    "Origin",
    "https://vrchat.com"
  );


  pipelineSocket.addHeader(
    "User-Agent",
    USER_AGENT
  );


  pipelineSocket.setCACert(
    VRCHAT_ROOT_CA
  );


  connectPipelineNow();
}


// ============================================================
//                   MANUAL RECONNECT
// ============================================================

void reconnectPipeline() {

  Serial.println();
  Serial.println(
    "[PIPELINE] Manual reconnect requested"
  );


  pipelineSocket.close();


  pipelineConnected =
    false;


  lastPipelineError =
    "";


  pipelineLastDisconnect =
    0;


  connectPipelineNow();


  drawPipelineBadge();
}


// ============================================================
//                           LOGOUT
// ============================================================

void performLogout() {

  pipelineSocket.close();


  pipelineConnected =
    false;


  pipelineStarted =
    false;


  showStatus(
    "LOGGING OUT",
    "Ending VRChat session",
    COL_ORANGE
  );


  if (
    authCookie.length()
  ) {

    WiFiClientSecure client;

    HTTPClient http;


    prepareHTTP(
      http,
      client,
      API_LOGOUT
    );


    http.addHeader(
      "Cookie",
      buildCookieHeader()
    );


    int code =
      http.sendRequest(
        "PUT"
      );


    Serial.print(
      "Logout HTTP: "
    );


    Serial.println(
      code
    );


    http.end();
  }


  clearStoredSession();


  wipePassword();

  wipe2FA();


  notificationCount =
    0;


  rgbOff();


  delay(
    300
  );


  drawLoginScreen();
}


// ============================================================
//                      WI-FI SETUP TOUCH
// ============================================================

void handleWiFiScanTouch(
  int x,
  int y
) {

  if (
    wifiSetupFromAdvanced &&
    hit(
      x,
      y,
      backButton
    )
  ) {

    drawSessionScreen();

    return;
  }


  for (
    int row = 0;
    row < 4;
    row++
  ) {

    int index =
      wifiListOffset +
      row;


    Button networkButton =
      {8, 35 + row * 41, 304, 36};


    if (
      index <
        scannedWiFiCount &&
      hit(
        x,
        y,
        networkButton
      )
    ) {

      selectedWiFiSSID =
        scannedWiFiSSIDs[index];


      inputTarget =
        INPUT_WIFI_PASSWORD;


      keyboardBuffer =
        "";


      keyboardMode =
        KB_LOWER;


      drawKeyboard();


      return;
    }
  }


  if (
    hit(
      x,
      y,
      {5, 204, 72, 31}
    )
  ) {

    beginWiFiScan(
      wifiSetupFromAdvanced
    );

    return;
  }


  if (
    hit(
      x,
      y,
      {82, 204, 75, 31}
    )
  ) {

    selectedWiFiSSID =
      "";


    inputTarget =
      INPUT_WIFI_SSID;


    keyboardBuffer =
      "";


    keyboardMode =
      KB_LOWER;


    drawKeyboard();


    return;
  }


  if (
    hit(
      x,
      y,
      {162, 204, 73, 31}
    )
  ) {

    wifiListOffset =

      max(
        0,
        wifiListOffset - 4
      );


    drawWiFiScanScreen();


    return;
  }


  if (
    hit(
      x,
      y,
      {240, 204, 75, 31}
    )
  ) {

    wifiListOffset =

      min(
        max(
          0,
          scannedWiFiCount - 4
        ),
        wifiListOffset + 4
      );


    drawWiFiScanScreen();
  }
}


// ============================================================
//                      LOGIN TOUCH
// ============================================================

void handleLoginTouch(
  int x,
  int y
) {

  if (
    hit(
      x,
      y,
      usernameButton
    )
  ) {

    inputTarget =
      INPUT_USERNAME;


    keyboardBuffer =
      loginUsername;


    keyboardMode =
      KB_LOWER;


    drawKeyboard();


    return;
  }


  if (
    hit(
      x,
      y,
      passwordButton
    )
  ) {

    inputTarget =
      INPUT_PASSWORD;


    keyboardBuffer =
      loginPassword;


    keyboardMode =
      KB_LOWER;


    drawKeyboard();


    return;
  }


  if (
    hit(
      x,
      y,
      clearLoginButton
    )
  ) {

    loginUsername =
      "";


    wipePassword();


    drawLoginScreen();


    return;
  }


  if (
    hit(
      x,
      y,
      loginButton
    )
  ) {

    performLogin();


    return;
  }
}


// ============================================================
//                    KEYBOARD TOUCH
// ============================================================

void handleKeyboardTouch(
  int x,
  int y
) {

  String row1;
  String row2;
  String row3;


  switch (
    keyboardMode
  ) {

    case KB_LOWER:

      row1 =
        "qwertyuiop";

      row2 =
        "asdfghjkl";

      row3 =
        "zxcvbnm";

      break;


    case KB_UPPER:

      row1 =
        "QWERTYUIOP";

      row2 =
        "ASDFGHJKL";

      row3 =
        "ZXCVBNM";

      break;


    case KB_SYMBOL1:

      row1 =
        "1234567890";

      row2 =
        "!@#$%^&*()";

      row3 =
        "-_=+[]{}";

      break;


    case KB_SYMBOL2:

      row1 =
        "`~\\|/<>?";

      row2 =
        ".,:;\"'";

      row3 =
        "";

      break;
  }


  if (
    processKeyboardRowTouch(
      row1,
      70,
      x,
      y
    )
  ) {

    return;
  }


  if (
    processKeyboardRowTouch(
      row2,
      106,
      x,
      y
    )
  ) {

    return;
  }


  if (
    processKeyboardRowTouch(
      row3,
      142,
      x,
      y
    )
  ) {

    return;
  }


  Button modeButton =
    {5,183,58,48};

  Button spaceButton =
    {67,183,94,48};

  Button deleteButton =
    {165,183,69,48};

  Button okButton =
    {238,183,77,48};


  if (
    hit(
      x,
      y,
      modeButton
    )
  ) {

    switch (
      keyboardMode
    ) {

      case KB_LOWER:

        keyboardMode =
          KB_UPPER;

        break;


      case KB_UPPER:

        keyboardMode =
          KB_SYMBOL1;

        break;


      case KB_SYMBOL1:

        keyboardMode =
          KB_SYMBOL2;

        break;


      case KB_SYMBOL2:

        keyboardMode =
          KB_LOWER;

        break;
    }


    drawKeyboard();


    return;
  }


  if (
    hit(
      x,
      y,
      spaceButton
    )
  ) {

    if (
      keyboardBuffer.length() <
      80
    ) {

      keyboardBuffer +=
        ' ';
    }


    drawKeyboard();


    return;
  }


  if (
    hit(
      x,
      y,
      deleteButton
    )
  ) {

    if (
      keyboardBuffer.length()
    ) {

      keyboardBuffer.remove(
        keyboardBuffer.length() - 1
      );
    }


    drawKeyboard();


    return;
  }


  if (
    hit(
      x,
      y,
      okButton
    )
  ) {

    if (
      inputTarget ==
      INPUT_USERNAME
    ) {

      loginUsername =
        keyboardBuffer;

    } else if (
      inputTarget ==
      INPUT_PASSWORD
    ) {

      loginPassword =
        keyboardBuffer;


      keyboardBuffer =
        "";


      drawLoginScreen();


      return;

    } else if (
      inputTarget ==
      INPUT_WIFI_SSID
    ) {

      selectedWiFiSSID =
        keyboardBuffer;


      keyboardBuffer =
        "";


      if (
        selectedWiFiSSID.length() ==
        0
      ) {

        drawWiFiScanScreen();

        return;
      }


      inputTarget =
        INPUT_WIFI_PASSWORD;


      keyboardMode =
        KB_LOWER;


      drawKeyboard();


      return;

    } else {

      String candidatePassword =
        keyboardBuffer;


      keyboardBuffer =
        "";


      if (
        connectWiFiCredentials(
          selectedWiFiSSID,
          candidatePassword
        )
      ) {

        savedWiFiSSID =
          selectedWiFiSSID;


        savedWiFiPassword =
          candidatePassword;


        saveWiFiSettings();


        continueAfterWiFiConnection();

      } else {

        showStatus(
          "WI-FI FAILED",
          "Password or network incorrect",
          COL_RED
        );


        delay(
          1800
        );


        beginWiFiScan(
          wifiSetupFromAdvanced
        );
      }


      return;
    }


    keyboardBuffer =
      "";


    drawLoginScreen();


    return;
  }
}


// ============================================================
//                       2FA TOUCH
// ============================================================

void handle2FATouch(
  int x,
  int y
) {

  const char numbers[9] = {

    '1','2','3',

    '4','5','6',

    '7','8','9'
  };


  int index =
    0;


  for (
    int row = 0;
    row < 3;
    row++
  ) {

    for (
      int column = 0;
      column < 3;
      column++
    ) {

      Button key = {

        54 +
          column * 72,

        74 +
          row * 43,

        62,

        36
      };


      if (
        hit(
          x,
          y,
          key
        )
      ) {

        if (
          twoFactorCode.length() <
          8
        ) {

          twoFactorCode +=
            numbers[index];
        }


        draw2FAScreen();


        return;
      }


      index++;
    }
  }


  Button deleteButton =
    {18,205,84,30};


  Button zeroButton =
    {118,205,84,30};


  Button verifyButton =
    {218,205,84,30};


  if (
    hit(
      x,
      y,
      deleteButton
    )
  ) {

    if (
      twoFactorCode.length()
    ) {

      twoFactorCode.remove(
        twoFactorCode.length() - 1
      );
    }


    draw2FAScreen();


    return;
  }


  if (
    hit(
      x,
      y,
      zeroButton
    )
  ) {

    if (
      twoFactorCode.length() <
      8
    ) {

      twoFactorCode +=
        '0';
    }


    draw2FAScreen();


    return;
  }


  if (
    hit(
      x,
      y,
      verifyButton
    )
  ) {

    verify2FACode();


    return;
  }
}


// ============================================================
//                        HOME TOUCH
// ============================================================

void handleHomeTouch(
  int x,
  int y
) {

  if (
    hit(
      x,
      y,
      notificationsButton
    )
  ) {

    drawNotificationsScreen();


    return;
  }


  if (
    hit(
      x,
      y,
      eventsButton
    )
  ) {

    eventScrollOffset =
      0;

    drawEventsScreen();


    return;
  }


  if (
    hit(
      x,
      y,
      groupsButton
    )
  ) {

    groupScrollOffset =
      0;

    drawGroupsScreen();


    return;
  }


  if (
    hit(
      x,
      y,
      advancedButton
    )
  ) {

    logoutArmed =
      false;


    drawSessionScreen();


    return;
  }
}


// ============================================================
//                       TOUCH ROUTER
// ============================================================

void handleTouch(
  int x,
  int y
) {

  switch (
    currentScreen
  ) {

    case SCREEN_WIFI_SCAN:

      handleWiFiScanTouch(
        x,
        y
      );

      break;

    case SCREEN_LOGIN:

      handleLoginTouch(
        x,
        y
      );

      break;


    case SCREEN_KEYBOARD:

      handleKeyboardTouch(
        x,
        y
      );

      break;


    case SCREEN_2FA:

      handle2FATouch(
        x,
        y
      );

      break;


    case SCREEN_HOME:

      handleHomeTouch(
        x,
        y
      );

      break;


    case SCREEN_NOTIFICATIONS:

      if (
        hit(
          x,
          y,
          backButton
        )
      ) {

        drawHomeScreen();


        break;
      }


      if (
        y >= 37 &&
        y < 232
      ) {

        int index =
          (y - 37) / 39;


        if (
          index >= 0 &&
          index < notificationCount &&
          index < 5
        ) {

          PagerNotification &n =
            notifications[index];


          openDetail(
            SCREEN_NOTIFICATIONS,
            n.subtype,
            n.sender,
            n.message,
            n.receivedEpoch,
            n.receivedAt
          );
        }
      }

      break;


    case SCREEN_EVENTS:

      if (
        hit(
          x,
          y,
          backButton
        )
      ) {

        drawHomeScreen();


        break;
      }


      if (
        y >= 37 &&
        y < 232
      ) {

        int index =
          eventScrollOffset +
          (y - 37) / 39;


        if (
          index >= 0 &&
          index < timelineEventCount
        ) {

          TimelineEntry &entry =
            timelineEvents[index];


          openDetail(
            SCREEN_EVENTS,
            entry.type,
            entry.subject,
            entry.detail,
            entry.receivedEpoch,
            entry.receivedAt
          );
        }
      }

      break;


    case SCREEN_GROUPS:

      if (
        hit(
          x,
          y,
          backButton
        )
      ) {

        drawHomeScreen();


        break;
      }


      if (
        y >= 37 &&
        y < 232
      ) {

        int index =
          groupScrollOffset +
          (y - 37) / 39;


        if (
          index >= 0 &&
          index < groupEventCount
        ) {

          TimelineEntry &entry =
            groupEvents[index];


          openDetail(
            SCREEN_GROUPS,
            entry.type,
            entry.subject,
            entry.detail,
            entry.receivedEpoch,
            entry.receivedAt
          );
        }
      }

      break;


    case SCREEN_DETAIL:

      if (
        hit(
          x,
          y,
          backButton
        )
      ) {

        if (
          detailReturnScreen ==
          SCREEN_NOTIFICATIONS
        ) {

          drawNotificationsScreen();

        } else if (
          detailReturnScreen ==
          SCREEN_EVENTS
        ) {

          drawEventsScreen();

        } else {

          drawGroupsScreen();
        }
      }


      break;


    case SCREEN_TIME_SETTINGS:

      if (
        hit(
          x,
          y,
          backButton
        )
      ) {

        drawSessionScreen();


        break;
      }


      for (
        int row = 0;
        row < 4;
        row++
      ) {

        int index =
          timeZoneListOffset + row;


        if (
          index < TIME_ZONE_COUNT &&
          hit(
            x,
            y,
            {12, 55 + row * 36, 296, 31}
          )
        ) {

          selectedTimeZone =
            index;


          saveTimeSettings();


          applyTimeSettings();


          drawTimeSettingsScreen();


          break;
        }
      }


      if (
        hit(
          x,
          y,
          {50, 207, 100, 28}
        )
      ) {

        timeZoneListOffset =

          max(
            0,
            timeZoneListOffset - 4
          );


        drawTimeSettingsScreen();


        break;
      }


      if (
        hit(
          x,
          y,
          {170, 207, 100, 28}
        )
      ) {

        timeZoneListOffset =

          min(
            max(
              0,
              TIME_ZONE_COUNT - 4
            ),
            timeZoneListOffset + 4
          );


        drawTimeSettingsScreen();
      }


      break;


    case SCREEN_SESSION:

      if (
        hit(
          x,
          y,
          backButton
        )
      ) {

        logoutArmed =
          false;


        drawHomeScreen();


        break;
      }


      if (
        hit(
          x,
          y,
          timeSettingsButton
        )
      ) {

        timeZoneListOffset =

          selectedTimeZone >= 4
            ? 4
            : 0;


        drawTimeSettingsScreen();


        break;
      }


      if (
        hit(
          x,
          y,
          reconnectButton
        )
      ) {

        logoutArmed =
          false;


        reconnectPipeline();


        drawSessionScreen();


        break;
      }


      if (
        hit(
          x,
          y,
          changeWiFiButton
        )
      ) {

        logoutArmed =
          false;


        beginWiFiScan(
          true
        );


        break;
      }


      if (
        hit(
          x,
          y,
          logoutButton
        )
      ) {

        if (
          logoutArmed
        ) {

          performLogout();

        } else {

          logoutArmed =
            true;


          drawSessionScreen();
        }
      }

      break;


    default:

      break;
  }
}


// ============================================================
//                      CONNECT WI-FI
// ============================================================

bool connectWiFi() {

  if (
    savedWiFiSSID.length() ==
    0
  ) {

    return false;
  }


  return connectWiFiCredentials(
    savedWiFiSSID,
    savedWiFiPassword
  );
}


bool connectWiFiCredentials(
  const String &ssid,
  const String &password
) {

  showStatus(
    "WI-FI",
    "Connecting to " + ssid,
    COL_LIGHTBLUE
  );


  WiFi.mode(
    WIFI_STA
  );


  WiFi.setAutoReconnect(
    true
  );


  WiFi.disconnect(
    false,
    false
  );


  delay(
    100
  );


  if (
    !WiFi.config(
      INADDR_NONE,
      INADDR_NONE,
      INADDR_NONE,
      PRIMARY_DNS,
      SECONDARY_DNS
    )
  ) {

    Serial.println(
      "Failed to configure DNS."
    );
  }


  WiFi.begin(
    ssid.c_str(),
    password.length()
      ? password.c_str()
      : nullptr
  );


  unsigned long started =
    millis();


  while (

    WiFi.status() !=
      WL_CONNECTED

    &&

    millis() - started <
      20000

  ) {

    delay(
      100
    );
  }


  if (
    WiFi.status() !=
    WL_CONNECTED
  ) {

    return false;
  }


  Serial.println();
  Serial.println(
    "Wi-Fi connected."
  );


  Serial.print(
    "Pager IP: "
  );


  Serial.println(
    WiFi.localIP()
  );


  Serial.print(
    "Primary DNS: "
  );


  Serial.println(
    WiFi.dnsIP(0)
  );


  Serial.print(
    "Secondary DNS: "
  );


  Serial.println(
    WiFi.dnsIP(1)
  );


  applyTimeSettings();


  return true;
}


void continueAfterWiFiConnection() {

  if (
    sessionValid
  ) {

    pipelineStarted =
      false;


    pipelineConnected =
      false;


    drawHomeScreen();


    startPipeline();


    return;
  }


  if (
    authCookie.length()
  ) {

    if (
      verifyStoredSession()
    ) {

      Serial.println(
        "Saved VRChat session valid."
      );


      drawHomeScreen();


      startPipeline();


      return;
    }


    Serial.println(
      "Stored session invalid."
    );


    clearStoredSession();
  }


  drawLoginScreen();
}


// ============================================================
//                          SETUP
// ============================================================

void setup() {

  Serial.begin(
    115200
  );


  delay(
    300
  );


  Serial.println();
  Serial.println(
    "======================================"
  );

  Serial.println(
    " VRCHAT CYD STABLE PIPELINE PAGER"
  );

  Serial.println(
    "======================================"
  );


  // Backlight

  pinMode(
    TFT_BACKLIGHT,
    OUTPUT
  );


  digitalWrite(
    TFT_BACKLIGHT,
    HIGH
  );


  screenBacklightOn =
    true;


  lastScreenTouch =
    millis();


  notificationWakeUntil =
    0;


  // RGB

  pinMode(
    LED_RED_PIN,
    OUTPUT
  );


  pinMode(
    LED_GREEN_PIN,
    OUTPUT
  );


  pinMode(
    LED_BLUE_PIN,
    OUTPUT
  );


  rgbOff();


  // Audio

  setupAudio();


  // TFT

  tft.init();


  tft.setRotation(
    1
  );


  tft.fillScreen(
    TFT_BLACK
  );


  // Touch

  touchSPI.begin(

    TOUCH_CLK,

    TOUCH_MISO,

    TOUCH_MOSI,

    TOUCH_CS
  );


  ts.begin(
    touchSPI
  );


  ts.setRotation(
    1
  );


  // Splash

  centeredText(
    "VRCHAT",
    160,
    72,
    4,
    COL_LIGHTBLUE
  );


  centeredText(
    "PAGER",
    160,
    113,
    4,
    COL_WHITE
  );


  centeredText(
    "STABLE PIPELINE",
    160,
    158,
    2,
    COL_GREY
  );


  delay(
    650
  );


  // Stored session

  loadSession();


  // Saved Wi-Fi credentials

  loadWiFiSettings();


  // Wi-Fi

  if (
    savedWiFiSSID.length() ==
    0
  ) {

    beginWiFiScan(
      false
    );


    return;
  }


  if (
    !connectWiFi()
  ) {

    Serial.println(
      "Saved Wi-Fi failed; opening setup."
    );


    beginWiFiScan(
      false
    );


    return;
  }


  continueAfterWiFiConnection();
}


// ============================================================
//                           LOOP
// ============================================================

void loop() {

  // ----------------------------------------------------------
  // PRIORITY 1:
  // Service the WebSocket as often as possible.
  // ----------------------------------------------------------

  if (
    sessionValid &&
    WiFi.status() ==
      WL_CONNECTED &&
    pipelineStarted &&
    pipelineConnected
  ) {

    pipelineSocket.poll();
  }


  // ----------------------------------------------------------
  // If authenticated but Pipeline has never been started,
  // initialise it.
  // ----------------------------------------------------------

  if (
    sessionValid &&
    WiFi.status() ==
      WL_CONNECTED &&
    !pipelineStarted
  ) {

    startPipeline();
  }


  // ----------------------------------------------------------
  // ArduinoWebsockets reconnect control.
  // ----------------------------------------------------------

  if (
    sessionValid &&
    WiFi.status() ==
      WL_CONNECTED &&
    pipelineStarted &&
    !pipelineConnected &&
    millis() -
      pipelineLastDisconnect >=
      PIPELINE_RECONNECT_MS
  ) {

    pipelineLastDisconnect =
      millis();


    connectPipelineNow();
  }


  // ----------------------------------------------------------
  // Touch
  // ----------------------------------------------------------

  int touchX;

  int touchY;


  bool touched =

    readTouch(
      touchX,
      touchY
    );


  if (
    touched &&
    !touchWasDown
  ) {

    touchWasDown =
      true;


    touchStartX =
      touchX;


    touchStartY =
      touchY;


    touchLastX =
      touchX;


    touchLastY =
      touchY;


    touchStartScreen =
      currentScreen;


    touchWakeConsumed =
      false;


    if (
      !screenBacklightOn
    ) {

      // The first touch only wakes the display. It must not
      // accidentally activate the button beneath the finger.

      lastScreenTouch =
        millis();


      notificationWakeUntil =
        0;


      setScreenBacklight(
        true
      );


      touchWakeConsumed =
        true;

    } else {

      lastScreenTouch =
        millis();


      notificationWakeUntil =
        0;


      if (
        currentScreen !=
          SCREEN_EVENTS &&
        currentScreen !=
          SCREEN_GROUPS &&
        currentScreen !=
          SCREEN_DETAIL
      ) {

        handleTouch(
          touchX,
          touchY
        );
      }
    }
  }


  if (
    touched &&
    touchWasDown
  ) {

    touchLastX =
      touchX;


    touchLastY =
      touchY;
  }


  if (
    !touched &&
    touchWasDown
  ) {

    if (
      !touchWakeConsumed &&
      currentScreen ==
        touchStartScreen &&
      (
        touchStartScreen ==
          SCREEN_EVENTS ||
        touchStartScreen ==
          SCREEN_GROUPS ||
        touchStartScreen ==
          SCREEN_DETAIL
      )
    ) {

      int dragY =
        touchLastY -
        touchStartY;


      bool middleDrag =

        touchStartX >=
          45 &&
        touchStartX <=
          275 &&
        abs(
          dragY
        ) >=
          25;


      if (
        middleDrag
      ) {

        if (
          touchStartScreen ==
          SCREEN_EVENTS
        ) {

          int maximumOffset =

            max(
              0,
              timelineEventCount - 5
            );


          eventScrollOffset =

            constrain(
              eventScrollOffset +
                (
                  dragY < 0
                    ? 5
                    : -5
                ),
              0,
              maximumOffset
            );


          drawEventsScreen();

        } else if (
          touchStartScreen ==
            SCREEN_GROUPS
        ) {

          int maximumOffset =

            max(
              0,
              groupEventCount - 5
            );


          groupScrollOffset =

            constrain(
              groupScrollOffset +
                (
                  dragY < 0
                    ? 5
                    : -5
                ),
              0,
              maximumOffset
            );


          drawGroupsScreen();

        } else {

          int maximumOffset =

            max(
              0,
              detailLineCount - 8
            );


          detailScrollOffset =

            constrain(
              detailScrollOffset +
                (
                  dragY < 0
                    ? 5
                    : -5
                ),
              0,
              maximumOffset
            );


          drawDetailScreen();
        }

      } else {

        handleTouch(
          touchStartX,
          touchStartY
        );
      }
    }

    touchWasDown =
      false;
  }


  serviceScreenTimeout();


  // ----------------------------------------------------------
  // Wi-Fi state tracking
  // ----------------------------------------------------------

  static bool previousWiFi =
    true;


  bool wifiConnected =

    WiFi.status() ==
    WL_CONNECTED;


  if (
    previousWiFi &&
    !wifiConnected
  ) {

    Serial.println(
      "[WI-FI] Lost"
    );


    pipelineConnected =
      false;


    pipelineStarted =
      false;


    drawPipelineBadge();
  }


  if (
    !previousWiFi &&
    wifiConnected
  ) {

    Serial.println(
      "[WI-FI] Restored"
    );


    pipelineStarted =
      false;


    drawPipelineBadge();
  }


  previousWiFi =
    wifiConnected;


  if (
    !wifiConnected
  ) {

    static unsigned long lastWiFiRetry =
      0;


    if (
      millis() -
      lastWiFiRetry >
      5000
    ) {

      lastWiFiRetry =
        millis();


      WiFi.reconnect();
    }
  }


  // ----------------------------------------------------------
  // IMPORTANT:
  //
  // Deliberately NO delay(5), delay(10), etc. here.
  //
  // Give the network stack a scheduler opportunity without
  // deliberately sleeping the WebSocket servicing loop.
  // ----------------------------------------------------------

  yield();
}
