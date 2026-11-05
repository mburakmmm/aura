# Aura mobil uyarlama ana TODO listesi

> Durum tarihi: 5 Kasım 2026  
> Kapsam: Aura framework, iOS/Android host kabukları, ortak mobil API,
> rendering, input, erişilebilirlik, araçlar, paketleme ve yayın kapıları.  
> Kapsam dışı: Nox derleyicisi, Nox mobil target/runtime implementasyonu.
> Aura yalnız sürümlü ve belgelenmiş Nox mobil/embed sözleşmesini kullanır.

Bu dosya Aura'nın mobil çalışması için yürütme defteridir. `AGENTS.md`
invariant'ları her maddede geçerlidir. Çelişkide `AGENTS.md` kazanır.
Mimari sınırı değiştiren implementasyon ilgili ADR kabul edilmeden başlamaz.

## Durum işaretleri

- `[ ]`: başlanmadı
- `[~]`: kısmi; kendi kabul kapısı henüz geçmedi
- `[x]`: kanıtla tamamlandı
- `[!]`: kullanıcı, mağaza hesabı, fiziksel cihaz veya dış karar bekliyor

Bir görev yalnız kod yazıldığı için tamamlanmış sayılmaz. İlgili test, cihaz
kanıtı, ownership kontrolü, doküman ve geriye uyumluluk kapısı da geçmelidir.

## Değişmez uygulama sınırları

- [ ] Widget, Element ve RenderObject ayrımı mobilde de korunacak.
- [x] Widget/example kaynaklarında UIKit, Android/JNI tipi veya `extern def`
  bulunmayacak.
- [ ] Platform host widget ağacı kurmayacak; yalnız yüzey, olay, lifecycle,
  OS servisi ve paketleme sağlayacak.
- [ ] Platform olayları doğrudan UI state mutate etmeyecek; sürümlü kuyruğa
  yazılacak.
- [ ] UI ağacı tek mantıksal UI thread'den mutate edilecek.
- [ ] Build/layout/paint callback'lerinde blocking I/O yapılmayacak.
- [ ] Native handle ve JNI/Objective-C nesne ownership'i public API'ye
  sızmayacak.
- [ ] Nox compiler bu repodan değiştirilmeyecek.
- [ ] Masaüstü macOS/Windows/Linux davranışı her mobil PR'da korunacak.
- [ ] Desteklenmeyen özellik sessiz no-op olmayacak; açık diagnostic verecek.

## Doğrulanmış başlangıç durumu

- [x] Masaüstü ve yeni mobil headless testleri güncel Nox 2.0 toolchain ile
  34/34 geçti; mobile native contract ve sanitizer kapıları da yeşil.
- [x] Tarihsel koruma `mobile-sources` dalı `2cb8177` duruyor. Güncel kaynak
  o dalın worktree'si değil; `main` üzerindeki bu kayıt.
- [x] Widget/example platform kaçışı için ilk contract taraması geçiyor.
- [x] Tek pointer koordinat dönüşümü native birim testi geçiyor.
- [~] iOS simulator ve Android ARM64 için geçmiş preview kanıtı var; güncel
  temiz checkout/CI kanıtı yok.
- [~] Dört Nox mobil runtime artifact'i yerelde var; mobil Skia arşivleri
  scriptlerin beklediği konumda yok.
- [~] Mobil kaynaklar `main` üzerinde izleniyor. Bu ağacın CI kaydı yok.

## 5 Kasım 2026 — iPhone 17 Pro turu

Cihaz UDID `00008150-001974CC0163401C`. Kurulum development imzasıyla ve
elle yazılmış entitlements ile yapıldı. Xcode projesi, `.xcarchive`, imzalı
IPA ve TestFlight bu turun parçası değil. GPU, emoji, çift yön ve TalkBack
yok. Yazılan metin süreç ölünce durmuyor; Notes belgesi derlenmiş başlangıç
metnidir.

- [x] Notes: başlık durum çubuğunun altında, döndürme, kısa notta sürükleyerek
  seçim. Uygulama öldürülüp açılınca derlenmiş metin geri gelir.
- [x] Counter: metin okunur, düğme sayacı artırır.
- [x] Chat: yazılan mesaj gönderilir, composer görünür.
- [x] Settings: açık temada Switch mürekkebi, yatayda koyu kart kayar, kaydırma
  çubuğu hareketten sonra solar.
- [x] Todo: başlık koyu mürekkep. Open ayrıntıyı açar. Picture 160 pt kırmızı
  kare ve Close gösterir. Save başlığı listeye yazar. Done topuzu yer değiştirir.
  Delete satırı siler.
- [x] Calculator: sayı Dynamic Island'ın altında kalır. `1 + 2 =` ekranı değiştirir.
- [x] Files: kök Documents'tır. `welcome.md` ve `notes` açılır. Back klasörden,
  Close nottan bir önceki yere döner.
- [x] Dashboard: `0`, Open, Idle ve Refresh görünür. Open alttaki kartı büyütür
  ve "Queue is receiving work" ile turuncu Open düğmesini gösterir. Idle kartı
  değiştirir.
- [~] VoiceOver, Notes üzerinde kelime dokunuşu, sağ/sol kaydırma, çift dokunuşla
  düzenleme, başlık rotoru, 33 öğelik chooser, öneri çubuğu ve klavye Done
  görüldü. Counter, Chat ve Settings için aynı tur yok. Özel rotor eylemi açık.
- [ ] Süreç ölümü sonrası düzenlenmiş metin.
- [ ] iPad, soğuk açılış ölçümü, 10.000 satır Chat döndürmesi.

## Kritik yol

```text
M0 kaynak ve ADR
  -> M1 ortak mobil sözleşme
  -> M2 input/gesture + M3 text/IME + M4 semantics
  -> M5 iOS host + M6 Android host
  -> M7 rendering/performance
  -> M8 mobil widget/UX + M9 platform entegrasyonu
  -> M10 tooling/paketleme
  -> M11 CI/cihaz laboratuvarı
  -> M12 preview/supported yayın kapıları
```

---

## M0 — Kaynak, baseline ve mimari karar kapısı

### M0.1 Kaynağı güvenli ve izlenebilir hale getir

- [x] Untracked mobil kaynakları ayrı commit'te güvenceye al.
- [x] Ayrı worktree açılmadı. Güncel kaynak `main` commit'ine alındı.
  `mobile-sources` `2cb8177` tarihsel koruma olarak duruyor.
- [x] `main` üzerindeki kopyalar bu commit'te kaybolmadan izlendi.
- [x] Kaynak, üretilmiş artifact ve yerel ortam girdilerini sınıflandır.
- [x] Üretilmiş APK/AAB/IPA/app/object/archive/keystore dosyalarını ignore et.
- [x] Gerçek kaynaklar `main` üzerinde izleniyor.
- [x] Mobil kaynak ağacında ikili veya signing secret bulunmadığını CI'da tara.
- [x] Her mobil commit için Aura ve Nox commit kimliklerini kanıta yaz.

### M0.2 Baseline ve uyumluluk

- [x] Masaüstü unit/widget/render test baseline'ını kaydet.
- [x] On masaüstü self-check'i mobil PR kapısına bağla.
- [ ] Benchmark baseline'ını build/layout/paint/composite ve allocation ile
  kaydet.
- [x] Aura'nın desteklediği minimum/maksimum Nox sürüm aralığını manifestte
  tanımla.
- [ ] Nox sürümü değişince Aura uyumluluk işini otomatik tetikle.
- [x] Nox embed ABI sürüm uyuşmazlığında erken ve açıklayıcı hata üret.

