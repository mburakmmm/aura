# Mobil kabuk

Counter, Notes, Chat, Settings, Todo, Calculator, Files ve Dashboard aynı Nox kaynağından kurulur. Widget kodu UIKit, JNI veya `extern def` içermez. Dokunuş, güvenli alan ve klavye payı host olayına çevrilir. Çizim `native/skia` raster tamponunu yüzeye kopyalar. GPU bu kabuğun kapısı değildir.

Paket kimliği `examples/mobile/app.json` içindedir. Sürüm, minimum iOS, yön ve asset hash’i oradan okunur.

## Cihaz

5 Kasım 2026’da iPhone 17 Pro’da sekiz örnek development imzasıyla açıldı. Kurulum elle entitlements ister. Xcode projesi, `.xcarchive` ve imzalı IPA yoktur.

Görülenler: Notes döndürme ve seçim, Counter artışı, Chat gönderimi, Settings yatay kaydırma, Todo ayrıntı ve resim, Calculator `1 + 2 =`, Files içinde Back ve Close, Dashboard Open animasyonu.

VoiceOver yalnız Notes’ta kısmi görüldü. Düzenlenen metin süreç ölünce durmaz. Android telefon, iPad, TestFlight ve GPU bu turda yoktur.

## Derleme

```sh
AURA_IOS_SIGN_IDENTITY=<identity> ./scripts/build_ios_app.sh examples/notes/main.nox Notes device
```

Betik `build/ios/device/Notes/Notes.app` üretir. Kurmaz ve başlatmaz. Kimlik boşsa eklenti dylib’i imzasız kalır. Uygulama entitlements dosyasını betik yazmaz.
