/*
  SERVO — 3 servoya kadar deneme ve kontrol (robot kol, kapı mandalı vb. için temel).
  Sayfa: kaydırıcı ile açı, 0/90/180 düğmeleri, "Tara" (0→180→0), "Bırak" (servo gevşer).
  Jarvis/telefon adresleri (k = yönetici şifresi):
    /servo?k=..&n=1&aci=90      → 1. servoyu 90 dereceye götürür ve tutar
    /tara?k=..&n=1              → 0'dan 180'e gidip döner
    /birak?k=..&n=1             → servoyu gevşetir (n yoksa hepsi)
    /durum?k=..                 → açılar
  BAĞLANTI (tek servo deneme): kahverengi/siyah → G (GND), kırmızı → 5V, turuncu/sarı (sinyal) → ayardaki pin.
    Varsayılan pinler: C3 Super Mini: 4, 5, 6   ·   ESP32 DevKit/NodeMCU: 13, 14, 27
  3 servo birlikte çalışacaksa 5V'u kartın USB'sinden değil ayrı 5V 2A adaptörden verin (GND'ler ortak olsun), yoksa kart yeniden başlar.
*/
#include <JarvisCekirdek.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <Wire.h>
#include <U8g2lib.h>

#if CONFIG_IDF_TARGET_ESP32C3
static const char* VP[3] = {"4", "5", "6"};
#else
static const char* VP[3] = {"13", "14", "27"};
#endif

// ---------------- OLED gözler (128x64 I2C: SSD1306 0,96" ya da SH1106 1,3") ----------------
// Bağlantı: OLED VCC → 3V3, GND → GND, SDA/SCL → ayardaki pinler (ESP32: SDA 21, SCL 22 · C3: SDA 20, SCL 21)
#if CONFIG_IDF_TARGET_ESP32C3
static const char* V_SDA = "20";
static const char* V_SCL = "21";
#else
static const char* V_SDA = "21";
static const char* V_SCL = "22";
#endif
static U8G2* ekran = nullptr;
static int gozX = 0, gozHedef = 0;           // bakış yönü (-14 .. 14)
static unsigned long sonKirp = 0, kirpArasi = 3500, sonBakis = 0, sonCiz = 0, kirpBasla = 0;
static String ifade = "normal";               // normal | mutlu | kizgin | uykulu

static void gozCiz() {
  if (!ekran) return;
  unsigned long n = millis();
  if (n - sonCiz < 40) return;
  sonCiz = n;
  if (n - sonBakis > (unsigned long)random(2500, 6000)) { sonBakis = n; int r = random(0, 5); gozHedef = r == 0 ? -14 : r == 1 ? 14 : 0; }
  gozX += (gozHedef > gozX) ? 2 : (gozHedef < gozX ? -2 : 0);
  if (!kirpBasla && n - sonKirp > kirpArasi) { kirpBasla = n; }
  int h = ifade == "uykulu" ? 10 : 36;
  if (kirpBasla) {  // kırpma: 120 ms kapan, 120 ms açıl
    unsigned long t = n - kirpBasla;
    if (t < 120) h = max(2, (int)(h * (120 - t) / 120));
    else if (t < 240) h = max(2, (int)(h * (t - 120) / 120));
    else { kirpBasla = 0; sonKirp = n; kirpArasi = random(2500, 6500); }
  }
  ekran->clearBuffer();
  const int w = 34, cy = 32, x1 = 30 + gozX, x2 = 98 + gozX;
  for (int g = 0; g < 2; g++) {
    int cx = g ? x2 : x1;
    ekran->drawRBox(cx - w / 2, cy - h / 2, w, h, h > 12 ? 8 : 2);
    ekran->setDrawColor(0);
    if (ifade == "mutlu") ekran->drawBox(cx - w / 2, cy + 4, w, h);                 // alt yarısı kapalı: gülen göz
    if (ifade == "kizgin") ekran->drawTriangle(cx - w / 2 - 1, cy - h / 2 - 1, cx + w / 2 + 1, cy - h / 2 - 1, g ? cx - w / 2 - 1 : cx + w / 2 + 1, cy - 4);  // çatık kaş
    ekran->setDrawColor(1);
  }
  ekran->sendBuffer();
}

