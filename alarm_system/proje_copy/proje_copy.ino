#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

// Kamera pin tanımlamaları (AI Thinker için) - Değiştirmeyin
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// WiFi bilgilerinizi girin
const char* ssid = "Ayberk";
const char* password = "ayberk2003";

// Flask sunucu bilgileri
const char* serverName = "172.20.10.7"; 
const int serverPort = 5000;

// Multipart boundary string
const char* boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";

// ----------- YENİ EKLEDİKLERİMİZ -----------
bool alarmMode = false;                  // Alarm modda mıyız?
unsigned long alarmEndTime = 0;          // Alarm mod bitiş zamanı (millis)
unsigned long lastCaptureTime = 0;       // Son fotoğraf çekme zamanı
const unsigned long normalInterval = 2500;  // Normal mod çekim aralığı (ms) => 2.5s
const unsigned long alarmInterval = 200;    // Alarm mod çekim aralığı (ms) => 0.2s
const unsigned long alarmDuration = 5000;   // Alarm mod süresi (ms) => 5s
// -------------------------------------------

void setup() {
  Serial.begin(115200);
  
  // Kamera ayarları (Aynen koruyun)
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if(psramFound()){
    Serial.println("PSRAM found");
    config.frame_size    = FRAMESIZE_VGA;  // Çözünürlük
    config.jpeg_quality  = 10;             // Kalite (0=en iyi)
    config.fb_count      = 2;
  } else {
    config.frame_size    = FRAMESIZE_VGA;
    config.jpeg_quality  = 10;
    config.fb_count      = 2;
  }

  // Kamera başlat
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return;
  }

  // Dikey Flip
  sensor_t * s = esp_camera_sensor_get();
  if (s == NULL) {
    Serial.println("Camera sensor get failed!");
  } else {
    s->set_vflip(s, 1);  
    Serial.println("Vertical Flip (V-Flip) aktiflesti!");
  }

  // Wi-Fi bağlanma
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  unsigned long currentTime = millis();

  // ------ ALARM MOD KONTROLÜ ------
  if (alarmMode) {
    // Alarm modda: 0.2s'de bir fotoğraf çekelim (sunucuya yollamadan ve çıktı üretmeden).
    if (currentTime - lastCaptureTime >= alarmInterval) {
      lastCaptureTime = currentTime;

      camera_fb_t * fb = esp_camera_fb_get();
      if (!fb) {
        // Alarm modunda hata oluşsa bile çıktı vermiyoruz
      } else {
        // Fotoğraf çekiliyor ancak işlem yapılmıyor
        esp_camera_fb_return(fb);
      }
    }

    // Alarm süresi bitti mi?
    if (currentTime >= alarmEndTime) {
      // 5 saniye bitti, şimdi sunucuya 1 foto gönderelim:
      camera_fb_t * fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("[ALARM MODE] Fotoğraf çekilemedi.");
      } else {
        Serial.println("[ALARM MODE] 5sn sonunda sunucuya fotoğraf gönderiliyor...");
        
        // -------- API'ye Fotoğraf Gönderimi --------
        Serial.printf("Image size: %d bytes\n", fb->len);

        WiFiClient client;
        if (!client.connect(serverName, serverPort)) {
          Serial.println("Connection to server failed");
          esp_camera_fb_return(fb);
          alarmMode = false;  // Bağlantı yoksa normal moda dön
          return;
        }

        // HTTP POST isteği başlığı
        String header = "";
        header += "POST /detect HTTP/1.1\r\n";
        header += "Host: " + String(serverName) + ":" + String(serverPort) + "\r\n";
        header += "Content-Type: multipart/form-data; boundary=" + String(boundary) + "\r\n";
        header += "Connection: close\r\n";

        // Body
        String body = "";
        body += "--" + String(boundary) + "\r\n";
        body += "Content-Disposition: form-data; name=\"image\"; filename=\"image.jpg\"\r\n";
        body += "Content-Type: image/jpeg\r\n\r\n";

        // Footer
        String footer = "\r\n--" + String(boundary) + "--\r\n";

        size_t contentLength = body.length() + fb->len + footer.length();
        header += "Content-Length: " + String(contentLength) + "\r\n\r\n";

        // İstek gönder
        client.print(header);
        client.print(body);
        client.write(fb->buf, fb->len);
        client.print(footer);

        // Yanıt al
        String response = "";
        while (client.connected()) {
          while (client.available()) {
            char c = client.read();
            response += c;
          }
          if (!client.connected()) break;
        }

        Serial.println("Response:");
        Serial.println(response);

        // JSON parse
        const size_t capacity = JSON_OBJECT_SIZE(2) + 200;
        DynamicJsonDocument doc(capacity);
        DeserializationError error = deserializeJson(doc, response);

        if (error) {
          Serial.print("deserializeJson() failed: ");
          Serial.println(error.f_str());
          alarmMode = false;  // JSON hatası varsa normal moda dön
        } else {
          // Alarm modu kontrolü için "face" veya "person" var mı bakılır
          bool faceOrPerson = false;

          // 1) face kontrolü
          if (doc.containsKey("faces")) {
            int fc = doc["faces"];
            if (fc > 0) faceOrPerson = true;
          }

          // 2) YOLO person kontrolü
          if (doc.containsKey("yolo_detections")) {
            JsonArray detections = doc["yolo_detections"].as<JsonArray>();
            for (JsonObject det : detections) {
              String cls = det["class"];
              if (cls.equalsIgnoreCase("person")) {
                faceOrPerson = true;
                break;
              }
            }
          }

          // Eğer face/person varsa alarm süresini uzat
          if (faceOrPerson) {
            Serial.println("[ALARM MODE] Face veya Person tekrar bulundu, 5 saniye uzatılıyor...");
            alarmEndTime = millis() + alarmDuration;
          } else {
            Serial.println("[ALARM MODE] Face/Person yok, normal moda dönülüyor.");
            alarmMode = false;
          }
        }

        esp_camera_fb_return(fb);
      }
    }
  } else {
    // NORMAL MOD
    // 2.5s'de bir foto çekip sunucuya gönderelim
    if (currentTime - lastCaptureTime >= normalInterval) {
      lastCaptureTime = currentTime;

      camera_fb_t * fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("Camera capture failed");
        return;
      }

      Serial.println("[NORMAL MODE] Fotoğraf çekildi, sunucuya gönderiliyor...");
      Serial.printf("Image size: %d bytes\n", fb->len);

      WiFiClient client;
      if (!client.connect(serverName, serverPort)) {
        Serial.println("Connection to server failed");
        esp_camera_fb_return(fb);
        return;
      }

      // HTTP POST isteği başlığı
      String header = "";
      header += "POST /detect HTTP/1.1\r\n";
      header += "Host: " + String(serverName) + ":" + String(serverPort) + "\r\n";
      header += "Content-Type: multipart/form-data; boundary=" + String(boundary) + "\r\n";
      header += "Connection: close\r\n";

      // Body
      String body = "";
      body += "--" + String(boundary) + "\r\n";
      body += "Content-Disposition: form-data; name=\"image\"; filename=\"image.jpg\"\r\n";
      body += "Content-Type: image/jpeg\r\n\r\n";

      // Footer
      String footer = "\r\n--" + String(boundary) + "--\r\n";

      size_t contentLength = body.length() + fb->len + footer.length();
      header += "Content-Length: " + String(contentLength) + "\r\n\r\n";

      // İstek gönder
      client.print(header);
      client.print(body);
      client.write(fb->buf, fb->len);
      client.print(footer);

      // Yanıt al
      String response = "";
      while (client.connected()) {
        while (client.available()) {
          char c = client.read();
          response += c;
        }
        if (!client.connected()) break;
      }

      Serial.println("Response:");
      Serial.println(response);

      // JSON parse
      const size_t capacity = JSON_OBJECT_SIZE(2) + 200;
      DynamicJsonDocument doc(capacity);
      DeserializationError error = deserializeJson(doc, response);

      if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.f_str());
      } else {
        // Face/Person kontrolü ve Alarm Moduna geçiş
        bool faceOrPerson = false;

        if (doc.containsKey("faces")) {
          int fc = doc["faces"];
          if (fc > 0) faceOrPerson = true;
        }

        if (doc.containsKey("yolo_detections")) {
          JsonArray detections = doc["yolo_detections"].as<JsonArray>();
          for (JsonObject det : detections) {
            String cls = det["class"];
            if (cls.equalsIgnoreCase("person")) {
              faceOrPerson = true;
              break;
            }
          }
        }

        if (faceOrPerson) {
          Serial.println("[NORMAL MODE] Face/Person bulundu => Alarm Moda geçiliyor (5s).");
          alarmMode = true;
          alarmEndTime = millis() + alarmDuration;
        }
      }

      esp_camera_fb_return(fb);
    }
  }
}