### M0.3 Katman ve FFI sınırı denetimi

- [x] `aura/platform` katmanının widgets import etmediğini test et.
- [x] `aura/graphics` katmanının widgets/platform-native tip import etmediğini
  test et.
- [x] Widget/example `.nox` dosyalarında UIKit/JNI/Activity/UIView/extern
  kaçışını token-aware contract testiyle engelle.
- [x] FFI sembollerini platform, graphics ve text backend ile sınırla.
- [x] iOS/Android host'un yalnız ortak sürümlü `native/mobile` C sembol
  kümesini uyguladığını contract testiyle doğrula.
- [x] Platform-specific `if` kontrollerinin framework boyunca dağılmadığını
  denetle.

### M0.4 Zorunlu ADR seti

- [x] ADR-MOB-AURA-001: iOS host, scene ve UI-thread modeli
  (`adr/020-ios-host-scene-ui-thread.md`).
- [x] ADR-MOB-AURA-002: Android host, Activity/Surface ve UI-thread modeli
  (`adr/019-android-host-surface-lifecycle.md`).
- [x] ADR-MOB-AURA-003: sürümlü event queue/ABI, ordering ve coalescing
  (`adr/015-mobile-event-queue.md`).
- [x] ADR-MOB-AURA-004: lifecycle, scheduler, background ve restoration
  (`adr/017-mobile-lifecycle-scheduler.md`).
- [x] ADR-MOB-AURA-005: logical pixel, safe area, inset ve WindowMetrics
  (`adr/016-window-metrics-safe-area.md`).
- [x] ADR-MOB-AURA-006: multi-pointer gesture arena
  (`adr/021-multi-pointer-gesture-arena.md`) ve nested scroll delta tüketimi
  (`adr/022-nested-scroll-consumption.md`).
- [x] Ek ADR: mobil system/predictive back, root isolation ve batched route
  mutation (`adr/023-mobile-system-back.md`).
- [x] ADR-MOB-AURA-007: sürümlü `TextEditingValue`, iOS `UITextInput` ve
  Android `InputConnection` sınırı
  (`adr/028-text-editing-session-revisions.md`).
- [x] ADR-MOB-AURA-008: VoiceOver/TalkBack semantics mapping ve action
  (`adr/031-mobile-semantics-actions.md`).
- [ ] ADR-MOB-AURA-009: Skia raster fallback ve GPU backend seçimi.
- [ ] ADR-MOB-AURA-010: PlatformView/plugin composition ve signing.
- [x] ADR-MOB-AURA-011: native ownership, JNI/ObjC lifetime ve shutdown
  (`adr/018-mobile-native-runtime-ownership.md`).
- [x] ADR-MOB-AURA-012: asset, manifest, package ve reproducible build
  (`adr/032-mobile-package-manifest.md`).
- [x] ADR-MOB-AURA-013: storage roots, cache ve state restoration
  (`adr/033-mobile-storage-roots.md`). Process death sonrası Navigator
  kurulumu açık.
- [ ] ADR-MOB-AURA-014: permissions, privacy ve platform service sınırı.
- [ ] Her ADR'ye reddedilen alternatif, risk, migration ve rollback ekle.

### M0 çıkış kapısı

- [x] Mobil kaynakların tamamı `main` üzerinde izleniyor.
- [ ] ADR seti kabul edildi.
- [ ] Desktop test/self-check/benchmark baseline'ı yeşil.
- [x] Katman ve FFI contract testleri CI'da.
- [x] Hardcoded yerel makine yolu source contract değil.

---

## M1 — Ortak mobil runtime ve event sözleşmesi

### M1.1 Sürümlü event modeli

- [x] `MobileEvent`/ortak platform event yapısına ABI version ve struct size
  ekle.
- [x] Her olaya shell/window başına monoton sequence number ekle.
- [x] Pointer, lifecycle, metrics, environment, memory, surface, text edit ve
  route olayları callback monoton timestamp'ini taşıyor. Sıfır değer shell
  girişinde doldurulur; bekleyen route ve surface aynı değeri korur.
- [x] Device ID ve pointer ID alanlarını ekle.
- [x] Pointer kind: touch/mouse/stylus/trackpad ayrımını ekle.
- [x] Buttons, modifiers, pressure, tilt ve scroll phase alanlarını modelle.
- [x] Text payload için açık UTF-8 length/ownership sözleşmesi, deep-copy ve
  clear/reset/destroy testi ekle.
- [x] Resize/metrics bilgisini typed payload'a taşı; iOS ve Android adapter'ını
  eski pointer alanlarından geçirmenin dışına çıkar.
- [x] Lifecycle event'lerini ayrı typed payload ile modelle ve fixture'da
  doğrula.
- [x] Memory pressure seviyesi typed payload, shell kuyruğu ve
  LifecycleOwner observer'ı ile testli. Cache tahliye politikası yok.
- [~] Surface create/change/destroy typed payload, shell state machine ve iki
  adapter bağlantısı testli; cihaz/instrumentation fixture'ı bekliyor.
- [x] Brightness, text scale, accessibility, bold text ve locale listesi
  environment event'i ile Binding'e bağlandı; shell kuyruk testi geçiyor.
- [x] Keyboard visibility/inset değişimini surface boyutundan ayırıp iOS ve
  Android typed metrics payload'ına bağla.
- [x] Deep-link location ve source host kuyruğundan Binding üzerinden
  Navigator'a gidiyor. Boş location ve kapanmış pencere hata üretiyor.

### M1.2 Kuyruk semantiği

- [x] Tek producer varsayımını mutex'li queue ve dört producer stres testiyle
  kaldır.
- [x] Queue ordering'i platform callback sırasıyla tanımla ve test et.
- [x] Pointer move ve ardışık metrics coalescing politikasını yaz ve test et.
- [x] Down/up/cancel/text/lifecycle event'lerinin coalesce edilmediğini test et.
- [x] Frame boundary sırasında gelen event'in hangi frame'e ait olduğunu
  tanımla.
- [x] Queue için 1024 event üst sınırı, coalescible eviction ve bounded
  snapshot politikasını uygula.
- [x] Overflow'da typed diagnostic/counter üret ve aktif pointer sequence'ini
  cancel et.
- [x] Event text payload'ını deep-copy et ve clear/reset/destroy sırasında tam
  serbest bırak.
- [x] Concurrent append/read/clear stres testi yaz.
- [x] 100.000 event deterministik fuzz ile ASan/UBSan kapısı ekle.

### M1.3 WindowMetrics ve ortam modeli

- [x] `WindowMetrics` typed value-object API'sini ekle.
- [x] Logical width/height taşı.
- [x] Device pixel ratio taşı.
- [x] Physical pixel size taşı.
- [x] Safe-area/view padding: left/top/right/bottom taşı.
- [x] Keyboard/view inset: left/top/right/bottom taşı.
- [x] System gesture inset ve en fazla 8 cutout dikdörtgeni metrics'te.
  iOS public cutout listesi vermez; notch view padding'de kalır.
- [x] Orientation ve portrait'tan saat yönünde `rotation_degrees` value
  object'te ve shell testinde.
- [x] Refresh rate taşınıyor; `frame_budget_ms` hızdan türetilir.
- [x] Text scale Android `fontScale` ve iOS Dynamic Type body punto / 17
  olarak metrics'e gider.
- [x] Platform brightness, high contrast, reduce motion, VoiceOver/TalkBack
  ve bold text preference event'i host'tan Binding'e gider.
- [x] Locale listesi satır sonuyla ayrılmış BCP-47; birincil locale ilk
  satırdır. Layout direction aynı environment event'indedir.
