# ADR-031 Mobil semantics modeli ve eylem eşlemesi

ADR-MOB-AURA-008. Metin şekillendirme kararı `adr/008-text-shaping.md`
dosyasındadır; bu kayıt onun yerine geçmez.

## Context

Mevcut semantics düğümü role, label, value, checked, bounds ve press taşır.
VoiceOver ve TalkBack stabil kimlik, ayrı hint, seçim, heading, live region,
focus sırası ve tap dışındaki eylemler ister. Platform erişilebilirlik iş
parçacığı, UI iş parçacığı ağacı yeniden kurarken yarım bir ağaç okumamalıdır.

Bu dilim cihaz, VoiceOver veya TalkBack oturumu açmaz. Host adapter'ları
M4.2 ve M4.3'tedir. Model ve eşleme onlar yazılmadan önce headless olarak
kilitlenir.

## Decision

`SemanticsNode` yayınlanan snapshot'tır. Widget değildir ve render state
tutmaz. Stabil kimlik, düğümü üreten `RenderObject.node_id` değeridir.
Widget yeniden kurulduğunda render object korunuyorsa kimlik değişmez.

Role, label, value ve hint ayrı alanlardır. Durumlar enabled, selected,
checked, toggled, expanded, obscured, heading, hidden ve focusable olarak
ayrılır. Live region `0` yok, `1` polite, `2` assertive değerlerini alır.
Editable düğüm selection base ve extent taşır. Focusable düğümler ağaç
sırasıyla `focus_order` alır. Bir düğümün `group_id` değeri, role'ü dolu
en yakın atasının kimliğidir.

Eylem adları sabittir: `tap`, `long-press`, `increment`, `decrement`,
`scroll-forward`, `scroll-backward`, `set-text`. `dispatch_semantics_action`
bu adı UI iş parçacığında ilgili render object'e uygular. Tap mevcut press
yolunu, scroll viewport kaydırmasını, set-text ise editing controller'ın
tüm metnini değiştirir. Düğüm eylemi taşımıyorsa dispatch `0` döner.

Gizli düğüm ve viewport kırpmasının tamamen dışında kalan düğüm yayınlanmaz.
Keep-alive ile render ağacında tutulan ekran dışı satır da yayınlanmaz.

Her frame yeni bir ağaç kurulur ve `SemanticsOwner.commit` önceki ağacın
yerine onu koyar. Commit, eski düğüm nesnelerini değiştirmez. Diff yalnız
role'ü dolu düğümleri kimliğe göre added, updated ve removed olarak üretir.
Aynı imza ikinci frame'de boş diff verir.

Platform eşlemesi saf fonksiyondur:

| Aura role | iOS element | iOS trait | Android class |
| --- | --- | --- | --- |
| button | UIButton | button | android.widget.Button |
| toggle | UISwitch | button | android.widget.Switch |
| text | UILabel | static-text | android.widget.TextView |
| text + heading | UIAccessibilityElement | header | android.widget.TextView |
| text-field | UITextField | text-field | android.widget.EditText |
| image | UIImageView | image | android.widget.ImageView |
| scroll-view | UIScrollView | none | android.widget.ScrollView |
| adjustable | UISlider | adjustable | android.widget.SeekBar |

Android `ACTION_CLICK` 16 tap, `ACTION_LONG_CLICK` 32 long-press,
`ACTION_SET_TEXT` 2097152 set-text olur. `ACTION_SCROLL_FORWARD` 4096,
düğüm increment taşıyorsa increment, aksi halde scroll-forward olur.
`ACTION_SCROLL_BACKWARD` 8192 aynı ayrımla decrement veya scroll-backward
olur. Bir düğüm hem increment hem scroll-forward taşımaz. iOS token'ları
`activate`, `increment`, `decrement`, `scroll-forward`, `scroll-backward`
ve `set-text` aynı Aura adlarına döner. Düğümde olmayan eylem boş döner.

Mevcut `aura_ax_add` imzası değişmez. Desktop ve mobil host'lar bugün role,
label, value, checked, bounds ve press okumaya devam eder. Yeni alanların
FFI'ye taşınması VoiceOver ve TalkBack adapter'ının işidir.

## Alternatives

Her frame tüm ağacı platforma yeniden bildirmek focus'u düşürür ve gereksiz
yeniden duyuru üretir. Semantics düğümünü yerinde mutate etmek, platform
okuması ile UI frame'ini aynı nesnede yarıştırır. Android action kodunu
role'den bağımsız tek anlama bağlamak, adjustable ile scrollable'ı ayıramaz.
UIKit veya AccessibilityNodeInfo'yu bu dilimde doldurmak, cihaz kanıtı
olmadan adapter'ı tamamlanmış gibi gösterir.

## Consequences

İkinci aynı frame semantics diff'i boştur. Hint veya selection değişimi
yalnız o kimliği updated yapar. Ekran dışı virtual satır render ağacında
kalabilir fakat semantics ağacına girmez. Accessibility eylemi state
mutation'a aynı UI frame sözleşmesiyle ulaşır. VoiceOver trait'lerinin ve
TalkBack node'larının gerçek platform nesnesine yazılması hâlâ açıktır.

## Migration

Eski `tap` eylemi ve press id korunur. `aura_ax_*` çağrılarının imzası
aynıdır. Yeni alanlar varsayılan değerle başlar; mevcut describe
implementasyonları derlenmeye devam eder.

## Risks

Kimlik, render object dispose edilince yeniden kullanılırsa eski platform
focus'u yanlış düğüme bağlanabilir. Adapter yazılırken focus, node id
yaşadığı sürece tutulmalıdır. Viewport kırpması kısmi görünen satırı
yayınlamaya devam eder; tamamen dışarıdaki satır yayınlanmaz.

## Rollback

Commit ve diff kaldırılır, collect yeniden tek kökü yerinde doldurur.
Eylem dispatch'i ve platform eşleme fonksiyonları silinir. `aura_ax_add`
yolu zaten değişmediği için host binary'leri rollback'ten etkilenmez.
