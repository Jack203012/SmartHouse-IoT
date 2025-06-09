#include <WiFi.h>
#include <DHT.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#define DHTPIN 0 // cảm biến nhiệt độ
#define DHTTYPE DHT22
#define LED_PIN 12
#define LED_PIN_2 16
#define LED_PIN_3 22
#define PIRPIN 32     // cảm biến chuyển động
#define GASPIN 34     // cảm biến khí gaz
#define BUZZER_PIN 23 // loa
#define BUZZER_CHANNEL 0
#define RELAY_PIN 15
#define RELAY_PIN_2 4

DHT dht(DHTPIN, DHTTYPE);
// wifi setup
const char *ssid = "Wokwi-GUEST";
const char *password = "";
// MQTT server setup
const char *mqtt_server = "427cf6c836f74f49b885f5cbf568658e.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char *mqtt_username = "JacJack";
const char *mqtt_password = "Giang30122003";
const char *subtopic = "mqtt_subs";
const char *pubtopic = "mqtt_public";

// loa setup
long previousMillis = 0;
bool isBuzzing = false;
bool turnOffWarning = false;

// thời gian
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 * 3600);

int defaultTemp = 33;
float currTemp = 0;
float currHum = 0;
bool currMotion = false;
int currGaz = 0;
long sendDataDelay = 0;

// setup chức năng đèn
bool isAutoLight = false; // true = có chuyển động bật đèm | false = bật đèn luôn luôn
int LightTimeOn = 18;
int LightTimeOff = 6;
bool lightState = false;

WiFiClientSecure espClient;
PubSubClient client(espClient);

