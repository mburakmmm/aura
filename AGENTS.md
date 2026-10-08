AGENTS.md — Aura UI Framework

Bu dosya Aura üzerinde çalışan tüm AI kodlama ajanları için bağlayıcı mimari ve geliştirme sözleşmesidir.

Aura, Nox 2.0 üzerinde geliştirilen, Flutter’dan ilham alan fakat Flutter mimarisini birebir kopyalamayan, deklaratif, performans odaklı, native masaüstü ve ileride mobil uygulamalar üretmeyi hedefleyen bir UI framework’üdür.

Bu dosyadaki kurallar tasarım tercihleri değil, proje invariant’larıdır.

Bir ajan herhangi bir implementasyona başlamadan önce:

1. Bu dosyayı tamamen okumalıdır.
2. İlgili mevcut Aura kodunu incelemelidir.
3. Nox dilinin mevcut özellikleriyle çözüm aramalıdır.
4. Yeni Nox dil özelliği istemeden önce mevcut API ile çözülemeyeceğini kanıtlamalıdır.
5. Mimari sınırları değiştirecek bir karar gerekiyorsa kod yazmadan önce ADR oluşturmalıdır.

⸻

1. Proje Vizyonu

Aura’nın amacı:

Nox için Flutter seviyesinde ergonomik, fakat daha küçük çekirdeğe, daha deterministik lifecycle’a, açık performans modeline ve platformdan bağımsız rendering mimarisine sahip bir deklaratif UI framework oluşturmak.

Aura uygulama kodu şu hissi vermelidir:

from aura.app import run_app
from aura.widgets import StatelessWidget, BuildContext
from aura.layout import Column
from aura.text import Text
from aura.controls import Button
class CounterPage(StatelessWidget):
    def build(self: CounterPage, context: BuildContext) -> Widget:
        return Column([
            Text("Aura"),
            Button("Continue", handle_continue)
        ])
def handle_continue() -> None:
    print("clicked")
run_app(CounterPage())

ÖNEMLİ:

* Nox’ta doğrulanmamış syntax kullanılmaz.
* Lambda syntax varsayılmaz.
* Dinamik Any modeli yaratılmaz.
* Reflection framework’ün temel mekanizması yapılmaz.
* Kullanıcı explicit ownership görmez.
* Flutter kaynak kodu kopyalanmaz.
* Aura kendi API ve runtime tasarımına sahip olmalıdır.

⸻

2. Temel Felsefe

Aura’nın ana ilkeleri:

Declarative API
      +
Stable retained runtime
      +
Incremental reconciliation
      +
Constraint-based layout
      +
Retained render tree
      +
Platform-independent scene model
      +
Native platform backends

Aura üç şeyi kesinlikle birbirinden ayırır:

Widget
  ≠
Element
  ≠
RenderObject

Bunların görevleri karıştırılamaz.

⸻

3. Mimari Genel Görünüm

Ana pipeline:

Application
    │
    ▼
Widget Tree
    │
    ▼
Element Tree
    │
    ▼
Render Tree
    │
    ▼
Layout
    │
    ▼
Paint
    │
    ▼
Scene
    │
    ▼
Compositor
    │
    ▼
Platform Renderer

Input ters yönde akar:

Platform Event
     │
     ▼
Event Normalization
     │
     ▼
Hit Testing
     │
     ▼
Gesture Arena
     │
     ▼
Element / Controller
     │
     ▼
State mutation
     │
     ▼
Dirty Element
     │
     ▼
Rebuild

⸻

4. Katmanlar

Aura şu katmanlardan oluşur.

aura_core
aura_foundation
aura_widgets
aura_rendering
aura_graphics
aura_input
aura_animation
aura_semantics
aura_platform
aura_devtools

Bağımlılık yönü daima yukarıdan aşağıdır.

Örnek:

widgets
   ↓
rendering
   ↓
graphics
   ↓
platform

graphics hiçbir zaman widgets import etmez.

platform hiçbir zaman widgets import etmez.

⸻

5. Repo Yapısı

Önerilen yapı:

/aura
  /core
    object.nox
    key.nox
    lifecycle.nox
    diagnostics.nox
  /foundation
    geometry.nox
    color.nox
    constraints.nox
    edge_insets.nox
    alignment.nox
    matrix.nox
  /widgets
    widget.nox
    element.nox
    stateless.nox
    stateful.nox
    inherited.nox
    builder.nox
    app.nox
  /layout
    flex.nox
    row.nox
    column.nox
    stack.nox
    padding.nox
    align.nox
    sized_box.nox
    center.nox
  /rendering
    render_object.nox
    render_box.nox
    pipeline_owner.nox
    hit_test.nox
    layer.nox
  /graphics
    canvas.nox
    paint.nox
    path.nox
    image.nox
    text_layout.nox
    scene.nox
  /controls
    button.nox
    toggle.nox
    checkbox.nox
    slider.nox
    text_field.nox
  /text
    text.nox
    text_style.nox
    paragraph.nox
  /input
    pointer_event.nox
    keyboard_event.nox
    gesture.nox
    gesture_arena.nox
    focus.nox
  /animation
    animation.nox
    controller.nox
    curve.nox
    tween.nox
    ticker.nox
  /semantics
    semantics_node.nox
    semantics_owner.nox
  /platform
    platform.nox
    /macos
    /linux
    /windows
  /runtime
    binding.nox
    scheduler.nox
    frame_scheduler.nox
  /devtools
    inspector.nox
    performance.nox
    debug_overlay.nox
