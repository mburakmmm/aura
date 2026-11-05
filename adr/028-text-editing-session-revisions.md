# ADR-028 TextEditingValue ve sürümlü IME session

## Context

Aura controller bugün committed text, cursor/anchor ve composition string'ini
ayrı mutable alanlarda tutar. Native IME callback'leri hangi focus session'ına
ve hangi state revision'ına ait olduğunu taşımaz. Focus değişimi, background,
rotation veya gecikmiş callback eski composition'ın yeni field'a uygulanmasına
yol açabilir.

Nox `str` uzunluğu ve slicing'i Unicode codepoint semantiğindedir; UTF-8 byte
offset'i değildir. Ancak stdlib grapheme cluster segmentasyonu sunmaz. Bu eksik
dil değişikliği gerektirmez; Aura text katmanında UAX #29 tabanlı boundary
yardımcısı uygulanacaktır.

## Decision

`TextEditingValue` şu immutable snapshot alanlarını taşır:

- editing buffer `text`,
- yönlü `TextSelection(base_offset, extent_offset)`,
- `TextRange` composing aralığı; composition yoksa `(-1, -1)`,
- monotonik `revision`,
- focus ömrünü ayıran monotonik `generation`.

Controller committed model text'ini korur. Session editing buffer'ı committed
text snapshot'ından başlar. Aktif composition bu buffer içinde gerçek bir
range olarak tutulur; `setComposingRegion` mevcut aralığı işaretleyebilir ve
`setComposingText` yalnız o aralığı değiştirir. Finish/commit gelmeden
controller'ın committed `text` alanına yazılmaz; session close unfinished
buffer'ı deterministik olarak geri alır.

Her focus text session'ı generation'ı artırır. Framework'ten native host'a
gönderilen her editing state generation/revision içerir. Native adapter aynı
generation içinde kendi event revision'ını monotonik artırır. Nox yalnız aktif
generation ile eşleşen ve son kabul edilenden büyük revision'ı uygular. Eski,
tekrarlanan veya kapatılmış session callback'i deterministic olarak reddedilir.

Typed `TEXT_EDIT` event operation'ları commit, composing update/finish,
selection, surrounding-delete, range replacement ve editor action olarak
ayrılır. iOS Return, Android `performEditorAction` ve fiziksel Enter aynı
`PERFORM_ACTION` yoluna normalize edilir. Newline action controller'da metin
ekler; diğer action'lar field callback'ine gider. Legacy
`IME_INSERT/DELETE/MARK` desktop compatibility için geçiş döneminde kalır.

Offset'ler ABI boyunca UTF-8 byte değil Unicode codepoint indeksidir. Android
UTF-16 ve iOS `NSRange` adapter sınırında codepoint'e dönüştürülür. Grapheme
cursor/backspace bu codepoint contract üzerinde Aura boundary helper'ını
kullanır.

Selection endpoint'leri extended grapheme sınırına oturtulur. Cursor ve
backspace aynı sınırları kullanır. Word selection; Unicode alphanumeric,
whitespace ve symbol/punctuation olmak üzere üç deterministik grapheme sınıfı
üzerinde genişler. Line selection CR/LF/CRLF ayırıcısını seçime katmaz.

Aktif text field caret rectangle ve editable bounds'u layout tamamlandıktan
sonra, paint'ten önce host'a logical pixel koordinatlarıyla yollar. Native
adapter bu geometriyi IME candidate/cursor API'sine dönüştürür.

`TextInputConfiguration` text/email/URL/phone/number type'ını; IME action,
multiline, secure, autocorrect, capitalization ve ordered autofill hint'lerini
typed alanlarla taşır. Host yalnız aktif field config'i değiştiğinde native
input traits'i günceller; Widget katmanı UIKit/Android tipi görmez.

iOS adapter pozisyonları UIKit sözleşmesine uygun olarak UTF-16 tutar ve ABI
sınırında codepoint'e çevirir. Native adapter, UIKit'in senkron sorgularına
cevap verebilmek için local editing snapshot'ını önce optimistic günceller;
framework echo'su generation/revision ile uzlaştırılır. `UITextInputDelegate`
will/did bildirimleri mutation'ı çevreler ve aynı echo ikinci kez bildirim
üretmez.

Android adapter gerçek `Editable` snapshot'ını IME'ye açar. Nested batch edit
boyunca local state senkron kalır, typed platform event'leri sırayla biriktirilir
ve yalnız dıştaki `endBatchEdit` ile Nox kuyruğuna aktarılır. Extracted-text
monitor ve cursor-anchor monitor aynı snapshot/gönderilen caret geometrisinden
beslenir; adapter metin ölçümü icat etmez.

## Alternatives

Callback'i yalnız focused field'a uygulamak stale event'i ayırt etmez. UTF-8
byte offset taşımak Nox slicing ve platform UTF-16 aralıklarıyla uyuşmaz.
Composition'ı doğrudan committed text'e yazmak cancel davranışını bozar.
Compiler'a grapheme primitive eklemek framework problemini dil değişikliğine
çevirir.

## Risks

Android/iOS offset dönüşümleri emoji ve birleşik grapheme fixture'ları olmadan
hatalı olabilir. Revision wrap pratikte ulaşılamaz olsa da negatif değerler
geçersiz sayılır. Session sync reentrant native callback üretmemelidir.

## Consequences

Rotation/focus değişimindeki gecikmiş IME callback'leri güvenle düşürülür.
Editing state headless test edilebilir ve tam UITextInput/InputConnection
adapter'ları aynı contract'a bağlanabilir.

## Migration ve rollback

Controller'ın mevcut `text/cursor/anchor/composing` alanları compatibility için
korunur ve `editing_value()` snapshot'ını üretir. Typed event additive'dir.
Sorunda platform adapter typed callback'i kapatıp legacy event yoluna dönebilir.