void WifiSetup()
{
  Serial.println("Kết nối tới wifi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("Kết nối thành công!");
}

void reconnect()
{
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ArduinoClient", mqtt_username, mqtt_password))
    {
      Serial.println("Connected to MQTT server");
      client.subscribe(pubtopic);
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}
void AutoLight(String time, bool isMotion) // done
{
  if (!isAutoLight)
  {
    int timeInt = time.toInt();
    bool lightShouldBeOn = false;

    if (LightTimeOn < LightTimeOff)
    {
      // Khoảng sáng trong cùng ngày
      lightShouldBeOn = (timeInt >= LightTimeOn && timeInt < LightTimeOff);
    }
    else
    {
      // Khoảng sáng qua đêm
      lightShouldBeOn = (timeInt >= LightTimeOn || timeInt < LightTimeOff);
    }

    digitalWrite(LED_PIN, lightShouldBeOn ? HIGH : LOW);
  }
  else
  {
    int timeInt = time.toInt();
    bool lightShouldBeOn = false;
    if (LightTimeOn < LightTimeOff)
    {
      // Khoảng sáng trong cùng ngày (VD: 8h–20h)
      lightShouldBeOn = (timeInt >= LightTimeOn && timeInt < LightTimeOff);
      if (lightShouldBeOn)
      {
        if (isMotion)
        {
          analogWrite(LED_PIN, 255);
        }

        else
        {
          analogWrite(LED_PIN, 70);
        }
      }
    }
    else
    {
      // Khoảng sáng qua đêm (VD: 18h–6h)
      lightShouldBeOn = (timeInt >= LightTimeOn || timeInt < LightTimeOff);
      if (lightShouldBeOn)
      {
        if (isMotion)
        {
          analogWrite(LED_PIN, 255);
        }

        else
        {
          analogWrite(LED_PIN, 0);
        }
      }
    }
  }
}

void FireWarning(int gaz, long currTime)
{
  if (turnOffWarning)
  {
    turnOffAlarm();
  }
  else if (gaz > 1000)
  {
    soundAlarm(currTime);
  }
  else if (gaz > 4000)
  {
    soundAlarm(currTime);
    digitalWrite(RELAY_PIN_2, HIGH);
  }
  else
  {
    turnOffAlarm();
  }
}

void TurnOnFan(int temp)
{
  if (temp > defaultTemp)
  {
    digitalWrite(LED_PIN_2, HIGH);
    digitalWrite(RELAY_PIN, HIGH);
  }
  else
  {
    digitalWrite(LED_PIN_2, LOW);
    digitalWrite(RELAY_PIN, LOW);
  }
}
void callback(char *pubtopic, byte *payload, unsigned int length)
{
  String message = "";
  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }
  if (message.startsWith("AutoLight"))
  {
    String value = message.substring(message.indexOf(":") + 1); // Lấy phần sau dấu ':'
    isAutoLight = value.toInt();
  }
  if (message.startsWith("Warning"))
  {
    String value = message.substring(message.indexOf(":") + 1); // Lấy phần sau dấu ':'
    turnOffWarning = value.toInt();
  }
  if (message.startsWith("fanOnTemp"))
  {
    String value = message.substring(message.indexOf(":") + 1); // Lấy phần sau dấu ':'
    defaultTemp = value.toInt();
  }
  if (message.startsWith("LightOnTime"))
  {
    int onTimeStart = message.indexOf(":") + 1;
    int onTimeEnd = message.indexOf("|") - 1;
    String onTimeStr = message.substring(onTimeStart, onTimeEnd);

    // Tìm vị trí của dấu ":" thứ hai
    int offTimeStart = message.lastIndexOf(":") + 1;
    String offTimeStr = message.substring(offTimeStart);

    // Chuyển sang số nguyên
    int LightTimeOn = onTimeStr.toInt();
    int LightTimeOff = offTimeStr.toInt();
  }
  // Serial.println(message);
}
String getFormattedTime()
{
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  if (epochTime < 1672531200)
  {
    return "Chờ cập nhật thời gian...";
  }
  time_t timeNow = (time_t)epochTime;
  struct tm *ptm = localtime(&timeNow);
  char timeString[5];
  sprintf(timeString, "%02d", ptm->tm_hour);
  return String(timeString);
}
void turnOffAlarm()
{
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(RELAY_PIN_2, LOW);
  digitalWrite(LED_PIN_3, LOW);
  previousMillis = 0;
  isBuzzing = false;
}
void soundAlarm(long currentMillis)
{
  if (isBuzzing)
  {
    if (currentMillis - previousMillis >= 2000)
    { // Sau 2s tắt buzzer
      digitalWrite(BUZZER_PIN, LOW);
      digitalWrite(LED_PIN_3, HIGH);
      isBuzzing = false;
      previousMillis = currentMillis;
    }
  }
  else
  {
    if (currentMillis - previousMillis >= 1000)
    { // Sau 1s bật lại buzzer
      digitalWrite(BUZZER_PIN, HIGH);
      digitalWrite(LED_PIN_3, LOW);
      isBuzzing = true;
      previousMillis = currentMillis;
    }
  }
}

void setup()
{
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(LED_PIN_2, OUTPUT);
  digitalWrite(LED_PIN_2, LOW);

  pinMode(LED_PIN_3, OUTPUT);
  digitalWrite(LED_PIN_3, LOW);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(RELAY_PIN_2, OUTPUT);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  dht.begin();
  Serial.println("Khởi động cảm biến nhiệt độ....");
  pinMode(PIRPIN, INPUT);
  Serial.println("Khởi động cảm biến chuyển động...");
  pinMode(GASPIN, INPUT);
  Serial.println("Khởi động cảm biến khí gaz...");
  WifiSetup();
  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  timeClient.begin();
}
long dhtDelayReadTime = 0;

void loop()
{
  long currentTime = millis();
  if (!client.connected())
  {
    reconnect();
  }
  client.loop();
  // Đọc dữ liệu từ cảm biến DHT
  // test code set thời gian ------------------------------------
  timeClient.update();
  String timenow = getFormattedTime();
  if (currentTime - dhtDelayReadTime >= 5000)
  {
    int gasLevel = analogRead(GASPIN);
    bool motionDetected = digitalRead(PIRPIN); // Giả sử bạn có cảm biến chuyển động
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();
    currTemp = temperature;
    currHum = humidity;
    currGaz = gasLevel;
    currMotion = motionDetected;
    if (isnan(humidity) || isnan(temperature) || isnan(gasLevel))
    {
      Serial.println("Failed to read f-rom sensor!");
    }
    else if (currentTime - sendDataDelay >= 10000)
    {
      // Tạo đối tượng JSON để chứa các dữ liệu cảm biến
      StaticJsonDocument<200> doc;
      doc["Temperature"] = temperature;
      doc["Humidity"] = humidity;
      doc["Gaz"] = gasLevel; // Giả sử bạn đọc dữ liệu từ cảm biến khí gas ở đây
      doc["Motion"] = motionDetected ? "Phát hiện" : "Không phát hiện";

      // Chuyển đối tượng JSON thành chuỗi và gửi lên MQTT
      char jsonBuffer[256];
      serializeJson(doc, jsonBuffer);
      client.publish(subtopic, jsonBuffer);
      sendDataDelay = currentTime;
    }
    dhtDelayReadTime = currentTime;
  }
  FireWarning(currGaz, currentTime);
  AutoLight(timenow, currMotion);
  TurnOnFan(currTemp);

  // delay(100); // Gửi dữ liệu mỗi 5 giây
}