- [x] Reduce motion animasyonu hedefe sıçratır ve fling'i yerinde durdurur.
  Bold text aynı snapshot'tadır.
- [x] Metrics'i Inherited state üzerinden yalnız bağımlılara yayınla.
- [x] Eşit metrics değerinde gereksiz rebuild üretme.
- [x] `SafeArea` widget'ını typed insets üzerine kur.
- [x] Keyboard inset'i pencere boyutunu kalıcı olarak bozmak yerine ayrı alan
  olarak sun; resize politikası açık olsun.

### M1.4 Uygulama lifecycle modeli

- [x] `AppLifecycleState` değerlerini tanımla: detached/inactive/resumed/
  paused/hidden/terminated veya ADR'de seçilen eşdeğerler.
- [x] Lifecycle observer public API'sini ekle.
- [x] Lifecycle state'i global singleton yapma; binding/scene ile ilişkilendir.
- [x] Background'da yeni animation tick üretme.
- [x] Resume'da delta'nın dev sıçramasını önle.
- [x] Pending pointer sequence'i pause/surface loss sırasında cancel et.
- [x] Text input/focus'u lifecycle ile deterministik kapat/aç.
- [x] Network/disk task'larını framework adına zorla durdurma; state uygulama
  sözleşmesini belgele.
- [x] Post-frame callback'lerin background davranışını tanımla.
- [x] Terminate gelmeyebileceği için periyodik/restoration save hook'u sun.
- [x] Lifecycle reentrancy testli. Layout sırasında layout/paint ve paint
  sırasında layout/paint `illegal pipeline transition` tanısı üretiyor.

### M1.5 Native resource ownership

- [~] Nox app create/start/stop/destroy ortak host state machine'de; ayrı embed
  pump modeli gerekirse sözleşme/fixture genişletilecek.
- [x] create fail, partial init, start fail ve exception cleanup testlerini yaz.
- [x] stop/destroy idempotency politikasını Nox public contract ile eşleştir.
- [x] Shell window/event/AX/pixel/text kaynakları, iOS canvas/keyboard observer
  ve Android JNI global ref kapanıyor. Android surface state machine headless
  testte geçiyor.
- [ ] Cache/resource handle'larında dispose sonrası deterministic hata üret.
- [x] Process-global `chdir` kaldırıldı. Beş depolama kökü ayrıldı.
- [x] Thread init/shutdown sırasını açıkça test et.
- [x] Runtime exception bilgisini güvenli API ile logla; object layout offset'i
  varsayma.

### M1 çıkış kapısı

- [x] Ortak event/metrics/lifecycle sözleşmesi headless testte geçiyor.
- [ ] Aynı fixture iOS ve Android native adapter testinde aynı normalized
  event dizisini üretiyor.
- [x] Lifecycle/event queue ve native ownership state-machine sanitizer
  kapıları yeşil.

---

## M2 — Mobil pointer, gesture, focus ve navigation input

### M2.1 Pointer normalization

- [x] Her aktif parmağı stabil pointer ID ile takip et.
- [x] Aynı platform event'indeki bütün pointer değişimlerini sırayla aktar.
- [x] ACTION_POINTER_DOWN/UP ayrımını Android'de uygula.
- [x] UITouch identity'sini iOS'ta pointer ID'ye map et.
- [~] Android historical move ve iOS coalesced touch örnekleri sıralı
  aktarılıyor; cihaz ölçümü bekliyor.
- [x] Cancel ve surface loss'ta bütün aktif pointer'ları temizle.
- [x] Koordinatı logical pixel'e tek noktada dönüştür.
- [x] Safe-area'yı pointer koordinatından keyfi çıkarmak yerine viewport
  transform sözleşmesiyle eşleştir.
- [~] Tool type, pressure ve stylus tilt iki adapter'da taşınıyor; cihaz
  matrisi bekliyor.
- [~] iOS hover ve dolaylı trackpad scroll host'ta üretiliyor; tablet testi
  bekliyor.

### M2.2 Gesture arena

- [x] Arena state'ini pointer ID başına ayır.
- [x] Tap recognizer'ı pointer cancel ve yarış kaybını bildirecek şekilde
  tamamla.
- [x] Double tap ekle.
- [x] Long press ekle.
- [x] Pan/drag recognizer ekle.
- [x] Horizontal/vertical drag eksen kilidi ekle.
- [x] Scale/pinch recognizer ekle.
- [ ] Gerekirse rotation recognizer ekle.
- [x] Multi-pointer recognizer ile single-pointer scroll çatışmasını çöz;
  başlamış single-pointer winner ikinci pointer tarafından çalınmıyor.
- [x] Nested scroll arena politikasını tanımla; leaf-to-root remainder tüketimi
  ve yön tersine dönünce leaf'e geri dönüş testli.
- [~] Button-in-scroll, text-selection-in-scroll ve Android predictive-back
  çatışmaları testli; iOS swipe-back entegrasyonunu bekliyor.
- [x] Touch slop ve velocity eşiklerini adapter DPR normalizasyonu sonrası
  logical pixel / logical pixel-per-second biriminde ortak sözleşmeye taşı.
- [x] Fling'i platform timestamp'iyle hesapla.
- [x] 60/90/120 Hz'de gesture sonucu eşdeğer olsun.

### M2.3 Focus, keyboard ve system navigation

- [x] Focus tree'yi mobile scene/binding lifecycle'ına bağla.
- [x] Focus traversal order ve directional focus ekle.
- [x] Hardware keyboard key/code/modifier normalization yap.
- [x] Android system/predictive back'i Overlay→Navigator `pop` akışına bağla.
- [x] iOS interactive swipe-back politikasını tasarla; capability-gated edge
  recognizer, retained iki-route preview ve batched Navigator transaction ile uygula.
- [x] Back gesture sırasında route state mutation'ını batched uygula.
- [x] Back reddedilebiliyorsa handled/unhandled ABI sonucu üret ve Android'de
  unhandled kök rotayı Activity kapanışına yönlendir.
- [x] App link/universal link/intent route girişini typed API ile Navigator'a
  taşı.
- [x] Route stack restoration formatını sürümle.

### M2 çıkış kapısı

- [ ] Tek ve çok parmak test matrisi iki platformda geçiyor.
- [~] Button/scroll/text selection/Android back yarışları deterministik; iOS
  interactive swipe-back root/overlay/ikinci-pointer çatışmaları headless testli,
  fiziksel cihaz matrisi bekliyor.
- [ ] Chat 10.000 satırla touch scroll ve rotation sonrası sanal kalıyor.

---

## M3 — Text input, IME, seçim ve clipboard

### M3.1 Ortak editing sözleşmesi

- [x] `TextEditingValue`: text, selection, composing range tanımla.
- [x] Platforma gönderilen editing state için revision/generation ekle.
- [x] Platformdan gelen eski revision'ı reddet veya deterministik birleştir.
- [x] Selection ve composing range'i UTF-8 byte değil Unicode/grapheme
  semantiğiyle tanımla.
- [x] Grapheme-aware cursor hareketi ve backspace ekle.
- [x] Word/line selection contract'ı ekle.
- [x] Cursor rectangle ve editable bounds'u host'a ilet.
- [x] Keyboard config: text type, action, multiline, secure, autocorrect,
  capitalization ve autofill hints ekle.
- [x] Focus değişiminde text session create/update/close yap.

### M3.2 iOS `UITextInput`

- [~] `UIKeyInput` yerine `UITextInput` adapter ve derleme/contract kapısı
  eklendi; layout-doğru range geometry ve simulator IME acceptance bekliyor.
