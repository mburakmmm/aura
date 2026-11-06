# Göreli `extern` yolu

`noxc`, `from "..."` değerini clang’e olduğu gibi verir. Clang yolu çalışma dizinine göre açar.

Aura’da bu dizge `aura/platform/bridge.nox` içindedir:

```nox
extern def aura_window_create(...) -> int from "native/libaura_host.dylib"
```

Kütüphane `native/libaura_host.dylib` altındadır. `./scripts/build_host.sh` üretir. Depoya girmez.

## Belirti

Depo kökünden bağlanır:

```sh
noxc run examples/dashboard/main.nox
```

Örnek klasöründen bağlanmaz:

```sh
cd examples/dashboard
noxc run main.nox
```

```text
clang: error: no such file or directory: 'native/libaura_host.dylib'
```

Hata bağlama anındadır. Bağlama bitince ikili, dylib’in kurulum adını mutlak yol olarak taşır.

## İstenen davranış

`from` değeri bir yolsa (`/` içeriyor ya da `.o` / `.a` ile bitiyor) ve dosya çalışma dizininde yoksa, üst dizinlerde ara. `nox.json` bulunan proje kökündeki dosyayı clang’e mutlak yolla ver.

Dosya hiçbir üst dizinde yoksa bugünkü göreli yolu bırak.

`-lsqlite3` gibi kütüphane adlarına dokunma. `zig-out/lib/noxrt.o` sentinel’i aynı kalsın.

Yer: `compiler/main.zig` içindeki `appendExternLinkArgs`.

Aura’daki `from` dizgesini değiştirme.
