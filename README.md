# Jarvis ESP

Evdeki bütün ESP32 cihazlarının kodu burada. Bilgisayar olmadan güncellenir: buraya yapılan her değişikliği GitHub kendisi derler, imzalar ve `yayin` dalına koyar. Cihazlar 15 dakikada bir buraya bakar; kendilerine atanan projenin yeni sürümünü bulunca kendileri indirip kurar.

## Nasıl çalışır
- **`projeler/`**: her klasör bir proje (`temel`, `priz`, …). Eski projeler silinmez; istediğiniz cihaza istediğiniz zaman atanabilir.
- **`cihazlar.json`**: hangi cihazda hangi proje çalışacak. Kimlik cihazın kendi sayfasında yazar (ör. `jA1B2C3`).
  ```json
  "cihazlar": {
    "jA1B2C3": {"ad": "salon", "proje": "priz", "kart": "esp32"},
    "j0F11AA": {"ad": "mutfak", "proje": "temel", "kart": "c3"}
  }
  ```
  Bir cihaza yeni proje vermek = sadece o satırdaki proje adını değiştirmek. Diğer cihazlar etkilenmez.
- **`lib/JarvisCekirdek/`**: her cihazda ortak olan kısım: Wi-Fi kurulum ağı, `jarvis-<ad>.local` adresi, cihaz sayfası, imzalı güncelleme, bozuk sürümde eskisine dönme.
- **`kartlar.json`**: kart türleri (`c3` = ESP32-C3 Super Mini, `esp32` = DevKit V1 / NodeMCU-32S).

## Güvenlik
- Kodda **hiçbir şifre yok**. Wi-Fi şifresi, cihaz adı ve yönetici şifresi her cihazın kurulum sayfasında girilir ve sadece o cihazda saklanır.
- Güncellemeler **ECDSA imzalı**. Özel anahtar sadece bu deponun gizli değişkeninde (`Settings → Secrets → Actions → OTA_IMZA`). İmzasız ya da bozuk dosyayı cihaz kurmaz.
- Yeni sürüm açılıp 3 dakika içinde Wi-Fi'ye bağlanamazsa cihaz kendiliğinden eski sürüme döner.

## Yeni ESP eklemek
1. Bir kez USB ile `temel` yüklenir (bilgisayardaki `jarvis\esp\yukle.bat`).
2. Telefondan `Jarvis-XXXXXX` ağına bağlanılır (şifre `jarvis1234`), ev Wi-Fi'si, cihaz adı ve yönetici şifresi girilir.
3. Cihaz sayfasındaki kimlik `cihazlar.json`'a yazılıp proje atanır. Birkaç dakika içinde cihaz o projeye geçer.

## Yeni proje
`projeler/<ad>/<ad>.ino` (boş bırakılır) + `projeler/<ad>/<ad>_ana.cpp`:
```cpp
#include <JarvisCekirdek.h>
void setup() { jarvis::baslat("<ad>", "Açıklama"); /* ... */ }
void loop()  { jarvis::dongu(); /* ... */ }
```
