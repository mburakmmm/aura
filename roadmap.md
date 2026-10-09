# Aura yol haritası

Bu dosya 0.7’den mobil temelin kapanışına kadar bağlayıcı teslimat sözleşmesidir. `AGENTS.md` invariant’ları geçerlidir. Çelişki olursa `AGENTS.md` kazanır ve bu dosya ona göre güncellenir.

Bir sürüm bitmeden sonrakine geçilmez. Her sürümün self-check’i bir öncekini kırmaz. Nox derleyicisi değişmez. Widget katmanında `extern def` ve platform dalı yoktur. Yerleşim, sahne, kabuk, gezinme, durum, signal veya çizim backend’i değişecekse önce ADR yazılır.

0.1, 0.2, 0.3, 0.4, 0.5, 0.6 ve 0.7 kabul edilmiştir. 0.7 sanal listede satır yüksekliğini metne bağlar. Kapı `scripts/certify.sh` ve GitHub Actions’tır.

## 0.4 — Kabuk sertifikası

Tamamlandı. Üç masaüstü aynı programı koşar. Kanıt uygulaması `examples/proof`’tur. Sayaç, dikdörtgen, todo ve hesap makinesi self-check’leri de aynı kaynaktır.

Pano, imleç, dosya bırakma, IME ve erişilebilirlik listesi kabukta kalır. Karakter sanal tuştan üretilmez. Native kod Nox’a geri çağrı yapmaz.

## 0.5 — Notes

Tamamlandı. `examples/notes` tek belge açar. İlk paragraf dar genişlikte sarılır, ikinci paragraf satır sonundan sonra başlar.

Self-check cümleyi sürükleyerek seçer. Seçim dikdörtgenleri iki ayrı satırdadır. Kopya panoya yazılır ve ikinci paragrafa yapıştırılır. IME commit’i imlecin durduğu yere girer. Karakter sanal tuştan üretilmez.

Zengin metin, çift yön, emoji, not listesi ve kayıt bu sürüme girmedi.

## 0.6 — Settings

Tamamlandı. `examples/settings` temayı değiştirir. Temaya bakan satır kirlenir. Açık renk verilmiş başlık ve temaya bakmayan düğme yeniden kurulmaz.

Self-check gizli pencerede başlığı `Settings`, boyutu `800` ve `600`, ölçeği sıfırdan büyük okur. İşaretçi metin alanının üstündeyken `cursor_kind` `1`, dışında `0` olur.

Genel adlar `theme_of`, `Theme`, `ThemeData`, `ColorScheme`, `Typography`, `window_title`, `window_width`, `window_height`, `window_scale` ve `cursor_kind`’dır. Kırılması ana sürüm ister. Kalıcı ayar, çok bölmeli form ve sohbet bu sürüme girmedi.

## 0.7 — Chat

Tamamlandı. `examples/chat` yüksekliği metinden gelen bir liste açar. Ekran dışındaki satırlar kurulmaz. `keep_alive` en fazla 32 ek satır tutar. 10.000 satır belleği patlatmaz.

Yeni mesaj, kullanıcı sondayken altta kalır. IME kompozisyonu imleçte görünür ve commit mevcut ekleme yoluna girer. Karakter sanal tuştan üretilmez. Sliver yoktur. Yerleşim protokolü değişmedi.

## 0.8 — Dosya gezgini

Tamamlandı. `examples/files` bir dizin açar. Dizin okuma UI iş parçacığının dışında biter. Sonuç state’e yazılır ve `invalidate` edilir. `build` sırasında bloklayan bekleyiş yoktur.

Liste sanaldır. Önizleme görüntüdür. Bırakılan dosya mevcut bırakma kuyruğuna düşer. İkinci pencere önizlemeyi açar. `.md` dosyası satır satır düz metin olarak açılır. Zengin metin 1.0 kapısıdır.

## 0.9 — Dashboard

`AnimatedOpacity`, `AnimatedSize` ve `AnimatedPosition`, mevcut `AnimationController` ve kare damgası üzerine oturur. Animasyon işletim sistemi saatini okumaz.