- [x] Marked text set/unmark uygula.
- [x] Selected text range get/set uygula.
- [x] Replace range uygula.
- [x] Beginning/end document ve position/range API'lerini uygula.
- [~] First rect, caret rect ve closest position metin geometri
  snapshot'ından türetiliyor. iPhone 17 Pro'da Notes imleci satır sonunda,
  dokunulan yerde ve sürüklenen aralıkta duruyor. Uzun basış bırakılan
  yere kadar seçiyor; silme o aralığı kaldırıyor. Composition fixture'ı yok.
- [x] Input delegate notifications sırasını doğru uygula.
- [x] Return/newline/action davranışını config'e bağla.
- [x] Secure input ve autofill/content type politikasını uygula.
- [x] Hardware keyboard command'larını normalize et.

### M3.3 Android `InputConnection`

- [x] `BaseInputConnection` üstünde gerçek editable state sağla.
- [x] `setComposingText`, `setComposingRegion`, `finishComposingText` uygula.
- [x] `setSelection`, `getSelectedText`, surrounding text API'lerini uygula.
- [x] `deleteSurroundingText` range'lerini koru.
- [x] `beginBatchEdit`/`endBatchEdit` uygula.
- [x] `performEditorAction` uygula.
- [x] Extracted text ve cursor anchor info gerektiği kadar uygula.
- [~] Config değişince `restartInput`, değer değişince `updateSelection`
  sırası host kodunda; instrumentation fixture'ı bekliyor.
- [x] Hardware key event ile IME commit'i birbirine karıştırma.

### M3.4 Clipboard ve editing UX

- [x] Clipboard için sabit boy tamponu kaldır.
- [~] Text edit ve clipboard okuma uzunlukla gömülü NUL koruyor; yazma yolu
  Nox FFI yüzünden C string. Cihaz emoji fixture'ı bekliyor.
- [x] Copy/cut/paste/select-all actions ekle.
- [x] Seçim tutamaçları ve düzenleme menüsü ADR-030 ile sonraki overlay
  fazına bağlandı. Seçim ve pano sözleşmesi controller'da kalır.
- [x] Clipboard failure recoverable diagnostic üretmeli.
- [x] Klavye inset'i kök içeriği kaydırır; imleç inset'in üstünde kalır ve
  inset kapanınca kayma sıfırlanır.

### M3.5 Test matrisi

- [~] Türkçe harf dizisi ve grapheme backspace headless geçiyor. iPhone 17 Pro
  Türkçe klavyesinde Notes'a yazıp silmek çalışıyor. Tam Q/F tarama yok.
- [~] İngilizce metin commit ve backspace headless geçiyor; fiziksel klavye
  taraması cihaz fixture'ı bekliyor.
- [x] CJK composition commit olmadan modelde kalmıyor.
- [x] Emoji ve birleşik grapheme backspace tek grapheme siliyor.
- [x] RTL düzenlemede sağ kenar başlangıç, sol kenar bitiş imlecini koyuyor.
- [x] Selection replace ve delete aralığı committed metne yazılıyor.
- [x] Multiline Notes satır sonunu koruyor.
- [x] Chat send ve done eylemleri metne satır sonu yazmadan callback'e gidiyor.
- [x] Klavye kapanışı oturumu açık tutuyor; background bitmemiş composition'ı
  atıyor.
- [x] Rotation aktif composition'ı ve committed metni koruyor.

### M3 çıkış kapısı

- [ ] Notes ve Chat aynı acceptance testlerini iOS/Android'de geçiyor.
- [x] Composition commit olmadan modele yazılmıyor.
- [ ] Selection, clipboard ve keyboard inset iki platformda tutarlı.

---

## M4 — Semantics, accessibility, localization ve adaptif ortam

### M4.1 Semantics modelini tamamla

- [x] Semantics node'a stabil ID ekle.
- [x] Role, label, value ve hint'i ayrı tut.
- [x] enabled, selected, checked, toggled, expanded ve obscured state ekle.
- [x] heading, live region ve focusable state ekle.
- [x] Tap/activate, long press, increment/decrement ve scroll actions ekle.
- [x] Editable text, selection ve set-text actions ekle.
- [x] Semantics bounds/transform/clip'i scene ile eşleştir.
- [x] Hidden/offscreen virtual-list node'larını yayınlama.
- [x] Focus order ve traversal grouping ekle.
- [x] Semantics diff üret; her frame bütün ağacı gereksiz yeniden bildirme.
- [x] Platform UI thread okurken semantics snapshot'ını immutable tut.

### M4.2 VoiceOver

- [x] Yayımlanan role'lar iOS trait eşlemesine bağlandı. Heading biti
  `aura_ax_set_node` ile son eklenen node'a yazılır; `aura_ax_add` imzası
  durur. Cihaz VoiceOver kanıtı yok.
- [x] `accessibilityActivate` press kimliği varsa AX action'ı event kuyruğuna yazar.
- [x] Adjustable increment/decrement `AURA_AX_ACTION` ile node kimliğine gider.
  Headless kuyruk ve frame testi geçti. Cihaz rotor kaydırması kanıtlı değil.
- [ ] Editable text semantics ve selection uygula.
- [x] İlk yayın screen change, node kimliği değişince layout change, aynı
  kimlikte değer güncellemesi bildirimsiz. Headless sınıflandırma testi geçti.
  Cihaz odağı kanıtlı değil.
- [~] VoiceOver öğesi node kimliğiyle yeniden kullanılır;
  `indexOfAccessibilityElement` aynı nesneyi döner. Cihaz kanıtı yok.
- [~] Heading trait'i başlık node'una eklenir. Notes başlığı `as_heading`.
  iPhone 17 Pro'da headings rotoru bu başlığı gördü. Özel rotor eylemleri açık.

### M4.3 TalkBack

- [x] Role'lar Android class adına map edildi.
- [x] Checked state ve click action'ı node'a yazılıyor. Scroll ve set-text
  sonraki maddede açık.
- [x] `ACTION_CLICK` press kimliği varsa event kuyruğuna yazılıyor.
- [ ] Accessibility focus create/clear actions uygula.
- [ ] Hover exploration event'lerini sanal node'lara map et.
- [ ] Text selection/set-text ve scroll actions uygula.
- [ ] Content change event'lerini diff'e göre yayınla.

### M4.4 Adaptif ve uluslararası UI

- [x] Locale değişimi WindowMetrics bağımlılarını yeniden kuruyor.
- [x] `TextDirection` Row ana eksenini ve Column çapraz eksenini çeviriyor.
- [ ] Unicode bidi ve font fallback backend'ini tamamla.
- [ ] Emoji/color font destek matrisini tamamla.
- [x] Dynamic Type/Android font scale metin yerleşimini ölçekliyor.
- [x] High contrast ve bold text tercihleri tema renk ve ağırlığına bağlandı.
- [x] Reduce motion animasyonu hedefe sıçratır, fling'i yerinde durdurur.
- [x] Light/dark sistem teması `SystemTheme` ve pencere zeminine bağlandı.

### M4 çıkış kapısı

- [ ] Counter, Notes, Chat ve Settings VoiceOver/TalkBack ile kullanılabilir.
- [x] Accessibility actions gerçekten state mutation'a ulaşıyor.
- [ ] Türkçe, İngilizce ve bir RTL locale acceptance testinden geçiyor.

---

## M5 — iOS host ve cihaz kapısı

### M5.1 Scene ve lifecycle

