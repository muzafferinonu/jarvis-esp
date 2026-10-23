// TEMEL: sadece Jarvis çekirdeği. Her yeni ESP'ye ilk USB yüklemesinde bu girer; sonra GitHub'dan hangi proje atanırsa o kablosuz gelir.
#include <JarvisCekirdek.h>

void setup() {
  jarvis::baslat("temel", "Boş cihaz: proje atanmayı bekliyor");
  jarvis::sayfaEkle([]() {
    return String("<b>Bu cihaz boş.</b><br><small>GitHub'daki cihazlar.json dosyasında bu kimliğe (") + jarvis::cihazId() +
           ") bir proje yazılınca birkaç dakika içinde kendiliğinden o projeye geçer.</small>";
  });
}

void loop() { jarvis::dongu(); }
