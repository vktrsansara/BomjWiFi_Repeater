#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

// Подключаем NAPT
#include <lwip/napt.h>

#define AP_POWER 20.5    // Мощность сигнала AP (0-20.5, где 20.5 - максимум)
#define AP_MAX_CLIENTS 4 // Максимальное количество клиентов, которые могут одновременно подключиться к AP
#define WIFI_FAST_SCAN   // Быстрое сканирование WiFi (не сканирует все каналы, а только 1-6, что ускоряет процесс подключения к роутеру)

const char *AP_SSID = "ESP8266_Repeater_Setup"; // SSID для режима настройки (AP)
const char *AP_PASS = "";                       // Пароль для режима настройки (оставляем пустым для открытой сети)
const byte DNS_PORT = 53;                       // Порт для DNS сервера
IPAddress apIP(192, 168, 4, 1);                 // IP адрес для точки доступа (AP)

struct Config
{
  char wifi_ssid[32];
  char wifi_pass[64];
  char ap_ssid[32];
  char ap_pass[64];
  bool configured;
} config;

DNSServer dnsServer;
ESP8266WebServer server(80);

void loadConfig()
{
  EEPROM.begin(512);
  EEPROM.get(0, config);
  if (config.configured != true || strlen(config.wifi_ssid) == 0)
  {
    config.configured = false;
    memset(config.wifi_ssid, 0, sizeof(config.wifi_ssid));
    memset(config.wifi_pass, 0, sizeof(config.wifi_pass));
    strncpy(config.ap_ssid, "ESP8266_Repeater", sizeof(config.ap_ssid) - 1);
    strncpy(config.ap_pass, "12345678", sizeof(config.ap_pass) - 1);
  }
}

void saveConfig()
{
  EEPROM.put(0, config);
  EEPROM.commit();
}

void handleRoot()
{
  if (server.hasArg("save"))
  {
    strncpy(config.wifi_ssid, server.arg("wifi_ssid").c_str(), sizeof(config.wifi_ssid) - 1);
    strncpy(config.wifi_pass, server.arg("wifi_pass").c_str(), sizeof(config.wifi_pass) - 1);
    strncpy(config.ap_ssid, server.arg("ap_ssid").c_str(), sizeof(config.ap_ssid) - 1);
    strncpy(config.ap_pass, server.arg("ap_pass").c_str(), sizeof(config.ap_pass) - 1);
    config.configured = true;
    saveConfig();

    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'></head>";
    html += "<body><h2 style='text-align:center'>✅ Сохранено! Перезагрузка...</h2></body></html>";
    server.send(200, "text/html", html);
    delay(1000);
    ESP.restart();
    return;
  }

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial;margin:0;padding:20px;background:#f0f0f0}.container{max-width:500px;margin:auto;background:white;padding:20px;border-radius:10px}input{width:100%;padding:8px;margin-bottom:15px;box-sizing:border-box;}</style></head><body>";
  html += "<div class='container'><h1>Настройка WiFi репитера</h1><form method='post'>";
  html += "<label>🌐 WiFi роутер:</label><input type='text' name='wifi_ssid' value='" + String(config.wifi_ssid) + "' required>";
  html += "<input type='password' name='wifi_pass' value='" + String(config.wifi_pass) + "'>";
  html += "<label>📡 Имя сети репитера:</label><input type='text' name='ap_ssid' value='" + String(config.ap_ssid) + "' required>";
  html += "<input type='password' name='ap_pass' value='" + String(config.ap_pass) + "' minlength='8'>";
  html += "<input type='submit' name='save' value='💾 Сохранить' style='background:#4CAF50;color:white;border:none;height:40px;'></form></div></body></html>";
  server.send(200, "text/html", html);
}

void handleNotFound()
{
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
  server.send(302, "text/plain", "");
}

void startConfigMode()
{
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASS);
  dnsServer.start(DNS_PORT, "*", apIP);
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();
}

void startRepeaterMode()
{
  Serial.println("Запуск режима ретранслятора...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifi_ssid, config.wifi_pass);

  int attempts = 0;
  Serial.print("Подключение к WiFi роутеру");
  while (WiFi.status() != WL_CONNECTED && attempts < 30)
  {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("\nНе удалось подключиться к роутеру. Включаю режим настройки.");
    startConfigMode();
    return;
  }

  Serial.println("\nПодключено к роутеру!");
  Serial.print("STA IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("DNS от роутера: ");
  Serial.println(WiFi.dnsIP(0));

  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(config.ap_ssid, config.ap_pass, 1, false, AP_MAX_CLIENTS);

  auto &dhcpServer = WiFi.softAPDhcpServer();

  if (WiFi.dnsIP(0) != INADDR_NONE && WiFi.dnsIP(0) != IPAddress(0, 0, 0, 0))
  {
    dhcpServer.setDns(WiFi.dnsIP(0));
    Serial.print("DNS для клиентов установлен на DNS роутера: ");
    Serial.println(WiFi.dnsIP(0));
  }
  else
  {
    IPAddress googleDNS(8, 8, 8, 8);
    dhcpServer.setDns(googleDNS);
    Serial.println("DNS для клиентов установлен на Google DNS: 8.8.8.8");
  }

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);

  if (WiFi.dnsIP(0) != INADDR_NONE && WiFi.dnsIP(0) != IPAddress(0, 0, 0, 0))
  {
    dnsServer.enableForwarder("*", WiFi.dnsIP(0));
  }
  else
  {
    dnsServer.enableForwarder("*", IPAddress(8, 8, 8, 8));
  }

  dnsServer.start(DNS_PORT, "*", apIP);
  Serial.println("DNS Server с Forwarder запущен");

  err_t ret = ip_napt_init(1000, 30);
  if (ret == ERR_OK)
  {
    ret = ip_napt_enable_no(SOFTAP_IF, 1);
    if (ret == ERR_OK)
    {
      Serial.println("NAT активирован");
    }
    else
    {
      Serial.println("Ошибка активации NAT");
    }
  }
  else
  {
    Serial.println("Ошибка инициализации NAT");
  }

  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();
}

void setup()
{
  Serial.begin(115200);
  delay(100);

  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setOutputPower(AP_POWER);

  WiFi.setAutoReconnect(false);

  loadConfig();

  if (!config.configured)
  {
    startConfigMode();
  }
  else
  {
    startRepeaterMode();
  }
}

void loop()
{
  dnsServer.processNextRequest();
  server.handleClient();

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 10000)
  {
    lastCheck = millis();
    if (WiFi.status() != WL_CONNECTED && config.configured)
    {
      Serial.println("Потеряно соединение с роутером. Переподключение...");
      WiFi.reconnect();
    }
  }
}