- [x] `UISceneDelegate` state'lerini Aura lifecycle kuyruğuna yaz.
- [x] inactive/active/background/foreground/disconnect ayrımını koru.
- [x] App process lifecycle ile scene lifecycle'ı karıştırma.
- [x] Tek scene desteğini manifest ve runtime'da açıkça bildir.
- [x] iPad multi-window erteleniyorsa destek matrisinde açıkça yaz.
- [ ] Multi-window hedeflenirse scene -> Binding/Window mapping uygula.
- [x] Background'da `CADisplayLink` pause et.
- [x] Foreground'da display link'i güvenli yeniden başlat.
- [x] Refresh-rate değişimini metrics'e aktar.
- [x] Memory warning'i cache/resource cleanup kuyruğuna yaz. Kritik seviyede
  cache çocukları silinir; documents kalır.
- [x] Scene state restoration activity'si `AURA-RS` sürüm 1 adını taşır.
  Yığın dosyada kalır. Bilinmeyen sürüm snapshot'ı siler. Cihazda sahne
  geri yükleme turu yok.

### M5.2 Surface, metrics ve input

- [ ] UIWindow/UIWindowScene ownership'ini global tekil yerine scene state'e
  bağla.
- [x] Safe-area dört kenarı metrics'e aktar.
- [x] Klavye inset, süre ve eğri metrics event'ine aktarılıyor.
- [~] Rotation metrics'e aktarılıyor; size-class bekliyor.
- [ ] External display/scale değişimini doğru işle.
- [~] UITouch identity, timestamp ve coalesced move aktarılıyor. iPhone 17
  Pro'da tek parmak dokunuş Proof düğmelerine ulaşıyor; UIKit noktası ölçekle
  çarpılıp logical piksele iniyor. Coalesced örnek zamanlaması ölçülmedi.
- [~] Trackpad/mouse hover ve dolaylı scroll host'ta üretiliyor; tablet
  testi bekliyor.
- [~] Scene resign aktif pointer'lara cancel basıyor; instrumentation
  bekliyor.

### M5.3 Storage ve process

- [x] Documents, Application Support, Caches ve tmp yollarını ayrı çöz.
- [x] Backup'a girmemesi gereken cache dosyalarını işaretle.
- [x] Bundle'ı process cwd yapmak yerine asset resolver kullan.
- [x] Asset extraction gerekiyorsa versioned, atomic ve doğrulanmış yap.
- [x] Nox app shutdown/destroy yolunu scene/app kapanışına bağla.
- [x] Crash/exception loguna Aura/Nox version ve build ID ekle.

### M5.4 iOS packaging

- [ ] Xcode project/target üretimi ekle.
- [ ] Bundle ID, display name, version/build manifestten gelsin.
- [ ] Deployment target manifestten gelsin.
- [ ] App icons ve launch screen üret.
- [ ] Supported orientations üret.
- [ ] Entitlements ve capabilities manifestten üret.
- [ ] Privacy purpose strings üret ve doğrula.
- [ ] Privacy manifest/third-party SDK signature gereksinimlerini doğrula.
- [ ] Runtime-loaded imzasız plugin dylib modelini kaldır.
- [ ] Static plugin veya signed embedded framework politikasını uygula.
- [ ] dSYM ve symbol map üret.
- [~] Development signed device app: Proof, Notes, Counter, Chat, Settings,
  Todo, Calculator, Files ve Dashboard development kimliğiyle iPhone 17 Pro'ya
  kuruldu. Betik eklenti dylib'ini `AURA_IOS_SIGN_IDENTITY` varsa imzalar.
  Uygulama entitlements, Xcode projesi ve kurulum hâlâ elle.
- [ ] `.xcarchive` üret.
- [ ] Signed IPA export et.
- [ ] TestFlight validation/upload akışını ekle.

### M5.5 iOS acceptance

- [ ] Counter/Notes/Chat/Settings simulator smoke.
- [x] Proof fiziksel iPhone smoke: açılış, Noto, PNG/JPEG, tek parmak dokunuş,
  Debug çerçevesi, Profile süre şeridi ve Release temiz hali. iPhone 17 Pro.
- [x] Counter, Notes, Chat ve Settings fiziksel iPhone smoke. 5 Kasım 2026,
  iPhone 17 Pro. Todo, Calculator, Files ve Dashboard aynı turda açıldı.
- [ ] En az bir iPad layout/rotation testi.
- [~] VoiceOver cihaz testi: Notes'ta kelime dokunuşu, kaydırma, çift dokunuşla
  düzenleme, başlık rotoru, öğe chooser, öneri çubuğu ve klavye Done görüldü.
  Dört örneğin tamamı ve özel rotor eylemi açık.
- [~] Keyboard/IME cihaz testi: iPhone 17 Pro'da Notes klavyesi açılıyor,
  Türkçe karakter yazılıp siliniyor, imleç satır sonunda ve dokunulan yerde,
  sürükleme ve uzun basış bırakılan yere kadar seçiyor, silme o aralığı
  kaldırıyor, çift dokunuş kelimeyi seçip siliyor. Chat composer mesaj
  gönderir. Composition turu yok.
- [~] Background/resume: iPhone 17 Pro'da Proof ana ekrana atılıp geri açılınca
  çizim ve dokunuş sürüyor. Control Center, bellek uyarısı ve soğuk açılış
  ölçümü yok.
- [ ] Cold/warm startup ölçümü.
- [ ] Signed IPA hash ve toolchain metadata kaydı.

### M5 çıkış kapısı

- [ ] Simulator + fiziksel iPhone yeşil.
- [ ] Signed IPA TestFlight internal testing'de çalışıyor.
- [ ] Lifecycle, touch, IME ve VoiceOver cihazda kanıtlı.

---

## M6 — Android host ve cihaz kapısı

### M6.1 Activity/process/surface lifecycle

- [~] Activity start/resume/pause/stop/destroy event'leri normalize edildi;
  create için ayrı state/event ve config-change fixture'ı bekliyor.
- [x] Surface create/change/destroy state machine uygula.
- [~] Configuration change Activity recreation'da runtime korunuyor ve JNI
  ref'i değişiyor; instrumentation fixture'ı bekliyor.
- [x] Process death snapshot'ı `with_restoration` resolver'ı ile geri yükleniyor.
  Cihaz process-death kanıtı yok.
- [x] `Choreographer` callback'ini pause/stop sırasında kaldır.
- [x] Resume'da tek callback zinciri başlat; çift scheduling'i engelle.
- [~] Memory trim seviyeleri LifecycleOwner'a gidiyor; cache tahliye
  politikası bekliyor.
- [x] Surface yokken paint/present çağrısını güvenli durdur.
- [x] App/runtime destroy ve global ref cleanup uygula.

### M6.2 JNI doğruluğu

- [x] Native thread yalnız kendi AttachCurrentThread çağrısında detach eder.
- [~] Host JNI local ref'leri callback sonunda bırakılıyor; instrumentation
  sayımı bekliyor.
- [x] Global Activity ref'ini recreation/destroy sırasında güvenli değiştir.
- [x] Host JNI çağrısı sonrası bekleyen exception temizlenir ve loglanır;
  UI thread'e zehir bırakılmaz.
- [x] Java dizisi UTF-16 kod biriminden yürür; modified UTF-8 kullanılmaz.
- [~] Gömülü NUL text-edit shell testinde var. JNI dizesi ve emoji cihaz
  fixture'ı bekliyor.
- [~] Method ID'leri global-ref mutex'iyle korunuyor; class cache politikası
  ve JNI exception kapısı bekliyor.
- [x] Callback sırasında destroyed Activity kullanımını engelle.
- [ ] JNI fuzz, ASan/HWASan ve stres testi ekle.

### M6.3 Window, input ve accessibility

