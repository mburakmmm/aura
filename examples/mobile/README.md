# Mobil kabuk

Counter, Notes, Chat, Settings, Todo, Calculator, Files ve Dashboard aynı Nox kaynağından kurulur. Widget kodu UIKit, JNI veya `extern def` içermez. Dokunuş, güvenli alan ve klavye payı host olayına çevrilir. Çizim `native/skia` raster tamponunu yüzeye kopyalar. GPU bu kabuğun kapısı değildir.

Paket kimliği, sürüm, minimum iOS, yön ve asset hash’i `examples/mobile/app.json` içindedir. Örnek adı bundle id’ye eklenir: Notes için `com.noxlang.aura.Notes`.

## Cihaz

5 Kasım 2026’da iPhone 17 Pro’da sekiz örnek development imzasıyla açıldı.

Görülenler: Notes döndürme ve seçim, Counter artışı, Chat gönderimi, Settings yatay kaydırma, Todo ayrıntı ve resim, Calculator `1 + 2 =`, Files içinde Back ve Close, Dashboard Open animasyonu.

VoiceOver yalnız Notes’ta kısmi görüldü. Düzenlenen metin süreç ölünce durmaz. Android telefon, iPad, TestFlight ve GPU bu turda yoktur.

## Xcode paketi

```sh
./scripts/build_ios_xcode.sh examples/notes/main.nox Notes device
```

Betik yerel development profilini bulur, `build/ios/xcode/Notes/` altında proje, `.xcarchive`, dSYM ve development IPA üretir. Takım ve profil depoya yazılmaz. Kimlik yoksa cihaz eklentisi imzasız kalmaz; derleme durur.

`store` aynı betiğin üçüncü argümanıdır. `com.noxlang.aura.Notes` için App Store profili yoktur. TestFlight yüklemesi `scripts/upload_testflight.sh` ile yapılır ve API anahtarı repoda durmaz.

Doğrudan clang paketi de aynı manifesti kullanır:

```sh
./scripts/build_ios_app.sh examples/notes/main.nox Notes device
```

Bu yol kurmaz ve başlatmaz. Cihaz diliminde imza profilini kendisi çözer.
