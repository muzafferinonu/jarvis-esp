/*
  JarvisCekirdek — bütün Jarvis ESP32 cihazlarının ortak çekirdeği.
  - Wi-Fi kurulum portalı: Wi-Fi bilgisi yoksa ya da bağlanamazsa "Jarvis-XXXXXX" adlı ağ açılır (şifre: jarvis1234).
    Telefondan bağlanınca açılan sayfada Wi-Fi adı/şifresi, cihaz adı ve yönetici şifresi girilir. KODDA ŞİFRE YOKTUR.
  - mDNS: http://jarvis-<ad>.local ve http://jarvis-<kimlik>.local
  - Cihazın kendi sayfası: durum, proje, sürüm, güncelleme, dosyadan yükleme, proje ayarları, yeniden başlatma, Wi-Fi sıfırlama.
  - İmzalı kablosuz güncelleme: GitHub'daki "yayin" dalından bu cihaza atanan projenin son sürümünü indirir,
    SHA-256 ve ECDSA imzasını doğrular, ancak o zaman kurar. Yeni sürüm açılıp Wi-Fi'ye bağlanamazsa eski sürüme kendiliğinden döner.
  Proje dosyası şöyle başlar:
      #include <JarvisCekirdek.h>
      void setup() { jarvis::baslat("priz", "Işık prizi (servo)"); ... }
      void loop()  { jarvis::dongu(); ... }
*/
#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>
#include <functional>

#ifndef JARVIS_SURUM
#define JARVIS_SURUM 0
#endif
#ifndef JARVIS_DEPO
#define JARVIS_DEPO "muzafferinonu/jarvis-esp"
#endif
#ifndef JARVIS_KART
#if CONFIG_IDF_TARGET_ESP32C3
#define JARVIS_KART "c3"
#else
#define JARVIS_KART "esp32"
#endif
#endif

namespace jarvis {
extern WebServer sunucu;

void baslat(const char* proje, const char* aciklama);  // setup() içinde, en başta
void dongu();                                           // loop() içinde, her turda

bool anahtarDogru();      // istekte ?k=YÖNETİCİ_ŞİFRESİ ya da tarayıcı girişi (kullanıcı: jarvis) doğru mu
bool yetkiIste();         // doğru değilse 401/403 döndürür ve false verir
String cihazId();         // "j" + MAC'in son 6 hanesi (ör. jA1B2C3)
String cihazAdi();        // kurulumda verilen ad (ör. salon)
bool baglandi();          // Wi-Fi bağlı mı

// Proje ayarları: cihaz sayfasında kutu olarak görünür, şifreyle değiştirilir, elektrik gitse de unutulmaz.
void ayarAlani(const char* anahtar, const char* etiket, const char* varsayilan);
String ayarOku(const char* anahtar);
int ayarSayi(const char* anahtar);
void ayarYaz(const char* anahtar, const String& deger);

void durumEkle(std::function<String()> jsonParca);   // /durum JSON'una eklenecek alanlar: "\"isik\":\"acik\"" gibi
void sayfaEkle(std::function<String()> html);        // ana sayfaya eklenecek HTML (düğmeler vb.)
void yaz(const String& s);                            // seri monitöre + sayfadaki son olaylar listesine
String json(const String& s);                         // JSON için tırnak/ters bölü kaçışı
}  // namespace jarvis
