#include "JarvisCekirdek.h"
#include "imza_anahtari.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_mac.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pk.h>
#include <mbedtls/base64.h>
#include <vector>

// Yeni sürüm açılınca hemen "sağlam" sayılmasın: Wi-Fi'ye bağlanınca biz onaylarız; bağlanamazsa eski sürüme dönülür.
extern "C" bool verifyRollbackLater() { return true; }

namespace jarvis {

WebServer sunucu(80);
static Preferences cek;    // çekirdek ayarları (Wi-Fi, ad, şifre)
static Preferences prj;    // proje ayarları
static DNSServer dns;
static String gProje, gAciklama, gId, gAd, gAnahtar, gSonKontrol = "henüz bakılmadı", gHostname;
static bool gPortal = false, gOnaylandi = false;
static unsigned long gAcilis = 0, gSonGuncelleme = 0, gSonWifi = 0, gPortalBasladi = 0;
static uint32_t gAralik = 15UL * 60UL * 1000UL;
static bool gSimdiBak = false;
struct Alan { String a, etiket, vars; };
static std::vector<Alan> gAlanlar;
static std::vector<std::function<String()>> gDurum, gSayfa;
static String gOlaylar[8];
static int gOlayN = 0;

#if CONFIG_IDF_TARGET_ESP32C3
static const int LED = 8;          // C3 Super Mini: mavi LED, LOW = yanar
static const bool LED_TERS = true;
#else
static const int LED = 2;
static const bool LED_TERS = false;
#endif

static void led(bool on) { digitalWrite(LED, (on ^ LED_TERS) ? HIGH : LOW); }

String json(const String& s) {
  String o;
  o.reserve(s.length() + 4);
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if ((uint8_t)c >= 0x20) o += c;
  }
  return o;
}

static String html(const String& s) {
  String o = s;
  o.replace("&", "&amp;"); o.replace("<", "&lt;"); o.replace(">", "&gt;"); o.replace("\"", "&quot;"); o.replace("'", "&#39;");
  return o;
}

void yaz(const String& s) {
  Serial.println("[jarvis] " + s);
  unsigned long sn = millis() / 1000;
  gOlaylar[gOlayN % 8] = String(sn / 3600) + ":" + String((sn / 60) % 60) + ":" + String(sn % 60) + " " + s;
  gOlayN++;
}

String cihazId() { return gId; }
String cihazAdi() { return gAd; }
bool baglandi() { return WiFi.status() == WL_CONNECTED; }

static String slug(String s) {
  s.toLowerCase();
  const char* tr[][2] = {{"ç", "c"}, {"ğ", "g"}, {"ı", "i"}, {"ö", "o"}, {"ş", "s"}, {"ü", "u"}, {"Ç", "c"}, {"Ğ", "g"}, {"İ", "i"}, {"Ö", "o"}, {"Ş", "s"}, {"Ü", "u"}};
  for (auto& p : tr) s.replace(p[0], p[1]);
  String o;
  for (char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) o += c;
    else if ((c == ' ' || c == '-' || c == '_') && o.length() && !o.endsWith("-")) o += '-';
  }
  while (o.endsWith("-")) o.remove(o.length() - 1);
  return o.substring(0, 24);
}

bool anahtarDogru() {
  if (!gAnahtar.length()) return true;  // şifre hiç konmamışsa (kurulum bitmemiş) açık
  if (sunucu.hasArg("k") && sunucu.arg("k") == gAnahtar) return true;
  return sunucu.authenticate("jarvis", gAnahtar.c_str());
}

bool yetkiIste() {
  if (anahtarDogru()) return true;
  if (sunucu.hasArg("k")) sunucu.send(403, "text/plain", "anahtar yanlis");
  else sunucu.requestAuthentication(BASIC_AUTH, "Jarvis cihazi", "Kullanici adi: jarvis, sifre: kurulumda verdiginiz yonetici sifresi");
  return false;
}

