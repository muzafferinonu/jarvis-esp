/*
  PRİZ — servo, anahtarlı prizin aç/kapa düğmesine fiziksel olarak basar. 230 V'a HİÇ dokunulmaz.
  Jarvis (priz.py) ile aynı adresler: /ac?k=  /kapat?k=  /durum?k=  /test?k=&aci=  /ayar?k=&ac=&kapat=&orta=
  k = cihaz kurulumunda verdiğiniz yönetici şifresi (Jarvis'te Ev Cihazları kartındaki "Priz anahtarı" ile aynı olmalı).
  BAĞLANTI: servo kahverengi→GND, kırmızı→5V (VIN), sinyal→ayardaki pin (ESP32: 13, C3: 4). 470 µF kondansatör servoya yakın 5V-GND arası.
*/
#include <JarvisCekirdek.h>
#include <ESP32Servo.h>
#include <WiFi.h>

static Servo servo;
static String durum = "bilinmiyor";
static const int BASMA_MS = 600;
#if CONFIG_IDF_TARGET_ESP32C3
static const char* VARS_PIN = "4";
#else
static const char* VARS_PIN = "13";
#endif

static int pinNo() { return jarvis::ayarSayi("servo_pin"); }
static int aci(const char* a) { return constrain(jarvis::ayarSayi(a), 0, 180); }

static void git(int hedef, int bekle = BASMA_MS) {
  servo.attach(pinNo(), 500, 2400);
  servo.write(constrain(hedef, 0, 180));
  delay(bekle);
  servo.write(aci("orta"));
  delay(400);
  servo.detach();  // vızıldamasın, ısınmasın
}

static String js() {
  return "{\"durum\":\"" + durum + "\",\"ac\":" + String(aci("ac")) + ",\"kapat\":" + String(aci("kapat")) + ",\"orta\":" + String(aci("orta")) +
         ",\"ip\":\"" + WiFi.localIP().toString() + "\",\"sinyal\":" + String(WiFi.RSSI()) + "}";
}

static void cevap() {
  if (jarvis::sunucu.hasArg("g")) { jarvis::sunucu.sendHeader("Location", "/"); jarvis::sunucu.send(303); }
  else jarvis::sunucu.send(200, "application/json", js());
}

void setup() {
  jarvis::ayarAlani("servo_pin", "Servo sinyal pini", VARS_PIN);
  jarvis::ayarAlani("ac", "Açma açısı (0-180)", "60");
  jarvis::ayarAlani("kapat", "Kapatma açısı (0-180)", "120");
  jarvis::ayarAlani("orta", "Bekleme (orta) açısı", "90");
  jarvis::baslat("priz", "Işık prizi (servo düğmeye basar)");
  durum = jarvis::ayarOku("durum");
  if (!durum.length()) durum = "bilinmiyor";
  ESP32PWM::allocateTimer(0);
  servo.setPeriodHertz(50);
  git(aci("orta"), 300);
  jarvis::durumEkle([]() { return String("\"isik\":\"") + durum + "\",\"durum\":\"" + durum + "\""; });
  jarvis::sayfaEkle([]() {
    return "<b>Işık: " + durum + "</b><br><form method=post action='/ac?g=1' style=display:inline><input type=submit value='AÇ'></form>"
           "<form method=post action='/kapat?g=1' style=display:inline><input class=r type=submit value='KAPAT'></form>"
           "<br><small>Açıları aşağıdaki ayarlardan değiştirin; denemek için /test?aci=70 adresini açın.</small>";
  });
  auto& s = jarvis::sunucu;
  s.on("/ac", []() { if (!jarvis::yetkiIste()) return; git(aci("ac")); durum = "acik"; jarvis::ayarYaz("durum", durum); jarvis::yaz("ışığı açtım"); cevap(); });
  s.on("/kapat", []() { if (!jarvis::yetkiIste()) return; git(aci("kapat")); durum = "kapali"; jarvis::ayarYaz("durum", durum); jarvis::yaz("ışığı kapattım"); cevap(); });
  s.on("/durum", HTTP_GET, []() { if (!jarvis::yetkiIste()) return; jarvis::sunucu.send(200, "application/json", js()); });
  s.on("/test", []() {
    if (!jarvis::yetkiIste()) return;
    int a = jarvis::sunucu.hasArg("aci") ? jarvis::sunucu.arg("aci").toInt() : aci("orta");
    git(a, 1000);
    jarvis::sunucu.send(200, "text/plain", "test: " + String(a) + " derece, sonra orta");
  });
  s.on("/ayar", []() {
    if (!jarvis::yetkiIste()) return;
    for (const char* k : {"ac", "kapat", "orta"}) if (jarvis::sunucu.hasArg(k)) jarvis::ayarYaz(k, String(constrain(jarvis::sunucu.arg(k).toInt(), 0, 180)));
    jarvis::sunucu.send(200, "application/json", js());
  });
}

void loop() { jarvis::dongu(); }