/examples
/tests
/benchmarks
/docs
/adr

Platform native kodu gerekirse ayrı:

/native
  /macos
  /linux
  /windows

altında tutulabilir.

⸻

6. Değişmez Mimari Kurallar

6.1 Widget immutable configuration’dır

Widget:

* UI state’i değildir.
* Layout objesi değildir.
* Render objesi değildir.
* Platform handle tutmaz.
* Mutable lifecycle sahibi değildir.

Widget yalnızca konfigürasyondur.

Örnek:

class Text(Widget):
    def __init__(
        self: Text,
        value: str,
        style: TextStyle
    ) -> None:
        self.value = value
        self.style = style

Bir Widget frame’ler arasında yeniden üretilebilir.

Widget oluşturmanın ucuz olması gerekir.

⸻

7. Element Tree

Element uzun ömürlü runtime identity’dir.

Görevleri:

Widget identity
lifecycle
parent/child relationship
dirty state
reconciliation
BuildContext
state ownership
render-object connection

Widget yeniden oluşturulabilir.

Element mümkün olduğunca korunur.

Widget(old)
    │
update
    ▼
Element
    ▲
update
    │
Widget(new)

Bu ayrım Aura’nın temelidir.

⸻

8. RenderObject Tree

RenderObject:

layout
paint
hit test
geometry
render invalidation

işlerinden sorumludur.

RenderObject içinde:

* business state bulunmaz.
* navigation logic bulunmaz.
* networking bulunmaz.
* platform-specific window API bulunmaz.

Önerilen temel:

class RenderObject:
    def mark_needs_layout(self: RenderObject) -> None:
        pass
    def mark_needs_paint(self: RenderObject) -> None:
        pass
    def layout(self: RenderObject, constraints: Constraints) -> None:
        pass
    def paint(self: RenderObject, context: PaintingContext) -> None:
        pass

⸻

9. Widget → Element → RenderObject

Widget tipleri iki ana kategoriye ayrılır:

ComponentWidget
RenderObjectWidget

ComponentWidget kendi RenderObject’unu üretmez.

Örnek:

CounterScreen
Theme
Builder
Padding convenience wrapper

RenderObjectWidget doğrudan rendering katmanına bağlanır.

Örnek:

Text
Flex
Stack
Padding
SizedBox
Image

⸻

10. StatelessWidget

Temel API:

class StatelessWidget(Widget):
    def build(
        self: StatelessWidget,
        context: BuildContext
    ) -> Widget:
        raise NotImplementedError()

build():

* pure’a yakın tutulmalıdır.
* I/O yapmamalıdır.
* doğrudan platform API çağırmamalıdır.
* pahalı blocking işlem yapmamalıdır.
* child Widget üretmelidir.

⸻

11. StatefulWidget

Flutter’daki StatefulWidget fikri korunabilir ancak lifecycle sadeleştirilmelidir.

StatefulWidget
      │
      ▼
StatefulElement
      │
      ▼
State

Örnek:

class CounterWidget(StatefulWidget):
    def create_state(self: CounterWidget) -> CounterState:
        return CounterState()
class CounterState(State[CounterWidget]):
    def __init__(self: CounterState) -> None:
        self.count = 0
    def increment(self: CounterState) -> None:
        self.count = self.count + 1
        self.invalidate()
    def build(
        self: CounterState,
        context: BuildContext
    ) -> Widget:
        return Text(str(self.count))

invalidate():

State
 ↓
owning Element dirty
 ↓
next build phase
 ↓
rebuild

Anında recursive rebuild yapılmaz.

⸻

12. Flutter’dan Farkımız: Mutation sırasında rebuild yok

Aura’da:

state mutation
    ↓
dirty mark
    ↓
frame scheduler
    ↓
batched rebuild

olmalıdır.

Bu sayede:

state.set()
state.set()
state.set()

aynı frame’de üç rebuild yaratmaz.

Tek rebuild olur.

⸻

13. Fine-Grained Reactive State

StatefulWidget tek state modeli olmayacaktır.

Aura ikinci katman olarak lightweight reactive primitives sunacaktır.

Önerilen:

Signal[T]
Computed[T]
Effect

Ama bunlar Widget tree’nin yerine geçmez.

Örnek:

count: Signal[int] = Signal[int](0)

API hedefi:

Signal.set()
Signal.get()
Computed.get()

Reactive graph şu amaçla kullanılır:

state dependency tracking
     ↓
yalnız ilgili element dirty

Global “her şey rebuild” modeli yapılmaz.

⸻

14. Signal Kuralları

Signal:

* thread-safe varsayılmaz.
* UI thread’e ait olmalıdır.
* değişiklik aynı değerse notification üretmeyebilir.
* dependent Element’lar weak şekilde izlenmelidir.
* Element dispose edildiğinde subscription silinmelidir.

Circular Computed dependency runtime diagnostic üretmelidir.

⸻

15. BuildContext

BuildContext ayrı bir ağır obje olmamalıdır.

Element’in dar view’ı olarak düşünülür.

Görevleri:

ancestor lookup
theme lookup
media query
localization
dependency registration
navigator lookup
focus lookup

Widget doğrudan Element pointer’ı manipüle etmez.

⸻

16. Key Sistemi

Aura’da ilk sürümden itibaren Key vardır.

Key
ValueKey[T]
ObjectKey
GlobalKey

Ancak GlobalKey mümkün olduğunca nadir kullanım içindir.

Reconciliation identity:

same runtime type
+
compatible key

ile belirlenir.

Key yoksa sibling pozisyonu kullanılır.

⸻

17. Reconciliation Algoritması

Aura reconciliation Flutter kadar genel ama daha açık olmalıdır.

Eski children:

A B C D

Yeni:

A C E D

Algoritma:

prefix scan
suffix scan
keyed middle reconciliation
unkeyed positional fallback

Hedef:

O(n)

ortalama davranış.

Blind quadratic child search yasaktır.

⸻

18. Dirty Element Queue

Element invalidate() edildiğinde:

dirty_elements

kuyruğuna eklenir.

Aynı Element aynı frame’de yalnız bir kez bulunabilir.

Flush sırası:

parent depth ascending

olmalıdır.

Parent rebuild child’ı yok edebileceğinden child önce rebuild edilmemelidir.

⸻

19. Constraint-Based Layout

Aura Flutter’ın constraint modelini kullanır:

Constraints go down. Sizes go up. Parents position children.

Temel:

class Constraints:
    min_width: float
    max_width: float
    min_height: float
    max_height: float

RenderBox:

class RenderBox(RenderObject):
    def perform_layout(self: RenderBox) -> None:
        pass

Sonuç:

parent constraints
       ↓
child
       ↓
child size
       ↓
parent positioning

⸻

20. Geometry

Foundation primitive’leri:

Offset
Size
Rect
Radius
RRect
Insets
Alignment
Matrix4
Constraints
BoxConstraints

Bunlar mümkün olduğunca immutable value-object semantiğinde olmalıdır.

⸻

21. Flex Layout

Row ve Column aynı RenderFlex implementasyonunu kullanmalıdır.

Axis.horizontal
Axis.vertical

Temel property’ler:

main_axis_alignment
cross_axis_alignment
main_axis_size
spacing
children

Sonradan:

Expanded
Flexible
Spacer

eklenir.

İlk implementasyonda CSS Flexbox’ın tüm karmaşıklığı kopyalanmaz.

⸻

22. Stack Layout

Stack:

normal children
positioned children

destekler.

Position bilgisi Widget’da değil parent-data sistemi üzerinden RenderObject’a geçirilmelidir.

ParentDataWidget
      ↓
StackParentData

Bu desen yalnız gerektiğinde uygulanmalıdır.

⸻

23. Render Pipeline

Her frame:

1. process input
2. apply state mutations
3. rebuild dirty elements
4. layout dirty render objects
5. paint dirty render objects
6. compose layers
7. submit scene

PipelineOwner merkezi coordinator olur.

⸻

24. Layout Dirty Propagation

mark_needs_layout():

RenderObject
     ↓
relayout boundary bulunur
     ↓
PipelineOwner layout queue

Tüm render tree her değişiklikte layout edilmez.

Relayout boundary temel performans mekanizmasıdır.

⸻

25. Paint Dirty Propagation

Benzer şekilde:

mark_needs_paint()

yalnız gerekli paint subtree’sini dirty yapmalıdır.

Layout değişikliği paint gerektirebilir.

Paint değişikliği layout gerektirmemelidir.

⸻

26. RepaintBoundary

Aura ilk stabil sürümden önce RepaintBoundary desteklemelidir.

Amaç:

static expensive subtree
        │
        ├── cached layer
        │
        └── parent değişse bile repaint etme

Bu, scrollable UI için kritiktir.

⸻

27. Scene Graph

Canvas doğrudan platform API’sine çizmemelidir.

Paint sonucu:

Layer Tree

üretir.

Örnek layer tipleri:

TransformLayer
ClipRectLayer
OpacityLayer
PictureLayer
TextureLayer
PlatformViewLayer

Layer Tree:

Render Tree
   ↓ paint
Layer Tree
   ↓
Scene
   ↓
Compositor

⸻

28. Graphics Backend

İlk hedef:

Aura graphics API
      ↓
Skia veya başka backend

olabilir.

Ancak Aura Core hiçbir yerde doğrudan Skia type kullanmaz.

Backend abstraction:

GraphicsDevice
Surface
Canvas
Texture
Image
Font
Paragraph
Scene

olarak tutulur.

Backend ileride değiştirilebilir olmalıdır.

⸻

29. Renderer Stratejisi

İlk prototipte software renderer yazılmamalıdır.

Aşağıdaki seçenekler araştırılır:

Skia
Skia C API wrapper
wgpu/native
SDL + GPU backend
platform-native canvas

İlk amaç framework mimarisini doğrulamaktır, renderer icat etmek değil.

Renderer ADR olmadan seçilmez.

⸻

30. Text Rendering

Text rendering UI framework’ün zor kısmıdır.

İlk sürümden şu ayrım korunmalıdır:

Text Widget
    ↓
Paragraph
    ↓
TextLayoutEngine
    ↓
GlyphRun
    ↓
Canvas

İlk sürüm:

single paragraph
UTF-8
font family
size
weight
color
basic wrapping
alignment

destekler.

Daha sonra:

rich text
selection
bidi
complex shaping
emoji
fallback fonts

eklenir.

Text shaping için sıfırdan algoritma yazılmaz.

HarfBuzz benzeri olgun backend tercih edilir.

⸻

31. Event Loop

Aura kendi ayrı OS event loop’unu icat etmemelidir.

Nox async runtime + platform message pump birlikte koordine edilir.

Ana thread:

platform event pump
       +
Aura frame scheduler
       +
Nox tasks

ile yaşar.

UI tree yalnız UI thread’den mutate edilir.

⸻

32. Frame Scheduler

FrameScheduler şu fazları bilir:

idle
input
build
layout
paint
composite
post_frame

Lifecycle sırasında reentrant layout/paint yasaktır.

Örneğin paint sırasında:

mark_needs_layout()

bir sonraki frame’e schedule edilmelidir.

⸻

33. VSync

Platform backend:

request_frame()

üzerinden framework’e vsync sağlar.

Frame başlangıcı:

timestamp
frame_number
delta

taşımalıdır.

Animation framework sistem saatini doğrudan okumaz.

Frame timestamp kullanır.

⸻

34. Input Model

Platform event’leri normalize edilir:

PointerDown
PointerMove
PointerUp
PointerCancel
PointerScroll
KeyDown
KeyUp
TextInput
WindowResize
WindowFocus

Widget/platform native event objesi taşımaz.

⸻

35. Hit Testing

RenderObject:

def hit_test(
    self: RenderObject,
    result: HitTestResult,
    position: Offset
) -> bool:

benzeri contract’a sahip olur.

Hit test sonucu:

leaf → root

path tutar.

Event dispatch:

capture phase
target phase
bubble phase

zorunlu değildir.

Aura v0.1’de Flutter benzeri target propagation ile başlayabilir.

⸻

36. Gesture Arena

Pointer event ile Button davranışı aynı şey değildir.

Gesture abstraction:

Tap
DoubleTap
LongPress
Pan
Drag
Scale

için GestureArena kullanılmalıdır.

Bir pointer sequence için recognizer’lar yarışır.

Kazanan gesture event’i alır.

Bu sistem scroll ile button tap çatışmasını çözmek için gereklidir.

⸻

37. Focus Sistemi

Focus bağımsız tree olarak tasarlanmalıdır.

FocusNode
FocusScope
FocusManager

Keyboard input doğrudan Widget’a gönderilmez.

Aktif FocusNode üzerinden dispatch edilir.

⸻

38. Text Input

TextField doğrudan KeyDown event’lerinden karakter üretmeye çalışmaz.

IME destekli platform text input API’si kullanılmalıdır.

Ayrım:

physical key
logical key
text composition

korunmalıdır.

⸻

39. Accessibility / Semantics

Accessibility sonradan eklenecek bir özellik değildir.

İlk RenderObject API’sinde semantic hook bulunmalıdır.

SemanticsNode
role
label
value
enabled
checked
actions
bounds

Platform bridge:

macOS AX
Windows UI Automation
Linux AT-SPI

ile konuşabilir.

İlk prototype gerçek platform entegrasyonu içermese bile Semantics Tree mimarisi baştan var olmalıdır.

⸻

40. Theme

Theme global singleton olmaz.

Inherited state üzerinden akar.

Theme
ThemeData
ColorScheme
Typography

BuildContext:

Theme.of(context)

benzeri lookup yapabilir.

Aura hiçbir platformun Material veya Cupertino tasarımını framework’ün default kimliği olarak kabul etmez.

Aura kendi nötr design primitives’ine sahip olur.

⸻

41. Styling

Aura CSS clone yapmaz.

Styles typed object’lerdir.

Örnek:

TextStyle(
    font_size=16.0,
    font_weight=FontWeight.medium,
    color=Color(...)
)

Dinamik string style engine kullanılmaz.

Bunun avantajı:

compile-time typing
autocomplete
faster runtime
no CSS parser

⸻

42. Design System Katmanı

Core widget’lardan ayrı:

aura_design

paketi oluşturulabilir.

Burada:

Button
Card
Dialog
NavigationBar
TextField
Switch
Slider
Tabs
Menu
Tooltip

gibi opinionated komponentler bulunur.

Core rendering bu komponentlere bağlı değildir.

⸻

43. Navigation

Navigation ilk framework sürümünde minimal tutulur.

Temel:

Navigator
Route
RouteStack

Navigation state:

push
pop
replace
can_pop

destekler.

Router/deep-link sistemi sonraki fazdır.

⸻

44. Overlay

Dialog, menu, tooltip gibi şeyler için:

Overlay
OverlayEntry

altyapısı kullanılır.

Bunlar window seviyesinde absolute positioned child hack’leriyle yapılmaz.

⸻

45. Portals

Aura Flutter’dan ileri olarak kontrollü Portal API sunabilir.

Portal sayesinde:

logical parent
     ≠
render parent

olabilir.

Örnek:

dropdown
tooltip
context menu

Lifecycle ve Theme inheritance logical tree üzerinden korunur.

⸻

46. Scroll Sistemi

Scroll ilk büyük milestone sonrası eklenir.