void ayarAlani(const char* a, const char* etiket, const char* vars) { gAlanlar.push_back({a, etiket, vars}); }
String ayarOku(const char* a) {
  for (auto& x : gAlanlar) if (x.a == a) return prj.getString(a, x.vars);
  return prj.getString(a, "");
}
int ayarSayi(const char* a) { return ayarOku(a).toInt(); }
void ayarYaz(const char* a, const String& d) { prj.putString(a, d); }
void durumEkle(std::function<String()> f) { gDurum.push_back(f); }
void sayfaEkle(std::function<String()> f) { gSayfa.push_back(f); }

// ---------------------------------------------------------------- sayfalar
static const char* CSS =
    "<meta name=viewport content='width=device-width,initial-scale=1'><meta charset=utf-8>"
    "<style>body{font-family:system-ui,sans-serif;background:#0d1117;color:#e6edf3;margin:0;padding:16px;max-width:560px}"
    "h1{font-size:22px;margin:0 0 4px}small,.m{color:#8b949e}.k{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:12px;margin:12px 0}"
    "button,input[type=submit],a.b{background:#238636;color:#fff;border:0;border-radius:10px;padding:10px 14px;font-size:16px;margin:4px 4px 4px 0;text-decoration:none;display:inline-block}"
    ".r{background:#da3633}.g{background:#30363d}input,select{width:100%;box-sizing:border-box;padding:10px;margin:4px 0 10px;border-radius:8px;border:1px solid #30363d;background:#0d1117;color:#e6edf3;font-size:16px}"
    "td{padding:3px 8px 3px 0;vertical-align:top}</style>";

static String sureMetni(unsigned long sn) {
  if (sn < 120) return String(sn) + " sn";
  if (sn < 7200) return String(sn / 60) + " dk";
  return String(sn / 3600) + " sa " + String((sn / 60) % 60) + " dk";
}

static void anaSayfa() {
  String h = String("<!doctype html><title>") + html(gAd.length() ? gAd : gId) + "</title>" + CSS;
  h += "<h1>" + html(gAd.length() ? gAd : gId) + "</h1><small>Jarvis cihazı · " + gId + "</small>";
  h += "<div class=k><table>";
  h += "<tr><td class=m>Proje</td><td><b>" + html(gProje) + "</b> — " + html(gAciklama) + "</td></tr>";
  h += "<tr><td class=m>Sürüm</td><td>" + String((unsigned long)JARVIS_SURUM) + " (" + JARVIS_KART + ")</td></tr>";
  h += "<tr><td class=m>Adres</td><td>" + WiFi.localIP().toString() + " · http://" + gHostname + ".local</td></tr>";
  h += "<tr><td class=m>Wi-Fi</td><td>" + html(WiFi.SSID()) + " (" + String(WiFi.RSSI()) + " dBm)</td></tr>";
  h += "<tr><td class=m>Açık</td><td>" + sureMetni((millis() - gAcilis) / 1000) + "</td></tr>";
  h += "<tr><td class=m>Güncelleme</td><td>" + html(gSonKontrol) + "</td></tr></table></div>";
  for (auto& f : gSayfa) h += "<div class=k>" + f() + "</div>";
  if (gAlanlar.size()) {
    h += "<div class=k><b>Proje ayarları</b><form method=post action=/ayarlar>";
    for (auto& x : gAlanlar) h += "<label>" + html(x.etiket) + "<input name='" + x.a + "' value='" + html(prj.getString(x.a.c_str(), x.vars)) + "'></label>";
    h += "<input type=submit value=Kaydet></form></div>";
  }
  h += "<div class=k><b>Bakım</b> <small>(yönetici şifresi sorulur; kullanıcı adı: jarvis)</small><br>"
       "<form method=post action=/guncelle style=display:inline><input type=submit value='Güncellemeyi şimdi kontrol et'></form>"
       "<form method=post action=/yeniden style=display:inline><input class=g type=submit value='Yeniden başlat'></form>"
       "<form method=post action=/yukle enctype=multipart/form-data><label>Dosyadan yükle (.bin)<input type=file name=f accept=.bin></label><input class=g type=submit value=Yükle></form>"
       "<form method=post action=/wifi-sifirla onsubmit=\"return confirm('Wi-Fi bilgisi silinsin, kurulum ağı açılsın mı?')\"><input class=r type=submit value='Wi-Fi ayarını sıfırla'></form></div>";
  h += "<div class=k><b>Son olaylar</b><br><small>";
  for (int i = 0; i < 8 && i < gOlayN; i++) h += html(gOlaylar[(gOlayN - 1 - i) % 8]) + "<br>";
  h += "</small></div>";
  sunucu.send(200, "text/html", h);
}