- [x] API 30+ decor sistem pencerelerine sığmaz; eski API layout bayrakları
  kenardan kenara çizer. IME yüzeyi küçültmez.
- [x] Status, navigation, gesture inset ve cutout listesi metrics'e gider.
- [x] IME inset animasyonunun süre ve eğrisi metrics'e gider.
- [x] Density, fontScale, gece modu, yön ve locale configuration değişiminde
  yeniden yayınlanır.
- [x] Multi-touch/pointer history aktar.
- [ ] Predictive back entegrasyonu yap.
- [ ] TalkBack sanal node/actions/focus entegrasyonunu tamamla.
- [ ] Hardware keyboard ve trackpad testlerini ekle.
- [ ] Fold/unfold ve multi-window resize testlerini ekle.

### M6.4 Android packaging

- [ ] Raw `aapt2` preview scriptinden reproducible Gradle project üretimine geç.
- [ ] Application ID, namespace, version ve SDK seviyeleri manifestten gelsin.
- [ ] ABI split/universal package politikasını tanımla.
- [ ] Native pluginleri doğru `.so` ve ABI dizinleriyle paketle.
- [ ] Bütün native bağımlılıkları 16 KiB ELF alignment için denetle.
- [ ] 16 KiB emulator/device runtime açılışını kanıtla.
- [ ] `extractNativeLibs` ve zip alignment politikasını doğrula.
- [ ] Debug keystore ile release signing'i tamamen ayır.
- [ ] Signing secret'ını repo/log/artifact dışında tut.
- [ ] R8/ProGuard keep rules ve native sembol ayrımını ekle.
- [ ] Native debug symbols ve mapping artifact üret.
- [ ] Signed release APK üret.
- [ ] Signed release AAB üret.
- [ ] Play internal testing validation/upload akışını ekle.

### M6.5 Android acceptance

- [ ] Counter/Notes/Chat/Settings x86_64 emulator smoke.
- [ ] Aynı dört uygulama ARM64 fiziksel cihaz smoke.
- [ ] En az bir tablet ve bir foldable/config-change testi.
- [ ] TalkBack cihaz testi.
- [ ] Keyboard/IME cihaz testi.
- [ ] Background/resume/process death/memory pressure testi.
- [ ] Cold/warm startup ölçümü.
- [ ] Signed APK/AAB hash ve toolchain metadata kaydı.

### M6 çıkış kapısı

- [ ] x86_64 emulator + ARM64 fiziksel cihaz yeşil.
- [ ] Signed AAB Play internal testing'de çalışıyor.
- [ ] 16 KiB page-size runtime kanıtı var.
- [ ] Lifecycle, touch, IME ve TalkBack cihazda kanıtlı.

---

## M7 — Rendering, text, image ve performans

### M7.1 Raster fallback'i üretim kalitesine getir

- [x] Skia mobile dependency pinlerini toolchain manifestine taşı.
- [ ] Mobil Skia arşivlerini reproducible build ile üret.
- [ ] Frame başına gereksiz full-buffer malloc/free'yi kaldır.
- [ ] Skia buffer -> shell buffer -> platform bitmap çift kopyasını ölç ve azalt.
- [ ] Persistent/double/triple buffer ownership'ini tanımla.
- [ ] Surface resize sırasında eski buffer kullanımını engelle.
- [ ] Pixel format, premultiplication ve row stride'ı iki platformda test et.
- [ ] Retina/density scaling'de bulanıklık ve yanlış resample'ı engelle.
- [ ] Large image/font atlas/cache memory pressure testleri ekle.
- [ ] Raster fallback'i GPU gelince de koru.

### M7.2 GPU backend

- [ ] Metal/Vulkan/OpenGL ES/Skia Ganesh-Graphite seçeneklerini ölç.
- [ ] ADR kabul edilmeden backend seçme.
- [ ] iOS surface/context oluşturma ve resize uygula.
- [ ] Android surface/context oluşturma ve resize uygula.
- [ ] Context/device/surface loss recovery uygula.
- [ ] Render/scene katmanına backend-native type sızdırma.
- [ ] Texture/image lifetime ve dispose testleri ekle.
- [ ] GPU hata durumunda açık fallback/diagnostic üret.

### M7.3 Text shaping

- [ ] Ortak shaping backend'ini mobilde aynı API arkasında kullan.
- [ ] Font fallback zinciri ekle.
- [ ] Emoji ve color font desteği ekle.
- [ ] Unicode bidi/RTL desteği ekle.
- [x] Combining mark ve grapheme ölçümünü düzelt.
- [x] Line break/hyphenation destek matrisini yaz. Tire eklenmez; sığmayan
  kelime grapheme sınırından bölünür. Paragraf ayırıcıları LF, CR ve CRLF'dir.
- [x] Dynamic Type/fontScale ile paragraph cache invalidation yap.
- [ ] Text golden'larını backend/platform toleransıyla tasarla.

### M7.4 Performans, bellek ve enerji

- [ ] 60/90/120 Hz referans cihaz listesi belirle.
- [ ] Build/layout/paint/composite/total P50/P95/P99 ölç.
- [ ] Jank/dropped frame counter ekle.
- [ ] Frame başı allocation ve peak RSS ölç.
- [ ] GPU/CPU time ayrımını ölç.
- [ ] Cold/warm startup süresini ölç.
- [ ] Background CPU kullanımının sıfıra yakın olduğunu doğrula.
- [ ] Scroll, text-heavy, image-heavy ve animation benchmark ekle.
- [ ] 10.000 satır rotate/keyboard/resume benchmark ekle.
- [ ] Enerji/batarya profili için platform ölçüm prosedürü yaz.
- [ ] Correctness'i bozan spekülatif object pooling yapma.

### M7 çıkış kapısı

- [ ] Raster fallback stabil ve kaynak sızdırmıyor.
- [ ] Seçilen GPU backend iki platformda context-loss testinden geçiyor.
- [ ] Referans cihazlarda frame/memory/energy bütçeleri geçiyor.

---

## M8 — Mobil widget, adaptif layout ve uygulama UX'i

### M8.1 Temel mobil widget katmanı

- [x] `SafeArea` tamamla.
- [x] Keyboard-aware inset/scroll helper sağla.
- [x] Responsive constraints/breakpoint primitive'i tasarla.
- [x] Orientation değişiminde state preservation test et.
- [x] Touch target minimum boyutu için design-layer diagnostic ekle.
- [ ] Mobile scroll physics/overscroll politikasını platformdan bağımsız kur.
- [ ] Pull-to-refresh ayrı design widget/package olarak değerlendir.
- [ ] Modal/dialog bottom-sheet davranışını Overlay üzerine kur.
- [ ] Tooltip/context-menu uzun basma davranışını ekle.
- [ ] Selection handles ve edit menu tasarımını tamamla.

### M8.2 Navigation ve restoration

- [ ] Navigator'ı system back ile bütünleştir.
- [ ] Route stack serialize/restore et.
- [ ] Deep link -> route mapping API'sini ekle.
- [ ] Cold-start deep link ve running-app deep link testlerini ayır.
- [ ] Authentication redirect/pending route politikasını uygulama katmanına
  bırak.
- [ ] iPad/tablet multi-pane navigation örneği ekle.

### M8.3 Tema ve platform tercihleri

- [x] System brightness `SystemTheme` ile inherited Theme'e bağlandı.
- [ ] Dynamic color desteğini core zorunluluğu yapmadan design package'ta
  değerlendir.
- [x] Text scale, high contrast, bold text ve reduce motion headless testli.
- [ ] Platformu taklit eden Material/Cupertino default kimliği yaratma.
- [ ] Aura'nın nötr design primitives'ini mobile touch hedefleriyle tamamla.