Katmanlar:

Scrollable
ScrollController
ScrollPosition
Viewport
Sliver

İlk sürüm basit:

SingleChildScrollView
ListView

ile başlayabilir.

Ama virtualized list desteği olmadan 1.0 çıkarılmamalıdır.

⸻

47. Sliver Benzeri Model

Uzun listeler için bütün child’ları oluşturmak yasaktır.

Aura:

VirtualList

veya sliver benzeri lazy layout sunmalıdır.

Ama Flutter terminolojisini birebir kopyalamak zorunda değildir.

Önerilen isim:

ViewportItemProvider
VirtualList
VirtualGrid

⸻

48. State Preservation

Virtualized item dispose edilirse state otomatik olarak kaybolabilir.

Framework açık retention policy sağlamalıdır.

Örnek:

keep_alive

ancak sınırsız retention yapılmaz.

⸻

49. Animation

Animation sistemi frame scheduler’a bağlıdır.

Temel:

Ticker
AnimationController
Animation[T]
Tween[T]
Curve

Animation callback doğrudan OS timer kullanmaz.

VSync timestamp kullanır.

⸻

50. Implicit Animations

Kolay kullanım için:

AnimatedOpacity
AnimatedSize
AnimatedPosition

gibi widget’lar sonraki fazda eklenir.

İlk primitive:

Tween
AnimationController

olmalıdır.

⸻

51. Async UI Kuralları

Nox spawn / await Aura içinde kullanılabilir.

Fakat:

* build sırasında blocking await yapılmaz.
* renderer callback’lerinde blocking I/O yapılmaz.
* network/disk işlemleri UI frame pipeline dışında yapılır.

Async sonuç state’e uygulanır:

task
 ↓
result
 ↓
state mutation
 ↓
invalidate

⸻

52. Concurrency

Aura UI tree:

single logical UI thread

modeline sahiptir.

Nox’un M:N runtime’ı arka plan işleri için kullanılabilir.

Render tree aynı anda birden fazla worker tarafından mutate edilmez.

İleride parallel layout yalnız ayrı ADR ile düşünülebilir.

⸻

53. FFI Sınırı

Platform API veya renderer C API’leri:

Aura Nox API
   ↓
small native bridge
   ↓
platform/renderer

üzerinden çağrılır.

Widget katmanında extern def bulunmaz.

FFI sadece:

platform
graphics backend
text backend

gibi dar katmanlarda bulunabilir.

⸻

54. Native Handle Ownership

Her native handle wrapper açık lifecycle contract’a sahip olmalıdır.

Zorunlu testler:

create → use → close
close → close
close → method
failed create → cleanup
partial initialization → cleanup
exception → cleanup

Nox stdlib’de daha önce görülen native handle UAF/double-free sınıfı Aura’ya taşınmamalıdır.

⸻

55. Resource Types

Graphics kaynakları:

Image
Texture
Font
Shader
Surface

idempotent dispose desteklemelidir.

Dispose sonrası kullanım deterministic framework error üretmelidir.

Raw dangling native pointer oluşmamalıdır.

⸻

56. Platform Backend Interface

Önerilen API:

PlatformBackend
  create_window
  destroy_window
  poll_events
  request_frame
  set_cursor
  clipboard
  text_input
  accessibility

Window:

WindowHandle
size
scale_factor
title
visibility

gibi metadata taşır.

⸻

57. İlk Platform

Aura ilk gerçek renderer milestone’unda:

macOS

veya:

Linux

tek platform seçebilir.

Ancak platform API baştan çoklu backend’e uygun olmalıdır.

Platform-specific if kontrolleri framework boyunca dağılmaz.

⸻

58. Windows/macOS/Linux

Uzun vadeli hedef:

Aura Core
   │
   ├── Cocoa backend
   ├── Win32 backend
   └── Wayland/X11 backend

Windows veya Linux’un platform detayları widget API’sini değiştirmez.

⸻

59. Mobile

Android/iOS ancak desktop renderer ve input pipeline stabilize olduktan sonra başlar.

Mobil için gereken ek alanlar:

touch gestures
IME
soft keyboard
safe areas
lifecycle
orientation
GPU surface
accessibility
app packaging

Desktop mimarisini bozmadan eklenebilmelidir.

⸻

60. Web

Web ilk hedef değildir.

Aura Web gerekiyorsa sonraki büyük sürümde ayrı renderer/backend olarak değerlendirilir.

Nox’un native AOT modelini bozacak özel dil backend’i Aura uğruna eklenmez.

⸻

61. Hot Reload

Aura v0.x hedefinde gerçek Flutter Hot Reload zorunlu değildir.

İlk olarak:

fast rebuild
restart
state-independent development cycle

yeterlidir.

Hot reload için Nox derleyicisine özel runtime mutation sistemi eklenmez.

İleride:

hot restart
dynamic module reload

ayrı araştırma konusu olabilir.

⸻

62. DevTools

Aura DevTools hedefleri:

Widget inspector
Element inspector
Render tree inspector
layout bounds overlay
repaint overlay
frame timing
allocation counters
dirty rebuild count

İlk sürümde CLI/debug dump yeterlidir.

GUI DevTools sonradan yapılabilir.

⸻

63. Debug Modes

Framework:

debug
profile
release

davranışlarını ayırabilir.

