# ADR-008 Metin şekillendirme

## Context

Metin şekillendirme sıfırdan yazılmaz. İlk sürüm tek paragraf, UTF-8, aile, boyut, ağırlık, renk, temel kaydırma ve hizalama ister. Zengin metin, seçim, bidi, emoji ve yedek fontlar sonraki dilimdedir.

## Decision

Şekil ve ölçüm Core Text'tedir. Nox, `measure_width` ve `measure_height` ile satır kırar. `Paragraph` satır listesini, genişliği ve yüksekliği üretir. `RenderParagraph` bu ölçüyü kısıta sıkıştırır ve satırları tuvale basar. Harf şekillendirme elle yapılmaz.

`Text` widget'ı değer, punto, ağırlık veya renk değişmediyse yerleşimi ve boyamayı kirletmez.

## Alternatives

Kendi shaping algoritması Unicode'un tamamını yanlış ölçerdi. Skia paragraph'ı ilk backend platform tuvali olduğu için Core Text ile çift motor olurdu.

## Consequences

Metin ölçümü dylib ister. Penceresiz yerleşim testleri metin widget'ı kurmaz. Satır, boşlukta kırılır. Sığmayan kelime grapheme sınırından bölünür; birleştirici im ve emoji kümesi ortadan kesilmez. Tire eklenmez. Paragraf ayırıcıları LF, CR ve CRLF'dir. Ayırıcı satır metnine girmez.

`RenderParagraph` aynı metin, punto, ağırlık, aile ve en fazla genişlik için ölçümü saklar. Bunlardan biri değişince, Dynamic Type ve `fontScale` dahil, önbellek düşer ve yeniden ölçülür. Bidi ve renkli emoji fontu bu sözleşmenin dışındadır.

Görüntü yerleşimi piksel genişliğini `device_pixel_ratio` değerine böler. Metrics yoksa oran 1'dir. Bu, GPU örneklemesinden ayrı bir logical boyut sözleşmesidir.

## Migration

Shaping backend'i değişirse `measure_width`, `measure_height` ve tuvalin metin komutu aynı kalır. `Paragraph` Nox tarafındaki satır kırılımını ölçüm fonksiyonlarının arkasına saklar.