### M8 çıkış kapısı

- [ ] Counter/Notes/Chat/Settings telefon ve tablet boyutlarında taşmıyor.
- [ ] Rotation, keyboard ve text scale state kaybı yaratmıyor.
- [ ] Navigation back/deep-link/restoration acceptance testleri geçiyor.

---

## M9 — Platform servisleri, plugin ve PlatformView

### M9.1 Platform service sınırı

- [ ] Clipboard, haptics, system chrome ve orientation için typed service API
  tasarla.
- [ ] URL açma/share gibi servisleri core yerine dar platform package olarak
  değerlendir.
- [ ] Permissions için typed request/result/status sözleşmesi tasarla.
- [ ] Permission istemeyi build veya widget lifecycle içinde otomatik yapma.
- [ ] Denied/restricted/permanently-denied durumlarını ayır.
- [ ] Kamera, konum, bildirim gibi uygulama servislerini core UI'a gömme.
- [ ] Platform service callback'lerini event queue/scheduler üzerinden döndür.

### M9.2 Plugin modeli

- [ ] Plugin metadata, ABI ve capability sözleşmesini sürümle.
- [ ] iOS pluginleri statik kayıt veya signed embedded framework yap.
- [ ] Android pluginleri ABI başına `.so` ve güvenli JNI registration yap.
- [ ] Runtime-downloaded executable code'u temel mimari yapma.
- [ ] Plugin load/unload/fail/partial-init/exception cleanup testleri yaz.
- [ ] Plugin permission ve privacy metadata'sını app manifestine birleştir.

### M9.3 PlatformView

- [ ] Logical parent ile native/render parent ilişkisini ADR'de tanımla.
- [ ] Z-order ve overlay/underlay politikasını uygula.
- [ ] Clip/transform/opacity desteğini veya açık limitleri tanımla.
- [ ] Hit-test ve pointer routing uygula.
- [ ] Focus/IME/accessibility routing uygula.
- [ ] Scroll/animation sırasında native view konumunu senkronize et.
- [ ] Background/surface loss/dispose lifecycle'ını uygula.
- [ ] Screenshot/golden ve composition performans testleri ekle.

### M9 çıkış kapısı

- [ ] Plugin modeli mağaza signing kurallarıyla uyumlu.
- [ ] PlatformView touch/focus/a11y/lifecycle kapılarını geçiyor.
- [ ] Core framework platform servislerinin uygulama-specific kapsamını
  sahiplenmiyor.

---

## M10 — Aura CLI, manifest, asset ve paketleme

### M10.1 Toolchain manifest ve doctor

- [x] Nox, Aura, Skia, SDK, NDK ve JDK alt sınırını pinle. Xcode, Gradle, Clang
  ve Zig pinlenmez; doctor tespit edilen sürümü veya eksik olduğunu yazar.
- [x] `aura doctor` komutunu ekle.
- [x] Host OS ve desteklenen target kombinasyonunu raporla.
- [x] Simulator/emulator/device envanteri raporla.
- [x] Signing identity/profile/keystore durumunu secret göstermeden raporla.
- [x] Eksik dependency için katman-doğru, uygulanabilir hata mesajı ver.

### M10.2 Proje üretimi

- [ ] `aura create --platforms ios,android` ekle.
- [ ] Reproducible Xcode project üret.
- [ ] Reproducible Gradle project üret.
- [ ] Üretilen dosyaları template version ile işaretle.
- [ ] Template upgrade/migration politikasını tanımla.
- [ ] Kullanıcı kaynaklarını regeneration sırasında ezme.

### M10.3 Çalıştırma ve build

- [ ] `aura run` device seçimi/build/install/launch/log akışını yönetsin.
- [ ] `aura build ios` static Nox app + host + renderer + assets üretsin.
- [ ] `aura build ios --archive/--ipa` ekle.
- [ ] `aura build android --format apk|aab` ekle.
- [ ] Debug/profile/release modlarını iki platformda ayır.
- [ ] Build cache key'lerini toolchain/target/config ile doğru üret.
- [ ] Build loguna secret veya kişisel yol yazma.

### M10.4 Uygulama manifesti ve assets

- [x] App ID/bundle ID.
- [x] Display name.
- [x] Version/build number.
- [x] Min iOS/Android ve target SDK.
- [ ] Supported orientations/form factors. Yön listesi doğrulanıyor; form
  factor alanı yok.
- [x] Permissions ve purpose strings.
- [x] Entitlements/capabilities.
- [ ] Icons/launch assets.
- [ ] Fonts, images, locales, shaders ve raw assets.
- [x] Network/security/backup policy.
- [ ] Deep link/app link/universal link metadata.
- [x] Asset collision, missing asset ve hash validation.
- [ ] Deterministic ordering/timestamp ve reproducible archive. Asset sırası
  hash denetiminde. IPA/AAB arşivi henüz üretilmiyor.

### M10.5 Supply chain ve artifact

- [x] Her artifact için SHA-256 üret.
- [x] SBOM üret.
- [x] Third-party lisans bildirimi üret.
- [ ] Native symbol/dSYM/mapping artifact'lerini sakla.
- [x] Signing secret'larını repo, log ve genel artifact dışında tut.
- [x] Build provenance: Aura/Nox commit, toolchain, target, backend, cihaz/OS
  ve build/run ayrımını kaydet. Cihaz modeli bu kanıtta yok.

### M10 çıkış kapısı

- [ ] Temiz makinede doctor/create/run/build akışı tekrar üretilebilir.
- [ ] Debug/release APK, AAB ve IPA aynı manifestten üretiliyor.
- [ ] Artifact hash/SBOM/lisans/symbol dosyaları mevcut.

---

## M11 — Test, CI, cihaz laboratuvarı ve kalite

### M11.1 Test katmanları

- [ ] Event normalization unit testleri.
- [ ] Queue ordering/coalescing/concurrency testleri.
- [ ] WindowMetrics/SafeArea widget testleri.
- [ ] Lifecycle/scheduler headless testleri.
- [x] Multi-pointer gesture testleri.
- [ ] IME protocol adapter testleri.
- [ ] Semantics mapping/action testleri.
- [ ] Native ownership/sanitizer testleri.
- [ ] Surface/context loss render testleri.
- [ ] Mobile golden/screenshot testleri.
- [x] Package manifest/artifact validation testleri.

### M11.2 PR CI

- [x] Desktop macOS/Windows/Linux sertifikası.
- [x] Mobile contract ve dependency-direction testleri.
- [ ] Native C/ObjC/JNI unit testleri.
- [ ] iOS simulator build + launch + Counter smoke.
- [ ] Android x86_64 emulator build + launch + Counter smoke.
- [ ] Emulator/simulator watchdog.
- [ ] Cleanup failure'ını gizlemeyen teardown.
- [ ] `|| true` ile yeşile zorlanmış kritik adım bulunmaması.

### M11.3 Nightly CI

- [ ] Counter/Notes/Chat/Settings iki platformda.
- [ ] Lifecycle/background/resume.
- [ ] Rotation/configuration/process recreation.
- [ ] Memory pressure/cache recreation.
- [ ] IME/clipboard/keyboard inset.
- [ ] VoiceOver/TalkBack otomasyonunun mümkün alt kümesi.
- [ ] Performance/jank/startup/memory benchmark.
- [ ] 16 KiB Android emulator testi.
- [ ] ASan/TSan/HWASan uygulanabilir matris.

### M11.4 Release candidate laboratuvarı

