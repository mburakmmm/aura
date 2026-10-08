# ADR-007 Sahne ve katman

## Context

Çizim doğrudan platform API'sine yapılmamalıdır. Paint sonucu bir katman ağacı üretmeli, sahne bu ağacı bir `Canvas`'a sunmalıdır. Kaydırmalı arayüz için ileride `RepaintBoundary` gerekir; ilk sayaç tek bir resim katmanı ile doğrulanabilir.

## Decision

`PictureLayer` çizim komutlarını tutar. Komut türleri temizle, dikdörtgen, metin, opaklık açma ve opaklık kapamadır. `PaintingContext` ofset yığını taşır ve opaklığı komut olarak kaydeder. `Scene.submit` komutları `Canvas`'a yeniden oynatır. `RecordingCanvas` aynı komutları testte sayar. Opaklık, platform tuvalinde grafik durumunu kaydedip alfa uygulayarak basılır. Clip, texture ve platform view katmanları sonraki dilimlerdedir. Transform ötelemesi ayrı bir komut değildir; mevcut ofset yığınını kullanır.

Üst widget yeni bir ağaç ürettiğinde aynı metinli `Text` yapısal olarak boyamayı kirletmez. Böylece sayaç başlığı, bağımlılığı yokken paint kuyruğuna girmez. Paint yine de ebeveyn resmi yeniden basılırsa çocuğun `paint` metodunu çağırabilir; kirlenme `paint_mark_count` ile ölçülür.

## Alternatives

Her düğümü anında CoreGraphics'e basmak katman önbelleğini imkansız kılardı. Tam katman ağacını ilk sayaçta kurmak kullanılmayan clip ve opacity türlerini taşırdı.

## Consequences

`PipelineOwner.flush_paint` kökü bir `PictureLayer` içine boyar, sahneyi sunar ve `present` çağırır. Anlamsal ağaç aynı turun sonunda render ağacından toplanır.

## Migration

Yeni bir katman türü `Scene` oynatıcısına bir kol ekler. Widget kodu katman sınıfını tanımaz.
