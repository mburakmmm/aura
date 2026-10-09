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

**Şu anki sürüm 0.4.0’dır.** Aynı program macOS, Windows ve Linux kabuklarında sertifikalıdır. Kapı [roadmap.md](roadmap.md) bölüm 0.4’tür: `scripts/certify.sh` testleri ve beş gizli self-check’i koşturur. GitHub Actions bunu macOS, Win32, X11 ve Wayland üzerinde çalıştırır.

## Yol haritası

Bağlayıcı teslimat sözleşmesi [roadmap.md](roadmap.md) dosyasındadır. `AGENTS.md` invariant’ları her sürümde geçerlidir. Bir sürüm bitmeden sonrakine geçilmez.

| Sürüm | Teslim |
| --- | --- |
| 0.4 | Tamam: üç masaüstü kabuğunda aynı self-check |
| 0.5 | Notes: sarılmış metinde seçim ve pano |
| 0.6 | Settings: tema ve pencere API’si |
| 0.7 | Chat: değişken satır yüksekliğinde sanal liste |
| 0.8 | Dosya gezgini: UI iş parçacığı dışında iş |
| 0.9 | Dashboard: örtük animasyon, altın görüntü, kare bütçesi |
| 1.0 | Genel API dondurma |
| 1.1–1.5 | iOS kabuğu, klavye, dokunuş, Android, GPU yüzeyi |

Web, hot reload, sliver ve zengin metin bu sözleşmenin dışındadır.

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
```

`examples/proof` 0.3’ü tek ekranda gösterir: paketlenmiş font, PNG ve JPEG, opaklık, debug sınırı, kayıtlı `badge` görünümü ve yerli `PlatformView`.

## Test ve ölçüm

```sh
./scripts/test.sh
./scripts/certify.sh
./scripts/bench.sh
```

`test.sh` kabuğu derler ve `noxc test` çalıştırır. `certify.sh` buna beş gizli self-check ekler ve pencereyi açık bırakmaz. Benchmark sekiz senaryonun faz sürelerini ve kurulum sayaçlarını yazar.

## Düzen

Bağımlılık yukarıdan aşağı iner. Grafik ve platform katmanları widget import etmez. `extern def` yalnız kabuk, grafik ve metin ölçümündedir.

- `aura/foundation` geometri, renk, kısıt
- `aura/widgets` Widget, Element, State, Signal, Key
- `aura/rendering` RenderBox, PipelineOwner, sahne
- `aura/layout` Padding, SizedBox, Center, Row, Column, sanal liste
- `aura/text` metin, paragraf, metin alanı
- `aura/input` işaretçi, tuş, gesture arena, odak, bırakma
- `aura/controls` düğme ve anahtar
- `aura/platform` kabuk köprüsü
- `native/macos`, `native/windows`, `native/linux` kabuklar
- `native/skia` Skia raster oynatıcı
- `adr` mimari kararlar
- `examples` sayaç, dikdörtgen, todo, hesap makinesi, kanıt
- `roadmap.md` 0.4’ten mobil temele kadar sözleşme

Nox derleyicisi bu depodan değiştirilmez.

## Yayın

Sürüm numarası `nox.json` içindedir. Git etiketi `v` ile başlar ve bu numarayla aynıdır. `noxc` paket kodunu barındırmaz. [noxpkg](https://noxpkg.noxlang.com) yalnız depo adresini, etiketi ve açıklamayı indeksler. Onay admin panelinden verilir.

```sh
./scripts/release.sh
```

Betik testleri çalıştırır ve geçerlilerse etiket, push ve `noxc publish` komutlarını basar. Komutları kendisi çalıştırmaz.

## Lisans

Çerçeve kaynaklarının lisansı henüz seçilmemiştir. `native/fonts/NotoSans-Regular.ttf` SIL Open Font License 1.1 altındadır. Metin `native/fonts/OFL.txt` dosyasındadır. Skia, derleme betiğinin indirdiği üçüncü taraf arşividir ve depoya girmez.
