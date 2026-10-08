# ADR-013 Kabuk soyutlaması

## Context

ADR-005 ikinci platformu aynı Nox fonksiyon adlarıyla tarif eder. Pencere, olay, IME, pano, imleç, dosya bırakma ve erişilebilirlik boyamadan ayrıdır. Widget katmanında platform koşulu yazılmaz.

## Decision

Üç kabuk aynı C sembol setini dışa aktarır ve `native/libaura_host.dylib` adıyla üretilir. Nox kaynakları tek yolu bağlar. Sonek tarihseldir.

- macOS: Cocoa penceresi, CoreVideo vsync, AX.
- Windows: Win32, `WM_CHAR` metin commit’i, UI Automation.
- Linux: tek kütüphane. `WAYLAND_DISPLAY` bağlanırsa Wayland, yoksa X11. `AURA_LINUX_SHELL` C tarafında seçimi zorlar. AT-SPI aynı alanları yayınlar.

Olay numaraları ortaktır. Karakter, sanal tuş kodundan üretilmez. Erişilebilirlik eylemi kuyruğa yazılır. Animasyon kare damgasını kullanır.

## Alternatives

Her kabuk için ayrı Nox `extern` yolu, uygulama koduna `if platform` dağıtırdı. Wayland ve X11’i iki kütüphane yapmak yükleme seçimini Nox’a taşırdı.

## Consequences

Günlük geliştirme macOS kabuğunda koşar. Windows ve Linux aynı Nox testini kendi kabuğunda çalıştırır. En fazla 32 pencere kuralı durur. Kapatma idempotenttir.

## Migration

`aura/platform/bridge.nox` sembolleri sarar. `aura/platform/macos.nox` ikinci bir `extern` bloğu tutmaz; bridge adlarını yeniden dışa aktarır.
