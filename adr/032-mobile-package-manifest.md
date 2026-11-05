# ADR-032 Mobil paket manifesti ve tekrar üretilebilir build kanıtı

ADR-MOB-AURA-012.

## Context

Mobil preview build script'leri Nox ağacını kişisel bir mutlak yolla varsayıyordu.
Sürüm pinleri script, CI ve Android manifest arasında ayrı ayrı duruyordu. Üretilen
APK, AAB, IPA, keystore ve imza dosyalarının kaynak ağacından ayrımı ignore
kuralına yazılmamıştı. İmza parolası, provisioning profile ve private key için
kaynak taraması yoktu.

Mağaza imzası, Xcode/Gradle proje üretimi ve cihaz kurulumu bu kararda yok.
Onlar için imza kimliği ve cihaz laboratuvarı gerekir.

## Decision

`toolchain/manifest.json` Aura, Nox aralığı, CI Nox sürümü, embed ABI, Skia
milestone, Android NDK, min/target SDK, JDK alt sınırı ve iOS minimumunu pinler.
Xcode, Gradle, Zig ve Clang sürümleri pinlenmez; `scripts/aura doctor` onları
tespit eder veya eksik diye yazar.

`examples/mobile/app.json` bundle id, görünen ad, sürüm, build numarası, minimum
OS, yön, izin, purpose string, entitlement, simge, deep link, ağ ve asset
listesini taşır. Asset yolları sıralı ve tektir. SHA-256 dosyanın kendisiyle
tutmazsa paket denetimi durur. Cleartext ağ ve Android backup kapalıdır. Boş izin
listesi izin istenmediği anlamına gelir.

Nox kaynağı `NOX_ROOT` ile verilir. Ortam boşsa Aura deposunun yanındaki
`nox-lang` checkout'u, `include/nox_embed.h` varsa kullanılır. Script içine kişisel
mutlak yol yazılmaz.

Debug APK yalnız herkese açık Android debug parolasını kullanır. Başka bir
parola, private key, keystore, provisioning profile veya kişisel mutlak yol
kaynak taramasını düşürür. `|| true` yalnız `cat` ve `ls` tanı satırlarında
kalabilir.

`scripts/mobile_package.py proof` build kanıtı üretir: Aura commit, Nox commit
veya `unavailable`, CI Nox sürümü, embed ABI, toolchain ve component hash, host,
backend `skia-raster`, `run: false`. Kanıt kişisel yol ve parola taşımaz.
`sbom` bileşen listesinin SPDX JSON'udur. `hash` bir dosyanın SHA-256 değeridir.

Host, `nox_embed_abi_version()` pinlenen `NOX_EMBED_ABI` ile aynı değilse app
create çağrılmaz ve log `Nox embed ABI mismatch` olur.

## Alternatives

Kişisel checkout yolunu varsayılan bırakmak başka bir makinede yanlış ağaca
bağlanır. Sürümleri yalnız script içine yazmak CI pinini sessizce ayırır. Nox
widget katmanına keystore API'si koymak imza sırrını UI ağacına taşır. GPU veya
mağaza imzasını bu manifestin içine gizlemek ADR-009 ve imza ADR'si olmadan
backend seçmek olur.

## Consequences

Masaüstü PR sertifikası `scripts/test.sh` üzerinden paket denetimini de çalıştırır.
Doctor eksik Nox, NDK veya eski JDK için uygulanabilir mesajla durur. Pinlenmemiş
araçlar `optional-missing` satırıdır ve çıkış kodunu değiştirmez. SBOM, Skia'nın
`libjpeg.a` arşivi için kesin upstream lisansı iddia etmez.

`aura create`, `aura run` ve imzalı IPA/AAB bu kararın parçası değildir. Deep
link alanı manifestte durur; host intent filter'ına bu dilimde yazılmaz. Simge
listesi boşsa simge maddesi bitmiş sayılmaz.

## Migration ve rollback

Önce denetim ve doctor eklenir, sonra build script'lerindeki kişisel varsayılan
kalkar. Sorunda script varsayılanı geri gelebilir; kaynak taramasının kabul
ettiği private key veya kişisel yol geri gelmez. Embed ABI ayrımı create
çağrısından önce durur. Eski davranışa dönmek app'i yanlış ABI ile başlatmayı
geri getirir ve kabul edilmez.

## Risk

Kardeş `nox-lang` checkout'u yanlış dalda olabilir. `NOX_ROOT` bunu ezer. Debug
parolasının herkese açık olması release parolası için bir izin değildir. Asset
hash'i dosya değişince denetimi bilinçli olarak düşürür.