static void durumJson() {
  String j = "{\"id\":\"" + gId + "\",\"ad\":\"" + json(gAd) + "\",\"proje\":\"" + json(gProje) + "\",\"surum\":" + String((unsigned long)JARVIS_SURUM) +
             ",\"kart\":\"" JARVIS_KART "\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"sinyal\":" + String(WiFi.RSSI()) +
             ",\"acik_sn\":" + String((millis() - gAcilis) / 1000) + ",\"guncelleme\":\"" + json(gSonKontrol) + "\"";
  bool tam = anahtarDogru();
  if (tam) for (auto& f : gDurum) { String p = f(); if (p.length()) j += "," + p; }
  j += "}";
  sunucu.send(200, "application/json", j);
}

static void geri(const String& msg) {
  if (sunucu.hasArg("json") || sunucu.hasArg("k")) {
    sunucu.send(200, "application/json", "{\"ok\":true,\"mesaj\":\"" + json(msg) + "\"}");
    return;
  }
  sunucu.send(200, "text/html", String("<!doctype html>") + CSS + "<p>" + html(msg) + "</p><meta http-equiv=refresh content='3;url=/'><a class=b href=/>Geri</a>");
}

// ---------------------------------------------------------------- imzalı güncelleme
static bool imzaDogrula(const uint8_t hash[32], const String& imzaB64) {
  uint8_t sig[160];
  size_t n = 0;
  if (mbedtls_base64_decode(sig, sizeof(sig), &n, (const uint8_t*)imzaB64.c_str(), imzaB64.length()) != 0) return false;
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  bool ok = mbedtls_pk_parse_public_key(&pk, (const uint8_t*)JARVIS_IMZA_ACIK, sizeof(JARVIS_IMZA_ACIK)) == 0 &&
            mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 32, sig, n) == 0;
  mbedtls_pk_free(&pk);
  return ok;
}

static String hex(const uint8_t* b, int n) {
  String s;
  const char* h = "0123456789abcdef";
  for (int i = 0; i < n; i++) { s += h[b[i] >> 4]; s += h[b[i] & 15]; }
  return s;
}

static bool httpAl(const String& url, String& out) {
  NetworkClientSecure cl;
  cl.setInsecure();  // güvenlik imzadadır: indirilen dosya imzasız/bozuksa kurulmaz
  HTTPClient h;
  h.setTimeout(15000);
  if (!h.begin(cl, url)) return false;
  int c = h.GET();
  if (c != 200) { h.end(); return false; }
  out = h.getString();
  h.end();
  return true;
}

static String satirDeger(const String& metin, const String& anahtar) {  // "anahtar=değer" satırları
  int p = 0;
  while (p < (int)metin.length()) {
    int e = metin.indexOf('\n', p);
    if (e < 0) e = metin.length();
    String l = metin.substring(p, e);
    l.trim();
    int q = l.indexOf('=');
    if (q > 0 && l.substring(0, q) == anahtar) return l.substring(q + 1);
    p = e + 1;
  }
  return "";
}