static void ekranBaslat() {
  if (jarvis::ayarSayi("oled") != 1) return;
  Wire.begin(jarvis::ayarSayi("sda"), jarvis::ayarSayi("scl"));
  Wire.beginTransmission(0x3C);
  if (Wire.endTransmission() != 0) { jarvis::yaz("OLED bulunamadı (SDA/SCL pinlerini ve 3V3/GND'yi kontrol edin)"); return; }
  if (jarvis::ayarOku("oled_tur") == "sh1106") ekran = new U8G2_SH1106_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
  else ekran = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
  ekran->begin();
  ekran->setContrast(200);
  jarvis::yaz("OLED açıldı, gözler çalışıyor");
}

static Servo sv[3];
static int aci[3] = {-1, -1, -1};

static int pin(int i) { return jarvis::ayarSayi(i == 0 ? "pin1" : i == 1 ? "pin2" : "pin3"); }
static bool aktif(int i) { return pin(i) > 0; }

static void git(int i, int a) {
  if (i < 0 || i > 2 || !aktif(i)) return;
  a = constrain(a, 0, 180);
  if (!sv[i].attached()) sv[i].attach(pin(i), jarvis::ayarSayi("min_us"), jarvis::ayarSayi("max_us"));
  sv[i].write(a);
  aci[i] = a;
}

static void birak(int i) {
  if (i < 0 || i > 2) return;
  if (sv[i].attached()) sv[i].detach();
}

static int nArg() {
  int n = jarvis::sunucu.hasArg("n") ? jarvis::sunucu.arg("n").toInt() : 1;
  return constrain(n, 1, 3) - 1;
}

static String js() {
  String j = "{\"servolar\":[";
  for (int i = 0; i < 3; i++) {
    if (i) j += ",";
    j += "{\"n\":" + String(i + 1) + ",\"pin\":" + String(pin(i)) + ",\"aci\":" + String(aci[i]) + ",\"tutuyor\":" + String(sv[i].attached() ? "true" : "false") + "}";
  }
  return j + "]}";
}

