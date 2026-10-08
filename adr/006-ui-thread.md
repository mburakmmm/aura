# ADR-006 UI iş parçacığı

## Context

Nox FFI geri çağrıları daha sonra, olay döngüsünden veya bir closure üzerinden çağrılamaz. AppKit bu yüzden Nox'a geri dönemez. UI ağacı birden fazla worker'a kopyalanan modül durumunda duramaz.

## Decision

Tek mantıksal UI iş parçacığı vardır. `poll_events`, Cocoa kuyruğunu `nextEventMatchingMask` ile senkron boşaltır. VSync beklemesi CoreVideo `CVDisplayLink` içindedir; callback yalnız bir semaforu işaretler ve Nox'a düz bir dönüş olur. Ana run loop bekleme sırasında ilerlemediği için görüntü bağlantısı ana iş parçacığına bağlanmaz. API macOS 15'te kullanımdan kaldırılmıştır; yerine geçen `NSView.displayLink` ana run loop'a bağlı olduğu için bu bekleme modelinde kullanılamaz.

`FrameScheduler` fazları idle, input, build, layout, paint, composite sırasındadır. Paint sırasında gelen yerleşim isteği sonraki kareye yazılır. Element ağacı worker'a bırakılmaz. Kimlik sayaçları yalnız UI iş parçacığından artar.

## Alternatives

AppKit'in Nox fonksiyonunu hedef olarak tutması FFI sözleşmesine uymazdı. Ayrı bir Nox OS olay döngüsü icat etmek platform pompasını ikinci kez yazardı.

## Consequences

`run_app` tek akışta kalır: bir kare, sonra `wait_frame` ve yeniden kare, pencere kapanana kadar. Testler pencere açmadan `FramePump` ve `RecordingCanvas` kullanır. Gizli pencere testleri tutamaç ömrünü doğrular.

## Migration

FFI ileride gecikmeli callback kabul ederse bekleme, run loop'u kilitlemeyen bir görüntü bağlantısına taşınabilir. O güne kadar semafor sözleşmesi korunur.
