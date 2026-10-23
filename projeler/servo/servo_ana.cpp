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

#if CONFIG_IDF_TARGET_ESP32C3
static const char* VP[3] = {"4", "5", "6"};
#else
static const char* VP[3] = {"13", "14", "27"};
#endif

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
  jarvis::baslat("servo", "3 servoya kadar deneme ve kontrol");
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
    h += "<small id=m class=m></small><script>document.getElementById('k').value=localStorage.k||'';var t={};"
         "function c(u,n,a){var q=u+'?n='+n+'&k='+encodeURIComponent(localStorage.k||'')+(a!==undefined?'&aci='+a:'');"
         "fetch(q).then(function(r){document.getElementById('m').textContent=r.status==403?'Şifre yanlış':r.ok?'tamam':'hata '+r.status})}"
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
  s.on("/durum", HTTP_GET, []() { if (!jarvis::yetkiIste()) return; jarvis::sunucu.send(200, "application/json", js()); });
}

void loop() { jarvis::dongu(); }