- [ ] Fiziksel iPhone.
- [ ] Fiziksel iPad.
- [ ] Android ARM64 telefon.
- [ ] Android tablet/foldable.
- [ ] En düşük desteklenen OS.
- [ ] Güncel OS.
- [ ] 60/90/120 Hz cihazlar.
- [ ] Low-memory/low-storage.
- [ ] Offline/online ve ağ geçişi.
- [ ] Cold/warm start.
- [ ] App update/migration.
- [ ] Dark/light, text scale, locale ve RTL.
- [ ] Screen reader ve switch-control.
- [ ] Signed IPA/APK/AAB gerçek kurulum/launch.

### M11.5 Kanıt formatı

- [x] Aura commit/tag.
- [x] Nox commit/tag ve embed ABI.
- [x] Toolchain manifest hash.
- [ ] Device/emulator modeli ve OS.
- [x] Renderer/backend.
- [x] Build-only ile actual-run ayrımı.
- [ ] Test sonuçları ve performans ölçümleri.
- [ ] Artifact hash.
- [ ] Known failures/unsupported matrix.

### M11 çıkış kapısı

- [ ] PR, nightly ve RC matrisleri fail-closed.
- [ ] Fiziksel cihaz kanıtı olmadan `supported` ilan edilmiyor.
- [ ] Flaky testler sessizce retry ile gizlenmiyor; oranı raporlanıyor.

---

## M12 — Güvenlik, mağaza ve destek seviyesi

### M12.1 Güvenlik ve privacy

- [ ] Minimum permission ilkesi.
- [ ] iOS purpose string/privacy manifest doğrulaması.
- [ ] Android exported component ve intent filter doğrulaması.
- [ ] Network security/ATS politikası.
- [ ] Backup/restore hassas veri politikası.
- [ ] Keychain/Keystore entegrasyonu core dışı typed service olarak planla.
- [ ] Clipboard ve screenshot hassasiyet politikasını uygulamaya bırak.
- [ ] Native dependency CVE/license taraması.
- [ ] Secret scanning ve artifact scanning.

### M12.2 Crash ve gözlemlenebilirlik

- [ ] Framework error'larında phase/window/event bağlamı ekle.
- [ ] Native crash symbolication prosedürü.
- [ ] Nox exception ile native crash ayrımı.
- [ ] Startup/lifecycle/render/input breadcrumb formatı.
- [ ] Kullanıcı verisini loglamayan redaction politikası.
- [ ] Crash-free session hedefi ve release gate'i.

### M12.3 Support tier

- [ ] Her target için `experimental/preview/supported` matrisi yayınla.
- [ ] iOS simulator/device ve Android emulator/device seviyelerini ayrı izle.
- [ ] Desteklenmeyen özellikleri açıkça belgele.
- [ ] Store gereksinimlerini her RC'de güncel resmî belgelerle doğrula.
- [ ] Nox target `supported` olmadan Aura'yı “Nox mobile stable” ilan etme.
- [ ] Breaking public API için semantic versioning uygula.

### M12 çıkış kapısı

- [ ] TestFlight internal ve Play internal yayınları doğrulandı.
- [ ] Privacy/security/symbol/SBOM/lisans kapıları geçti.
- [ ] Support matrix kanıtla uyumlu.

---

## Aura mobil Definition of Done

- [ ] Aynı Nox widget kaynakları iOS ve Android'de platform dalı olmadan
  çalışıyor.
- [ ] Widget/Element/RenderObject ve katman bağımlılık invariant'ları korunuyor.
- [ ] iOS simulator + fiziksel iPhone/iPad testli.
- [ ] Android x86_64 emulator + ARM64 telefon/tablet testli.
- [ ] Touch, multi-touch, gesture arena ve system back testli.
- [ ] Tam IME composition/selection/clipboard/keyboard inset testli.
- [ ] Safe area, cutout, density, orientation ve font scale testli.
- [ ] Lifecycle, surface loss, memory pressure ve process restoration testli.
- [ ] VoiceOver ve TalkBack role/state/action/focus testli.
- [ ] RTL, emoji, font fallback ve Dynamic Type destek matrisine uygun.
- [ ] Counter, Notes, Chat ve Settings aynı acceptance testlerini iki
  platformda geçiyor.
- [ ] Chat 10.000 satırla rotate/keyboard/resume sonrası sanal kalıyor.
- [ ] Raster fallback ve seçilen GPU backend kaynak sızdırmıyor.
- [ ] 60/90/120 Hz performans, bellek ve enerji bütçeleri geçti.
- [ ] Debug/profile/release APK, AAB ve IPA tekrar üretilebilir.
- [ ] Signed IPA TestFlight internal testing'de.
- [ ] Signed AAB Play internal testing'de.
- [ ] Android 16 KiB page-size çalışma anında kanıtlandı.
- [ ] Crash symbols, SBOM, lisanslar, provenance ve artifact hash'leri saklandı.
- [ ] Bilinen unsupported alanlar açıkça belgeli; sessiz no-op yok.
- [ ] Masaüstü sertifikası ve public API uyumluluğu bozulmadı.

## Cihazsız yürütme sırası

Fiziksel cihaz, mağaza hesabı ve CI laboratuvarı istemeyen kalan iş. Var olan
maddeler yerinde kalır; bu sıra onların yürütme sırasıdır.

1. [x] Pipeline fazının yasadışı geçişi açık tanı üretiyor.
2. [ ] Kaynak handle dispose sonrası hata: image, window ve plugin kapsanıyor.
   Font, shader ve texture handle tipi henüz yok.
3. [x] Deep-link payload'ı host kuyruğundan Binding ile Navigator'a gidiyor.
4. [x] Android surface state machine headless sözleşmesi.
5. [x] Headless semantics modeli, ADR-008 kabulünden sonra.
6. [x] M8 içinde cihaz ölçümü istemeyen yerleşim ve inset işleri. Overscroll,
   pull-to-refresh, sheet, tooltip ve selection handle maddeleri açık.
7. [x] M7 içinde GPU backend ADR'si istemeyen text/image sözleşmeleri.
   Grapheme satır kırma, satır kırılım matrisi, paragraph cache ve logical
   image boyutu testli. Bidi, renkli emoji, font zinciri, raster ve GPU açık.
8. [x] M10 paketleme ve M0 CI kapıları. Manifest, doctor, hash, SBOM, kaynak
   taraması ve embed ABI ayrımı testli. Kaynak `main`'de. `aura create`/`run`,
   imzalı IPA/AAB ve dSYM açık.
9. [x] Process-death route snapshot'ı Navigator resolver'ı ile geri yükleniyor.
   Cihaz process-death kanıtı açık.
10. [x] iOS trait ve Android class/click bağları headless sözleşmede. Increment
    olayı frame'e gider. Set-text host bağı, özel rotor, focus kararlılığının
    cihaz kanıtı ve VoiceOver/TalkBack cihaz turu açık.

## İlk aktif çalışma paketi

Bu hedef şu sırayla başlar:

1. [x] M0.1 kaynak durumu `main` commit'ine alındı. Ayrı worktree yok.
2. [x] M0.3 katman/FFI contract testlerini sertleştir.
3. [x] ADR-MOB-AURA-003 event queue/ABI kararını yaz.
4. [x] ADR-MOB-AURA-005 WindowMetrics/safe-area kararını yaz.
5. [x] M1 event ve metrics headless acceptance fixture'larını ekle.

İlk paketin kaynak kapısı kapandı. Sıradaki uygulama M5.4 Xcode hedefi,
entitlements, ikon, açılış ekranı, gizlilik bildirimi, dSYM ve imzalı IPA'dır.
Android cihaz turu bu kapıdan sonra planlanır. TestFlight yüklemesi hesap
bekler.