Debug:

assertions
lifecycle validation
layout validation
diagnostics

Profile:

timings
counters
minimal diagnostics

Release:

diagnostics off
optimized paths

UI semantics build-mode’a göre değişmez.

⸻

64. Diagnostics

Hatalar açıklayıcı olmalıdır.

Örnek:

RenderFlex overflow
Widget reused with incompatible key
set_state after dispose
layout called during paint
cyclic build dependency
invalid constraints

Diagnostic yalnız:

"invalid state"

dememelidir.

⸻

65. Framework Error Policy

Programmer errors:

assert/exception

ile açıkça raporlanır.

Recoverable runtime events:

missing image
failed font
platform event

framework’ü crash ettirmemelidir.

⸻

66. Performance Hedefleri

60 Hz:

16.67 ms frame budget

120 Hz:

8.33 ms

Aura architecture 120 Hz’i tasarım hedefi olarak kabul eder.

Ama ilk implementation performans uğruna correctness’ten vazgeçmez.

⸻

67. Frame Budget İzleme

Profile mode:

build_ms
layout_ms
paint_ms
composite_ms
total_frame_ms

ölçmelidir.

Ayrıca:

widgets_built
elements_updated
render_objects_laid_out
render_objects_painted
layers_submitted

counter’ları tutulabilir.

⸻

68. Allocation Politikası

Her frame yüz binlerce transient allocation kabul edilmez.

Ama premature object pooling de yasaktır.

Önce ölç.

Sonra optimize et.

Potansiyel optimize alanları:

Element reuse
RenderObject reuse
small geometry values
display lists
layer reuse
text paragraph cache
image cache

⸻

69. Object Pooling

Widget pool yapılmaz.

Widget ephemeral configuration’dır.

Pooling düşünülürse:

native command buffers
display-list blocks
temporary layout arrays

gibi ölçülmüş hotspot’larda kullanılır.

⸻

70. Memory Leaks

Her lifecycle component için test:

mount
update
unmount
drop external refs
cycle collection
zero retained objects

olmalıdır.

Özellikle:

Element ↔ State
Element ↔ RenderObject
listeners
Signals
animation controllers
focus nodes

cycle riski taşır.

⸻

71. Lifecycle

Widget lifecycle yoktur.

Element lifecycle:

initial
mounted
active
inactive
unmounted

State lifecycle:

created
initialized
active
disposed

Dispose iki kez çağrılsa framework bug’ıdır.

⸻

72. Stateful Lifecycle API

Hedef:

class State[T]:
    def init_state(self: State[T]) -> None:
        pass
    def did_update_widget(
        self: State[T],
        old_widget: T
    ) -> None:
        pass
    def dispose(self: State[T]) -> None:
        pass

Lifecycle callback sırasında illegal operation’lar açıkça belgelenir.

⸻

73. Dependency Injection

Aura global service locator sağlamaz.

Uygulama-level dependency:

InheritedWidget
Provider-style package
constructor injection

ile çözülebilir.

DI framework core kapsamı değildir.

⸻

74. Context Dependencies

Bir Element:

Theme.of(context)

çağırdığında ilgili inherited value’ya dependency kaydeder.

Inherited value değişirse yalnız dependent Element dirty edilir.

Bu Flutter’daki InheritedWidget fikrini korur.

⸻

75. Aura’nın Flutter’dan Bilinçli Farkları

Aura şu alanlarda Flutter’dan farklı olacaktır:

1. Reactive Signal desteği framework-native olabilir.
2. Rebuild batching varsayılan davranıştır.
3. Styling Material merkezli değildir.
4. Platform backend sınırı daha serttir.
5. Render/scene abstraction renderer'dan bağımsız tutulur.
6. DevTools metrics ilk günden architecture parçasıdır.
7. Accessibility sonradan eklenmez.
8. Nox async runtime native olarak kullanılır.
9. Ownership API'ye sızmaz.
10. Framework Nox dilini yönlendirmez.

⸻

76. Nox Dil Sınırı — Çok Önemli

Aura ajanları Nox compiler repo’sunu değiştirmemelidir.

Aura’da bir problem görülürse sıralama:

1. Aura API ile çöz
2. Aura internal abstraction ile çöz
3. Nox'un mevcut stdlib/generic/protocol özellikleriyle çöz
4. Native bridge ile çöz
5. Sorun gerçekten genel bir dil eksikliği ise rapor oluştur

Doğrudan:

"Bu Aura'da kolay olur, Nox'a şu syntax'ı ekleyelim."

yasaktır.

Nox 2.0 stable contract kabul edilir.

⸻

77. Nox Features We Rely On

Aura aşağıdaki Nox özelliklerini temel kabul eder:

classes
single inheritance
generic classes
generic functions
protocols
first-class functions
closures
ARC / cycle handling
exceptions
async
spawn / await
Task[T]
Buffer
Span
nox.mem
nox.bits
fixed-width integers
FFI / callbacks
package system

Framework bu özelliklerin dışına özel compiler davranışı varsaymaz.

⸻

78. API İsimlendirme

Public API:

PascalCase classes
snake_case functions/methods

kullanır.

Örnek:

BuildContext
RenderObject
mark_needs_layout
create_element
perform_layout

Nox’un Pythonik görünümü korunur.

⸻

