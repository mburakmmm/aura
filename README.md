# Aura

Aura, [Nox](https://github.com/mburakmmm/nox-lang) için deklaratif bir masaüstü UI framework’üdür. Uygulama kodu widget ağacı kurar. Çerçeve bunu uzun ömürlü bir element ağacına, yerleşim ve boyamadan sorumlu bir render ağacına ve kabuğun pencereye bastığı bir sahneye çevirir.

```python
from aura.app import run_app
from aura.widgets import StatelessWidget, BuildContext
from aura.layout import Column
from aura.text import Text
from aura.controls import Button

class CounterPage(StatelessWidget):
    def build(self, context: BuildContext) -> Widget:
        return Column([
            Text("Aura"),
            Button("Continue", handle_continue),
        ])
```

Widget konfigürasyondur. Element kimliği, yaşam döngüsünü ve kirli durumu tutar. RenderObject yerleşir, boyar ve vuruş testi yapar. Bu üçü birbirinin yerine geçmez.

**Şu anki sürüm 1.3.0’dır.** Genel API donmuştur. `Text` durur. `TextRun` ve `RichText` satır içi kalın, eğik ve başlık taşır. Özel arayüz `Stack`, `Align`, `Inherited`, `MouseRegion`, `GestureDetector`, `DecoratedBox` ve `CustomPaint` ile kurulur. Kapı [roadmap.md](roadmap.md) bölüm 1.0’dır: `scripts/certify.sh` testleri ve on gizli self-check’i koşturur. GitHub Actions bunu macOS, Win32, X11 ve Wayland üzerinde çalıştırır.

## Yol haritası

Bağlayıcı teslimat sözleşmesi [roadmap.md](roadmap.md) dosyasındadır. `AGENTS.md` invariant’ları her sürümde geçerlidir. Bir sürüm bitmeden sonrakine geçilmez.

| Sürüm | Teslim |
| --- | --- |
| 0.4 | Tamam: üç masaüstü kabuğunda aynı self-check |
| 0.5 | Tamam: Notes, sarılmış satırda seçim, pano ve IME |
| 0.6 | Tamam: Settings, tema kirletmesi ve pencere okuması |
| 0.7 | Tamam: Chat, değişken satır yüksekliği ve IME |
| 0.8 | Tamam: Dosya gezgini, UI iş parçacığı dışında okuma |
| 0.9 | Tamam: Dashboard, örtük animasyon, altın piksel, kare bütçesi |
| 1.0 | Tamam: API dondurma, satır içi zengin metin, `packages/badge` |
| 1.1–1.5 | Sürüyor: 1.3.0 atanmayan ikona Nox markasını koyar ve örnek dizininden `aura run` o örneği açar. TestFlight, Play, GPU, TalkBack ve 16 KiB çalışma zamanı açık |

Web, hot reload ve sliver bu sözleşmenin dışındadır. Satır içi kalın, eğik ve başlık 1.0’dadır. Çift yön ve emoji yoktur.

Mobil kaynak `main` üzerindedir. Görev ve cihaz kanıtı [MOBILE_TODO.md](MOBILE_TODO.md) dosyasındadır. iPhone 17 Pro’da ve bir Android 12 telefonunda sekiz örnek açıldı. Notes için Xcode archive ve development IPA üretildi. TestFlight, Play, GPU ve TalkBack açıktır.

## Gereksinimler

- Nox 2.0 (`noxc`)
- Doğrulanmış kabuklar: macOS, Windows ve Linux. Linux paketi Wayland, Xkbcommon, X11, D-Bus ve wayland-protocols ister. Windows kabuğu MSVC ile Skia’ya bağlanır; Nox programını MinGW `cc` bağlar.
- Ağ: ilk derleme sabit Skia arşivini `third_party/` altına indirir

Çalışma dizini depo köküdür. Metin, `native/fonts/` altındaki OFL Noto Sans ile ölçülür.

## Derleme ve çalıştırma

```sh
./scripts/build_host.sh
noxc run examples/counter/main.nox
```

Pencere 800×600 açılır. Continue düğmesi sayacı artırır. Üstteki Aura yazısı aynı kaldığı için o metin boyamayı kirletmez.

Gizli pencere self-check’leri:

```sh
noxc run examples/counter/main.nox -- --self-check
noxc run examples/rectangle/main.nox -- --self-check
noxc run examples/todo/main.nox -- --self-check
noxc run examples/calculator/main.nox -- --self-check
noxc run examples/proof/main.nox -- --self-check
noxc run examples/notes/main.nox -- --self-check
noxc run examples/settings/main.nox -- --self-check
noxc run examples/chat/main.nox -- --self-check
noxc run examples/files/main.nox -- --self-check
noxc run examples/dashboard/main.nox -- --self-check
```

`examples/proof` 0.3’ü tek ekranda gösterir: paketlenmiş font, PNG ve JPEG, opaklık, debug sınırı, kayıtlı `badge` görünümü ve yerli `PlatformView`.

## Test ve ölçüm

```sh
./scripts/test.sh
./scripts/certify.sh
./scripts/bench.sh
```

`test.sh` kabuğu derler ve `noxc test` çalıştırır. `certify.sh` buna on gizli self-check ekler ve pencereyi açık bırakmaz. Benchmark sekiz senaryonun faz sürelerini ve kurulum sayaçlarını yazar.

## Düzen

Bağımlılık yukarıdan aşağı iner. Grafik ve platform katmanları widget import etmez. `extern def` yalnız kabuk, grafik ve metin ölçümündedir.

- `aura/foundation` geometri, renk, kısıt
- `aura/widgets` Widget, Element, State, Signal, Key
- `aura/rendering` RenderBox, PipelineOwner, sahne
- `aura/layout` Padding, SizedBox, Center, Align, Row, Column, Stack, DecoratedBox, sanal liste
- `aura/text` metin, `TextRun`, `RichText`, paragraf, metin alanı
- `aura/input` işaretçi, tuş, gesture arena, odak, bırakma
- `aura/controls` düğme ve anahtar
- `aura/platform` kabuk köprüsü
- `native/macos`, `native/windows`, `native/linux` kabuklar
- `native/skia` Skia raster oynatıcı
- `adr` mimari kararlar
- `examples` sayaç, dikdörtgen, todo, hesap makinesi, kanıt, notes, settings, chat, dosya gezgini, dashboard
- `packages/badge` ikinci paket. Uygulama `register_view` ile bağlar
- `roadmap.md` 0.5’ten mobil temele kadar sözleşme

Nox derleyicisi bu depodan değiştirilmez.

## Komut

`nox.json` bir `bin` girişi yayınlar. Global kurulumdan sonra komut `aura` olur.

```sh
noxc install github.com/mburakmmm/aura --ref v1.3.0
aura doctor -v
aura devices
aura run notes --platform macos
aura run notes --platform linux
aura run notes --platform windows
aura run notes --platform android --device <serial>
aura run notes --platform ios --mode simulator
aura build apk notes
aura build aab notes
aura build ipa notes --mode device
aura build linux notes
aura build windows notes
aura test
aura clean
```

Giriş yazılmazsa, bulunulan dizinden proje köküne kadar `main.nox` aranır. Örnek klasöründe o örneğin `main.nox` dosyası açılır. Proje kökünde `nox.json` içindeki `entry` kullanılır. Çalışma kopyasında `notes` gibi bir örnek adı `examples/notes/main.nox` dosyasına çözülür. `--platform` macOS, Linux, Windows, iOS veya Android seçer. `--mode` derleme kipidir. `--device` simülatör veya telefon kimliğidir.

İkon atanmamışsa uygulama Nox markasını kullanır. Atamak için proje köküne `icon.png` koyun veya `nox.json` içine `"icon": "dosya.png"` yazın. macOS çıktısı `.app` paketidir. Linux çıktısının yanında `.desktop` girişi, Windows çıktısının yanında `.ico` durur. Linux ve Windows kabuk kütüphanesi `scripts/build_host.sh` ile o işletim sisteminde üretilir. Nox programı `--target linux-x64` veya `--target windows-x64` ile derlenir; kabuk kütüphanesi başka bir işletim sisteminin dosyasıysa bağlama durur.

`aura doctor` masaüstü, Android ve iOS araç zincirlerini ayrı satırlarda yazar. Biri eksik olsa da diğerinin durumu görünür. `aura run` Android’de debug APK kurar. Release Android paketi `aura build aab` ile çıkar ve imza ortam değişkeni ister. iOS `store` yalnız paketi üretir. Kabuk kütüphanesi yoksa masaüstü komutları `scripts/build_host.sh` çalıştırır.

## Yayın

Sürüm numarası `nox.json` içindedir. Git etiketi `v` ile başlar ve bu numarayla aynıdır. `noxc` paket kodunu barındırmaz. [noxpkg](https://noxpkg.noxlang.com) yalnız depo adresini, etiketi ve açıklamayı indeksler. Onay admin panelinden verilir.

```sh
./scripts/release.sh
```

Betik testleri çalıştırır ve geçerlilerse etiket, push ve `noxc publish` komutlarını basar. Komutları kendisi çalıştırmaz.

## Lisans

Çerçeve kaynaklarının lisansı henüz seçilmemiştir. `native/fonts/NotoSans-Regular.ttf` SIL Open Font License 1.1 altındadır. Metin `native/fonts/OFL.txt` dosyasındadır. Skia, derleme betiğinin indirdiği üçüncü taraf arşividir ve depoya girmez.