void setup() {
  jarvis::ayarAlani("pin1", "1. servo pini (0 = kullanılmıyor)", VP[0]);
  jarvis::ayarAlani("pin2", "2. servo pini (0 = kullanılmıyor)", VP[1]);
  jarvis::ayarAlani("pin3", "3. servo pini (0 = kullanılmıyor)", VP[2]);
  jarvis::ayarAlani("min_us", "Servo en kısa sinyal (µs)", "500");
  jarvis::ayarAlani("max_us", "Servo en uzun sinyal (µs)", "2400");
  jarvis::ayarAlani("oled", "OLED gözler (1 = açık, 0 = kapalı)", "1");
  jarvis::ayarAlani("oled_tur", "OLED türü: ssd1306 (0,96\") ya da sh1106 (1,3\"; görüntü kayıksa bunu yazın)", "ssd1306");
  jarvis::ayarAlani("sda", "OLED SDA pini", V_SDA);
  jarvis::ayarAlani("scl", "OLED SCL pini", V_SCL);
  jarvis::baslat("servo", "3 servoya kadar deneme ve kontrol + OLED gözler");
  ekranBaslat();
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  for (auto& s : sv) s.setPeriodHertz(50);

  jarvis::durumEkle([]() { String j = js(); return j.substring(1, j.length() - 1); });
  jarvis::sayfaEkle([]() {
    String h = "<b>Servolar</b><br><label>Yönetici şifresi (bir kez yazın, bu telefonda hatırlanır)<input id=k type=password oninput=\"localStorage.k=this.value\"></label>";
    for (int i = 0; i < 3; i++) {
      String n = String(i + 1);
      if (!aktif(i)) { h += "<p class=m>" + n + ". servo kapalı (pin 0)</p>"; continue; }
      h += "<div style='margin:10px 0'><b>" + n + ". servo</b> <small>pin " + String(pin(i)) + "</small> <b id=a" + n + ">" + String(aci[i] < 0 ? 90 : aci[i]) + "°</b><br>"
           "<input type=range min=0 max=180 value=" + String(aci[i] < 0 ? 90 : aci[i]) + " oninput=\"g(" + n + ",this.value)\" style=width:100%><br>"
           "<button onclick=\"g(" + n + ",0)\">0°</button><button onclick=\"g(" + n + ",90)\">90°</button><button onclick=\"g(" + n + ",180)\">180°</button>"
           "<button class=g onclick=\"c('/tara'," + n + ")\">Tara</button><button class=r onclick=\"c('/birak'," + n + ")\">Bırak</button></div>";
    }
    h += "<div style='margin:10px 0'><b>Gözler</b> " + String(ekran ? "" : "<small class=m>(OLED bulunamadı)</small>") + "<br>"
         "<button onclick=\"e('normal')\">Normal</button><button onclick=\"e('mutlu')\">Mutlu</button><button onclick=\"e('kizgin')\">Kızgın</button><button onclick=\"e('uykulu')\">Uykulu</button></div>";
    h += "<small id=m class=m></small><script>document.getElementById('k').value=localStorage.k||'';var t={};"
         "function c(u,n,a){var q=u+'?n='+n+'&k='+encodeURIComponent(localStorage.k||'')+(a!==undefined?'&aci='+a:'');"
         "fetch(q).then(function(r){document.getElementById('m').textContent=r.status==403?'Şifre yanlış':r.ok?'tamam':'hata '+r.status})}"
         "function e(f){fetch('/goz?ifade='+f+'&k='+encodeURIComponent(localStorage.k||''))}"
         "function g(n,a){document.getElementById('a'+n).textContent=a+'°';clearTimeout(t[n]);t[n]=setTimeout(function(){c('/servo',n,a)},60)}</script>";
    return h;
  });

  auto& s = jarvis::sunucu;
  s.on("/servo", []() {
    if (!jarvis::yetkiIste()) return;
    int i = nArg();
    git(i, jarvis::sunucu.hasArg("aci") ? jarvis::sunucu.arg("aci").toInt() : 90);
    jarvis::sunucu.send(200, "application/json", js());
  });
  s.on("/tara", []() {
    if (!jarvis::yetkiIste()) return;
    int i = nArg();
    for (int a = 0; a <= 180; a += 5) { git(i, a); delay(25); }
    for (int a = 180; a >= 0; a -= 5) { git(i, a); delay(25); }
    git(i, 90);
    jarvis::yaz(String(i + 1) + ". servoyu taradım");
    jarvis::sunucu.send(200, "application/json", js());
  });
  s.on("/birak", []() {
    if (!jarvis::yetkiIste()) return;
    if (jarvis::sunucu.hasArg("n")) birak(nArg());
    else for (int i = 0; i < 3; i++) birak(i);
    jarvis::sunucu.send(200, "application/json", js());
  });
  s.on("/goz", []() {  // /goz?k=..&ifade=mutlu|kizgin|uykulu|normal&yon=sol|sag|orta
    if (!jarvis::yetkiIste()) return;
    if (jarvis::sunucu.hasArg("ifade")) ifade = jarvis::sunucu.arg("ifade");
    if (jarvis::sunucu.hasArg("yon")) { String y = jarvis::sunucu.arg("yon"); gozHedef = y == "sol" ? -14 : y == "sag" ? 14 : 0; sonBakis = millis(); }
    kirpBasla = millis();
    jarvis::sunucu.send(200, "application/json", "{\"ifade\":\"" + ifade + "\",\"oled\":" + String(ekran ? "true" : "false") + "}");
  });
  s.on("/durum", HTTP_GET, []() { if (!jarvis::yetkiIste()) return; jarvis::sunucu.send(200, "application/json", js()); });
}

void loop() {
  jarvis::dongu();
  gozCiz();
}
