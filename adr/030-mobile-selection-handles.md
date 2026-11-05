# ADR-030 Mobil seçim tutamaçları ve düzenleme menüsü

## Context

Metin seçimi bugün `TextEditingValue` aralığı ve alanın kendi çizimiyle
taşınır. iOS ve Android'in yüzen tutamaçları ile kes/kopyala/yapıştır
araç çubuğu platforma özel katmanlardır. Bunları çekirdek metin sözleşmesine
gömmek Widget, Element ve RenderObject sınırını platform tipi taşımaya iter.

## Decision

Seçim tutamaçları ve mobil düzenleme menüsü bu fazda uygulanmaz. Seçim,
imleç ve pano eylemleri platformdan bağımsız controller sözleşmesinde kalır.
Tutamaçlar sonraki bir design katmanı overlay'i olarak, metin alanı
geometri snapshot'ının üstüne kurulacaktır. Host bu overlay için ayrı bir
native tutamaç ağacı kurmaz.

## Alternatives

`UITextInput` ve `InputConnection` tutamaçlarını framework widget'ı gibi
içeri almak daha kısa görünürdü. Bu, metin katmanına UIKit ve Android view
tipleri sokar ve masaüstü seçimini de o tiplere bağlardı. Tutamaçları hemen
RenderObject içine boyamak da platform jestlerini ve erişilebilirlik
eylemlerini aynı nesneye yığardı.

## Consequences

Kopyala, kes, yapıştır ve seçim aralığı headless testle doğrulanır. Görünür
tutamaç, büyüteç ve platform araç çubuğu cihaz kabulüne kadar açık kalır.
Geometri snapshot'ı o işin girdisidir; bu ADR onu değiştirmez.

## Migration

Mevcut seçim boyası ve pano eylemleri yerinde kalır. Tutamaç overlay'i
eklendiğinde yeni bir render nesnesi ve hit-test yolu açılır; editing value
revizyon sözleşmesi değişmez. Geri alma, overlay'i mount etmemektir.