79. Public / Internal API

Internal semboller:

_

prefix’iyle işaretlenmelidir.

Örnek:

_reconcile_children
_mount_child
_flush_layout

Kullanıcı-facing API ile implementation details karıştırılmaz.

⸻

80. Compatibility

Aura kendi semantic versioning politikasına sahip olur.

Public API breaking change:

major

gerektirir.

Internal renderer/backend API:

0.x

döneminde daha hızlı değişebilir.

⸻

81. Test Katmanları

Aura test sistemi:

unit tests
widget tests
render tests
golden image tests
integration tests
platform tests
benchmark tests

olarak ayrılır.

⸻

82. Widget Test Harness

Widget testleri gerçek pencere gerektirmemelidir.

Örnek:

WidgetTester
  pump_widget
  pump_frame
  find
  tap
  enter_text
  expect_text

Ancak Flutter API isimleri zorunlu değildir.

⸻

83. Render Tests

Render testleri:

constraints
size
position
hit-test
dirty propagation

doğrular.

Örnek:

Column 300x400 constraint altında:
child A = 100x20
child B = 100x30
expected positions...

⸻

84. Golden Tests

Renderer stabil olduğunda pixel golden test eklenir.

Golden test:

scene input
   ↓
render
   ↓
reference image

platform variation toleransı kontrollü olmalıdır.

Text rendering farkları golden’ı flaky hale getirmemelidir.

⸻

85. Property Tests

Özellikle layout için property test değerlidir.

Örnek invariant’lar:

size >= min constraint
size <= max constraint
NaN yok
negative physical size yok
child parent bounds invariant

⸻

86. Benchmark Suite

Minimum benchmark’lar:

10k StatelessWidget build
10k Element reconciliation
1000-row virtual list scroll
deep tree layout
text-heavy screen
animation frame
signal update
button event dispatch

Her benchmark allocation count da raporlamalıdır.

⸻

87. İlk Milestone — Aura M0

Amaç:

Framework çekirdeğinin compile edilmesi.

İçerik:

Widget
Element
BuildContext
StatelessWidget
RenderObject
RenderBox
Constraints
Size
Offset
PipelineOwner

Renderer yok.

Unit tests var.

⸻

88. Aura M1 — İlk Görsel

Amaç:

Pencere aç ve dikdörtgen çiz.

Window
Graphics backend
Surface
Canvas
Color
RenderBox

Sonuç:

800 × 600 window
background
single colored rectangle

⸻

89. Aura M2 — Widget Pipeline

Amaç:

Widget
 ↓
Element
 ↓
RenderObject
 ↓
Layout
 ↓
Paint

tam zinciri.

Widget’lar:

Container
Padding
SizedBox
Center

⸻

90. Aura M3 — Flex

Eklenir:

Row
Column
Expanded
Flexible
Spacer

Constraint tests zorunlu.

⸻

91. Aura M4 — Text

Eklenir:

Text
TextStyle
Paragraph
font loading
basic wrapping

Text backend izole tutulur.

⸻

92. Aura M5 — Input

Eklenir:

PointerEvent
HitTest
Button
KeyboardEvent
Focus

Demo:

counter app

çalışmalıdır.

⸻

93. Aura M6 — Stateful

Eklenir:

StatefulWidget
State[T]
invalidate
batched rebuild
Signal[T]
Computed[T]

Counter yalnız state değişen subtree’yi rebuild etmelidir.

⸻

94. Aura M7 — Animation

Eklenir:

Ticker
AnimationController
Tween
Curve
Opacity
Transform

60/120 Hz profiling yapılır.

⸻

95. Aura M8 — Scrolling

Eklenir:

Scrollable
ScrollController
Viewport
VirtualList

10.000 row demo memory patlamadan scroll edebilmelidir.

⸻

96. Aura M9 — Production Foundation

Eklenir:

accessibility tree
text input / IME
clipboard
menus
cursor
window lifecycle
theme
navigation
overlay

⸻

97. Aura 0.1 Definition of Done

Aura 0.1:

desktop single-window
declarative widgets
stateful UI
text
buttons
layout
input
basic animations
virtualized scrolling
theme
navigation

sunmalıdır.

En az bir gerçek demo:

Notes/Todo application

olmalıdır.

⸻

98. Aura 0.2

Hedef:

multi-window
dialogs
menus
better text fields
drag/drop
images
advanced scrolling
accessibility bridge

⸻

99. Aura 0.3

Hedef:

Windows/macOS/Linux parity
improved renderer
devtools
performance tuning
plugin API

⸻

100. Aura 1.0

1.0 için gerekli:

stable public API
desktop 3-platform support
accessibility
IME
virtual lists
navigation
animations
images
clipboard
drag/drop
window APIs
golden tests
performance benchmarks
documentation
examples
package ecosystem

⸻

101. İlk Referans Uygulamalar

Framework’ün gerçek dünya doğrulaması için sırayla:

1. Counter
2. Todo
3. Calculator
4. Notes
5. Settings app
6. Chat UI
7. File browser
8. Dashboard

Her uygulama yeni bir framework özelliği doğrulamalıdır.

⸻

102. Anti-Patterns

Aşağıdakiler yasaktır:

Widget içinde native handle
Build sırasında network call
Global mutable UI state
Her state değişiminde full-tree rebuild
Her frame full-tree layout
Her frame full-tree repaint
String tabanlı CSS engine
Widget = RenderObject birleştirmesi
Platform if/else'lerin framework boyunca dağılması
Nox compiler patch'iyle framework bug'ını çözmek
unsafe pointer'ların public API'ye sızması

⸻

103. Kod Yazmadan Önce Agent Checklist

Her görevin başında ajan şu soruları cevaplamalıdır:

Bu hangi katmana ait?
Public API mi internal mı?
Widget/Element/RenderObject sınırlarından hangisine dokunuyor?
Lifecycle etkisi var mı?
Layout etkisi var mı?
Paint etkisi var mı?
Platform-specific mi?
Memory ownership riski var mı?
Test türü ne olmalı?
Benchmark gerekiyor mu?

⸻

104. Değişiklik Sonrası Checklist

Her görev sonunda:

[ ] Nox syntax geçerli
[ ] Yeni Nox dili özelliği varsayılmadı
[ ] Katman bağımlılık yönü korunuyor
[ ] Public API docs güncel
[ ] Unit test var
[ ] Render değişikliyse render test var
[ ] Lifecycle değişikliyse mount/unmount testi var
[ ] Native handle varsa double-close testi var
[ ] Allocation/lifetime kontrol edildi
[ ] Gereksiz full rebuild/layout/paint yok
[ ] Benchmark gerektiren değişiklik ölçüldü
[ ] ADR gerekiyorsa yazıldı

⸻

105. ADR Zorunlu Konular

Şu kararlar ADR olmadan değiştirilemez:

graphics backend
text shaping backend
Widget/Element relationship
State model
Signal model
layout protocol
scene/layer model
platform abstraction
threading model
navigation architecture
plugin/native bridge model

ADR formatı:

Context
Decision
Alternatives
Consequences
Migration

⸻

106. İlk ADR’ler

Repo oluşturulduğunda şu ADR’ler yazılmalıdır:

ADR-001 Widget/Element/RenderObject architecture
ADR-002 Constraint-based layout
ADR-003 State + Signal hybrid model
ADR-004 Renderer backend choice
ADR-005 Platform abstraction
ADR-006 UI-thread ownership model
ADR-007 Scene/layer composition
ADR-008 Text shaping strategy

⸻

107. Cursor İçin Görev Sırası

Cursor ajanı framework’ü rastgele dosyalardan başlatmamalıdır.

Sıra:

Phase 0
  repo skeleton
  AGENTS.md
  README
  ADRs
Phase 1
  geometry
  constraints
  Widget
  Element
  BuildContext
Phase 2
  RenderObject
  RenderBox
  PipelineOwner
  layout tests
Phase 3
  platform window abstraction
  graphics abstraction
  first renderer
Phase 4
  Container/Padding/SizedBox/Center
Phase 5
  Row/Column/Flex
Phase 6
  text
Phase 7
  input + hit testing
Phase 8
  StatefulWidget + State
Phase 9
  Signal + Computed
Phase 10
  animation
Phase 11
  scrolling/virtualization
Phase 12
  accessibility/navigation/devtools

Bir phase tamamlanmadan sonraki büyük phase’e geçilmez.

⸻

108. İlk Büyük Teknik Başarı Kriteri

Aura’nın architecture proof’u şu demo değildir:

window opens

Asıl başarı:

State mutation
    ↓
dirty Element
    ↓
incremental rebuild
    ↓
minimal relayout
    ↓
minimal repaint
    ↓
scene submission

zincirinin doğru çalışmasıdır.

Bu başarıldığında framework’ün omurgası kurulmuş sayılır.

⸻

109. İlk Performance Proof

Counter testinde:

root
 └── Column
      ├── static header
      ├── counter text
      └── button

count değiştiğinde:

static header

rebuild veya repaint edilmemelidir, eğer dependency yoksa.

DevTools bunu gösterebilmelidir.

⸻

110. Aura’nın Uzun Vadeli Kimliği

Aura:

Flutter syntax clone

değildir.

Aura’nın hedefi:

Flutter'ın deklaratif ergonomisi
+
React tarzı reactive primitives
+
Flutter'ın constraint layout gücü
+
retained render tree
+
Nox'un native AOT performansı
+
Nox async runtime
+
sıkı platform abstraction
+
görünmez ownership

kombinasyonudur.

Nihai kullanıcı deneyimi:

Python kadar okunabilir
Flutter kadar üretken
native framework kadar hızlı
Nox kadar statik ve deterministik

olmalıdır.

⸻

111. Son Değişmez Kural

Bir ajan bir Aura özelliğini çalıştırmak için Nox 2.0 dilinin semantiğini değiştirme ihtiyacı hissederse:

Aura kodunu değiştirmeyi dene.

Framework’ün dili şekillendirmesine izin verme.

Nox 2.0 Aura’nın üzerinde durduğu platformdur; Aura Nox 2.0’ın deney laboratuvarı değildir.

Gerçek genel bir compiler eksikliği keşfedilirse:

minimal repro
expected behavior
actual behavior
framework-independent justification

ile ayrı Nox issue’su hazırlanır.

Aura implementasyonu o issue çözülene kadar mevcut dil içinde güvenli workaround kullanır veya ilgili özelliği erteler.