static void guncellemeBak() {
  if (!baglandi()) return;
  String base = String("https://raw.githubusercontent.com/") + JARVIS_DEPO + "/yayin/";
  String atama;
  if (!httpAl(base + "cihazlar.txt", atama)) { gSonKontrol = "liste alınamadı (internet ya da depo yok)"; yaz(gSonKontrol); return; }
  String proje = satirDeger(atama, gId);
  if (!proje.length()) proje = gProje;  // bu cihaza proje atanmamış: kendi projesinde kalır
  String man;
  if (!httpAl(base + proje + "/" + JARVIS_KART + ".txt", man)) { gSonKontrol = "'" + proje + "' için " JARVIS_KART " sürümü yok"; yaz(gSonKontrol); return; }
  unsigned long surum = strtoul(satirDeger(man, "surum").c_str(), nullptr, 10);
  String sha = satirDeger(man, "sha256"), imza = satirDeger(man, "imza");
  int boyut = satirDeger(man, "boyut").toInt();
  if (proje == gProje && surum <= (unsigned long)JARVIS_SURUM) {
    gSonKontrol = "güncel (" + proje + " " + String(surum) + ") · " + sureMetni((millis() - gAcilis) / 1000) + " açıkken bakıldı";
    return;
  }
  if (sha.length() != 64 || !imza.length() || boyut <= 0) { gSonKontrol = "sürüm bilgisi bozuk"; yaz(gSonKontrol); return; }
  yaz("yeni sürüm: " + proje + " " + String(surum) + ", indiriliyor");
  NetworkClientSecure cl;
  cl.setInsecure();
  HTTPClient h;
  h.setTimeout(20000);
  if (!h.begin(cl, base + proje + "/" + JARVIS_KART + ".bin") || h.GET() != 200) { gSonKontrol = "dosya indirilemedi"; yaz(gSonKontrol); h.end(); return; }
  if (!Update.begin(boyut)) { gSonKontrol = "güncelleme alanı yok"; h.end(); return; }
  mbedtls_sha256_context sc;
  mbedtls_sha256_init(&sc);
  mbedtls_sha256_starts(&sc, 0);
  WiFiClient* st = h.getStreamPtr();
  uint8_t buf[1024];
  int kalan = boyut;
  unsigned long t0 = millis();
  led(true);
  while (kalan > 0 && millis() - t0 < 120000) {
    size_t av = st->available();
    if (!av) { delay(2); if (!h.connected()) break; continue; }
    int r = st->readBytes(buf, min((size_t)sizeof(buf), min(av, (size_t)kalan)));
    if (r <= 0) break;
    mbedtls_sha256_update(&sc, buf, r);
    if (Update.write(buf, r) != (size_t)r) break;
    kalan -= r;
  }
  h.end();
  uint8_t hash[32];
  mbedtls_sha256_finish(&sc, hash);
  mbedtls_sha256_free(&sc);
  if (kalan != 0) { Update.abort(); gSonKontrol = "indirme yarıda kaldı"; yaz(gSonKontrol); return; }
  if (hex(hash, 32) != sha) { Update.abort(); gSonKontrol = "dosya bozuk (SHA tutmadı), kurulmadı"; yaz(gSonKontrol); return; }
  if (!imzaDogrula(hash, imza)) { Update.abort(); gSonKontrol = "İMZA GEÇERSİZ, kurulmadı"; yaz(gSonKontrol); return; }
  if (!Update.end(true)) { gSonKontrol = "kurulum hatası " + String(Update.getError()); yaz(gSonKontrol); return; }
  yaz("kuruldu, yeniden başlıyor");
  delay(300);
  ESP.restart();
}

// dosyadan yükleme (yönetici şifresiyle; imza aranmaz: kendi derlediğiniz dosya için)
static bool gYukYetki = false;
static void yukleParca() {
  HTTPUpload& u = sunucu.upload();
  if (u.status == UPLOAD_FILE_START) {
    gYukYetki = anahtarDogru();
    if (gYukYetki) { yaz("dosyadan yükleme: " + u.filename); Update.begin(UPDATE_SIZE_UNKNOWN); }
  } else if (u.status == UPLOAD_FILE_WRITE && gYukYetki) {
    Update.write(u.buf, u.currentSize);
  } else if (u.status == UPLOAD_FILE_END && gYukYetki) {
    Update.end(true);
  }
}