Sabit paneller, veri değişince yeniden boyanmaz. Altın görüntü paketlenmiş Noto Sans ile alınır. Profil sayaçları 60 Hz ve 120 Hz bütçesini yazar. Ölçüm en pahalı fazı gösterirse tek iyileştirme yapılır.

## 1.0 — Dondurma

Genel API bu sürümde donar. Kırılması ana sürüm ister.

Kapılar:

- README ve çerçeve dokümanı günceldir.
- Sayaç, todo, hesap makinesi, Notes, Settings, Chat, dosya gezgini ve Dashboard self-check’leri geçer.
- Altın görüntü ve sekiz benchmark geçer.
- macOS, Windows ve Linux aynı kapıdan geçer.
- İkinci bir Nox paketi yalnız `register_view` ile bağlanır. Çekirdek dizin taramaz.
- Metin, düz paragrafın yanında satır içi stil taşır: kalın, eğik ve başlık. Dosya gezginindeki markdown düz satır duvarı olarak kalmaz. Çift yön ve emoji bu sürümde yoktur.

1.0 bitmeden mobil kabuk yazılmaz.

## 1.0 dışında bırakılanlar

Bu yol haritası şunları planlamaz: web, hot reload, GUI DevTools, sliver, çift yön, emoji ve derin bağlantılı yönlendirici. Sıraya alınmaları ayrı bir sözleşme ister. Zengin metin 1.0 kapısındadır.

## Mobil

Mobil, 1.0 masaüstü sözleşmesinin üstüne eklenir. Widget, Element ve RenderObject ayrımı değişmez. Yeni işletim sistemi yeni bir kabuk kütüphanesidir. Aynı C sembol kümesi ve aynı Nox köprüsü kullanılır. Olaylar kuyruğa yazılır, `poll_events` okur.

İlk telefon iOS’tur. Geliştirme makinesi ve mevcut Cocoa kabuğu oradadır. Android aynı sembolleri ikinci kabuk olarak uygular.

Çizim komutları değişmez. 1.1–1.4 pixmap’i Skia raster üretir ve kabuk bunu yüzeye basar. GPU yüzeyi 1.5’tir ve ADR’siz seçilmez. Masaüstü blit yolu durur.

### 1.1 — iOS kabuğu

Tek tam ekran yüzey. Dokunuş mevcut işaretçi olaylarına normalize edilir. Dikey yön ve güvenli alan, pencere boyutu ve iç boşluk olarak çerçeveye iner. Widget katmanı UIKit tipi görmez.

Kanıt, simülatörde sayaç self-check’idir. Simülatör açık bırakılmaz.

### 1.2 — Klavye ve IME

Metin alanı karakteri fiziksel tuştan üretmez. Yazılım klavyesi ve IME commit’i olay kuyruğuna yazılır. Klavye yüksekliği yerleşim kısıtını küçültür. Notes self-check’i bir paragraf yazar.

### 1.3 — Dokunuş ve yaşam döngüsü

Gesture arena dokunuşla çalışır. Kaydırma ile düğme aynı parmak dizisinde yarışır. Ön plan, arka plan ve bellek uyarısı UI iş parçacığında, kuyruk üzerinden uygulanır. Yön değişimi kısıtları yeniden hesaplar. Chat listesi döndürünce sanal kalır.

### 1.4 — Android kabuğu

Aynı Nox programı Android yüzeyine basılır. Dokunuş, IME, güvenli alan ve yaşam döngüsü 1.1–1.3 sözleşmesidir. Kanıt, emülatörde sayaç ve Notes self-check’idir.

### 1.5 — GPU yüzeyi, erişilebilirlik, paket

GPU backend’i ADR ile seçilir. Seçim, masaüstü pixmap yolunu bozmadan mobil sunumu hızlandırır. Anlam listesi VoiceOver ve TalkBack’e aynı alanlarla gider: rol, etiket, değer, seçili durum, sınır. Basma eylemi kuyruğa yazılır.

Paket, Nox AOT ikilisini bir iOS uygulama paketi ve bir Android paketinde başlatır. Mobil temel, bu üç kapı geçmeden kapanmış sayılmaz.
