#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <WiFiUdp.h>
#include <string.h>
#include <time.h>

#include "config.local.h"

// Valores particulares de la red y el bot viven en config.local.h.
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
String allowedChatId = ALLOWED_CHAT_ID;
byte pcMac[] = PC_MAC_BYTES;

static_assert(sizeof(pcMac) == 6, "La MAC de destino debe tener 6 bytes");

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);
WiFiUDP udp;

unsigned long lastCheck = 0;
const unsigned long checkInterval = 1000;

bool sendWakeOnLan() {
  byte packet[102];

  for (int i = 0; i < 6; i++) {
    packet[i] = 0xFF;
  }

  for (int i = 1; i <= 16; i++) {
    memcpy(&packet[i * 6], pcMac, 6);
  }

  if (!udp.beginPacket(WOL_BROADCAST_IP, 9)) return false;
  const size_t written = udp.write(packet, sizeof(packet));
  const bool sent = udp.endPacket() == 1;
  return written == sizeof(packet) && sent;
}

void handleMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;

    if (allowedChatId.length() == 0) {
      if (text == "/id") {
        bot.sendMessage(chat_id, "Tu chat ID es: " + chat_id, "");
      } else {
        bot.sendMessage(chat_id, "Configura ALLOWED_CHAT_ID antes de usar este bot.", "");
      }
      continue;
    }

    if (chat_id != allowedChatId) {
      bot.sendMessage(chat_id, "No autorizado.", "");
      continue;
    }

    if (text == "/start") {
      String msg = "ESP32 Wake-on-LAN listo.\n\n";
      msg += "Comandos disponibles:\n";
      msg += "/wake - Encender PC\n";
      msg += "/status - Ver estado del ESP32\n";
      msg += "/id - Ver tu chat ID";
      bot.sendMessage(chat_id, msg, "");
    }

    else if (text == "/wake") {
      if (sendWakeOnLan()) {
        bot.sendMessage(chat_id, "Paquete Wake-on-LAN enviado. Esto no confirma que la PC haya encendido.", "");
      } else {
        bot.sendMessage(chat_id, "No se pudo enviar el paquete Wake-on-LAN.", "");
      }
    }

    else if (text == "/status") {
      String msg = "ESP32 conectado.\n";
      msg += "WiFi: ";
      msg += WiFi.SSID();
      msg += "\nIP: ";
      msg += WiFi.localIP().toString();
      msg += "\nRSSI: ";
      msg += String(WiFi.RSSI());
      msg += " dBm";
      bot.sendMessage(chat_id, msg, "");
    }

    else if (text == "/id") {
      bot.sendMessage(chat_id, "Tu chat ID es: " + chat_id, "");
    }

    else {
      bot.sendMessage(chat_id, "Comando no reconocido. Usa /wake", "");
    }
  }
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi conectado");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // Si config.local.h proporciona la CA del servidor, se valida TLS.
  // Sin ella se conserva el comportamiento original: TLS sin validación.
#ifdef TELEGRAM_ROOT_CA
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  const unsigned long timeSyncStart = millis();
  while (time(nullptr) < 1609459200 && millis() - timeSyncStart < 20000) {
    delay(500);
  }
  if (time(nullptr) < 1609459200) {
    Serial.println("No se pudo sincronizar la hora para validar TLS. Reinicia cuando haya acceso NTP.");
    while (true) delay(1000);
  }
  secured_client.setCACert(TELEGRAM_ROOT_CA);
#else
  secured_client.setInsecure();
#endif

  udp.begin(9);

  Serial.println("Bot Telegram listo");
}

void loop() {
  if (millis() - lastCheck > checkInterval) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    while (numNewMessages) {
      handleMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }

    lastCheck = millis();
  }
}
