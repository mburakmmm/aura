# ADR-002 Kısıt tabanlı yerleşim

## Context

Aura, ebeveynin çocuğa kısıt verdiği, çocuğun boyut döndürdüğü ve ebeveynin çocuğu konumlandırdığı bir yerleşim ister. Her karede tüm render ağacını yeniden ölçmek hedeflenen 120 Hz bütçesine uymaz.

## Decision

`Constraints` minimum ve maksimum genişlik ile yükseklik taşır. `RenderBox.layout` kısıt değiştiyse veya nesne kirliyse `perform_layout` çalıştırır. `mark_needs_layout` relayout boundary bulur: kök, sıkı kısıt veya `force_boundary`. İki boyutu da verilmiş `SizedBox` ve `RenderView` boundary'dir.

Paint sırasında `mark_needs_layout` bir sonraki kareye yazılır. Paint sırasında `layout` çağrısı `layout called during paint` hatasıdır. Negatif veya ters kısıt `invalid constraints` hatasıdır. `RenderFlex` ana eksende sınırsızken `Expanded` görürse hata verir; taşma alanı `overflow` üzerinde tutulur ve bir kez `RenderFlex overflow` olarak yazılır.

## Alternatives

Her değişiklikte tam ağaç yerleşimi doğruydu fakat kaydırmalı ve metin ağırlıklı ekranlarda bütçeyi aşardı. CSS tarzı serbest boyut modeli kısıtın yönünü belirsiz bırakırdı.

## Consequences

Row ve Column aynı `RenderFlex` uygulamasını kullanır. Eksen, ana hizalama, çapraz hizalama, ana eksen boyutu ve aralık widget'tan `set_flex_style` ile geçer. `Expanded` sıkı, `Flexible` gevşek flex'tir.

## Migration

Yerleşim protokolü değişirse bu ADR güncellenmez; yeni ADR yazılır. Mevcut testler 300×400 kolon, aralık, taşma ve Expanded payını kilitler.