// ---------------------------------------------------------------- Wi-Fi ve kurulum portalı
static void portalSayfa() {
  String h = String("<!doctype html><title>Jarvis kurulum</title>") + CSS + "<h1>Jarvis cihaz kurulumu</h1><small>" + gId + " · " + html(gProje) + "</small>";
  h += "<form method=post action=/kaydet class=k><label>Wi-Fi ağı<select name=s>";
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) { WiFi.scanNetworks(true); n = 0; }
  for (int i = 0; i < n; i++) h += "<option>" + html(WiFi.SSID(i)) + "</option>";
  h += "</select></label><label>Listede yoksa adını yazın<input name=s2 placeholder='Wi-Fi adı'></label>"
       "<label>Wi-Fi şifresi<input name=p type=password></label>"
       "<label>Cihaz adı (salon, mutfak, robot…)<input name=ad value='" + html(gAd) + "'></label>"
       "<label>Yönetici şifresi (en az 8 karakter; Jarvis'teki anahtar bu olacak)<input name=k type=password></label>"
       "<input type=submit value='Kaydet ve bağlan'></form><p class=m>Liste boşsa birkaç saniye sonra sayfayı yenileyin.</p>";
  sunucu.send(200, "text/html", h);
  if (n > 0) { WiFi.scanDelete(); WiFi.scanNetworks(true); }
}

static void portalKaydet() {
  String s = sunucu.arg("s2").length() ? sunucu.arg("s2") : sunucu.arg("s");
  String p = sunucu.arg("p"), ad = sunucu.arg("ad"), k = sunucu.arg("k");
  if (!s.length()) return sunucu.send(400, "text/html", String(CSS) + "<p>Wi-Fi adı boş.</p><a class=b href=/>Geri</a>");
  if (gAnahtar.length() == 0 && k.length() < 8) return sunucu.send(400, "text/html", String(CSS) + "<p>Yönetici şifresi en az 8 karakter olmalı.</p><a class=b href=/>Geri</a>");
  cek.putString("ssid", s);
  cek.putString("pass", p);
  if (ad.length()) cek.putString("ad", ad);
  if (k.length() >= 8) cek.putString("anahtar", k);
  sunucu.send(200, "text/html", String(CSS) + "<p>Kaydedildi. Cihaz yeniden başlayıp <b>" + html(s) + "</b> ağına bağlanacak. Sonra http://jarvis-" +
                                     slug(ad.length() ? ad : gId) + ".local adresinden açabilirsiniz.</p>");
  delay(1500);
  ESP.restart();
}

static void portalBaslat() {
  gPortal = true;
  gPortalBasladi = millis();
  WiFi.mode(WIFI_AP_STA);
  String ap = "Jarvis-" + gId.substring(1);
  WiFi.softAP(ap.c_str(), "jarvis1234");
  dns.start(53, "*", WiFi.softAPIP());
  WiFi.scanNetworks(true);
  sunucu.on("/", HTTP_GET, portalSayfa);
  sunucu.on("/kaydet", HTTP_POST, portalKaydet);
  sunucu.onNotFound([]() { sunucu.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/"); sunucu.send(302); });
  sunucu.begin();
  yaz("kurulum ağı açıldı: " + ap + " (şifre jarvis1234) → http://192.168.4.1");
}

static bool wifiBaglan(uint32_t ms) {
  String s = cek.getString("ssid", ""), p = cek.getString("pass", "");
  if (!s.length()) return false;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(gHostname.c_str());
  WiFi.begin(s.c_str(), p.c_str());
#if CONFIG_IDF_TARGET_ESP32C3
  WiFi.setTxPower(WIFI_POWER_15dBm);  // C3 Super Mini: tam güçte anten yüzünden bağlanamayabiliyor
#endif
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < ms) { led((millis() / 250) % 2); delay(50); }
  led(false);
  return WiFi.status() == WL_CONNECTED;
}

static void normalSunucu() {
  sunucu.on("/", HTTP_GET, anaSayfa);
  sunucu.on("/durum", HTTP_GET, durumJson);
  sunucu.on("/cihaz", HTTP_GET, durumJson);
  sunucu.on("/guncelle", HTTP_POST, []() { if (!yetkiIste()) return; gSimdiBak = true; geri("Güncelleme kontrol ediliyor; yeni sürüm varsa birazdan kendini yeniden başlatır."); });
  sunucu.on("/yeniden", HTTP_POST, []() { if (!yetkiIste()) return; geri("Yeniden başlıyor…"); delay(500); ESP.restart(); });
  sunucu.on("/wifi-sifirla", HTTP_POST, []() { if (!yetkiIste()) return; cek.remove("ssid"); cek.remove("pass"); geri("Wi-Fi silindi; kurulum ağı açılıyor."); delay(500); ESP.restart(); });
  sunucu.on("/ayarlar", HTTP_POST, []() {
    if (!yetkiIste()) return;
    for (auto& x : gAlanlar) if (sunucu.hasArg(x.a)) prj.putString(x.a.c_str(), sunucu.arg(x.a));
    geri("Ayarlar kaydedildi. Bazıları yeniden başlatınca geçerli olur.");
  });
  sunucu.on("/yukle", HTTP_POST, []() {
    if (!gYukYetki) { yetkiIste(); return; }
    bool ok = !Update.hasError();
    geri(ok ? "Yüklendi, yeniden başlıyor." : "Yükleme olmadı: hata " + String(Update.getError()));
    if (ok) { delay(500); ESP.restart(); }
  }, yukleParca);
  sunucu.onNotFound([]() { sunucu.send(404, "text/plain", "yok"); });
  sunucu.begin();
}

void baslat(const char* proje, const char* aciklama) {
  Serial.begin(115200);
  gAcilis = millis();
  pinMode(LED, OUTPUT);
  led(false);
  gProje = proje;
  gAciklama = aciklama;
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  char b[8];
  snprintf(b, sizeof(b), "j%02X%02X%02X", mac[3], mac[4], mac[5]);
  gId = b;
  cek.begin("jarvis", false);
  prj.begin("proje", false);
  gAd = cek.getString("ad", "");
  gAnahtar = cek.getString("anahtar", "");
  gHostname = "jarvis-" + slug(gAd.length() ? gAd : gId);
  yaz(String("açıldı: ") + proje + " sürüm " + String((unsigned long)JARVIS_SURUM) + " kart " JARVIS_KART " kimlik " + gId);
  if (!wifiBaglan(20000)) { portalBaslat(); return; }
  gSonWifi = millis();
  yaz("Wi-Fi bağlı: " + WiFi.localIP().toString());
  if (MDNS.begin(gHostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("jarvis", "tcp", 80);
    MDNS.addServiceTxt("jarvis", "tcp", "id", gId);
    MDNS.addServiceTxt("jarvis", "tcp", "proje", gProje);
  }
  normalSunucu();
  gSonGuncelleme = millis() - gAralik + 30000;  // açılıştan 30 sn sonra ilk kontrol
}

void dongu() {
  if (gPortal) {
    if (!gOnaylandi && millis() - gAcilis > 180000) {  // yeni sürüm Wi-Fi'ye bağlanamayıp kurulum ağına düştüyse eskisine dön
      gOnaylandi = true;
      esp_ota_img_states_t st;
      if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) esp_ota_mark_app_invalid_rollback_and_reboot();
    }
    dns.processNextRequest();
    sunucu.handleClient();
    led((millis() / 1000) % 2);
    if (millis() - gPortalBasladi > 10UL * 60UL * 1000UL && cek.getString("ssid", "").length()) ESP.restart();  // 10 dk sonra kayıtlı ağı tekrar dene
    return;
  }
  sunucu.handleClient();
  unsigned long now = millis();
  if (baglandi()) {
    gSonWifi = now;
    if (!gOnaylandi && now - gAcilis > 20000) {  // yeni sürüm sağlam: onayla (geri dönüş iptal)
      esp_ota_img_states_t st;
      if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        yaz("yeni sürüm sağlam, onaylandı");
      }
      gOnaylandi = true;
    }
    if (gSimdiBak || now - gSonGuncelleme > gAralik) {
      gSimdiBak = false;
      gSonGuncelleme = now;
      guncellemeBak();
    }
  } else {
    if (!gOnaylandi && now - gAcilis > 180000) {  // yeni sürüm 3 dakikadır ağa çıkamadı: eskisine dön
      esp_ota_img_states_t st;
      if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        yaz("yeni sürüm ağa çıkamadı, eski sürüme dönülüyor");
        esp_ota_mark_app_invalid_rollback_and_reboot();
      }
    }
    if (now - gSonWifi > 15000) { gSonWifi = now; WiFi.reconnect(); }
  }
}

}  // namespace jarvis
