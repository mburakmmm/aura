#include <stdlib.h>
#include <string.h>
#include <mach/mach_time.h>
#include <Cocoa/Cocoa.h>
#include <CoreVideo/CoreVideo.h>
#include "paint.h"
#include "plugin.h"

enum {
    AURA_POINTER_DOWN = 1,
    AURA_POINTER_MOVE = 2,
    AURA_POINTER_UP = 3,
    AURA_POINTER_CANCEL = 4,
    AURA_KEY_DOWN = 5,
    AURA_KEY_UP = 6,
    AURA_RESIZE = 7,
    AURA_SCROLL = 8,
    AURA_TEXT = 9,
    AURA_WINDOW_FOCUS = 13,
    AURA_FILE_DROP = 14,
    AURA_AX_ACTION = 15
};

typedef struct AuraEvent {
    int kind;
    double x;
    double y;
    int64_t key;
    int64_t button;
    char *text;
} AuraEvent;

typedef struct AuraWindow AuraWindow;

@interface AuraAXNode : NSAccessibilityElement
@property (nonatomic, assign) AuraWindow *owner;
@property (nonatomic, assign) int64_t action;
@property (nonatomic, copy) NSString *labelText;
@property (nonatomic, copy) NSString *valueText;
@property (nonatomic, copy) NSString *roleKey;
@property (nonatomic, copy) NSString *roleName;
@property (nonatomic, assign) int checked;
@property (nonatomic, assign) NSRect localFrame;
@end

@interface AuraView : NSView <NSTextInputClient>
@property (nonatomic, assign) AuraWindow *owner;
@end

@interface AuraDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) AuraWindow *owner;
@end

struct AuraWindow {
    int alive;
    int should_close;
    NSWindow *window;
    AuraView *view;
    AuraDelegate *delegate;
    AuraEvent *events;
    int event_count;
    int event_cap;
    uint8_t *pixels;
    int pixel_w;
    int pixel_h;
    int pixel_stride;
    double scale;
    NSView *plugin_views[8];
    int64_t plugin_ids[8];
    dispatch_semaphore_t vsync;
    CVDisplayLinkRef link;
    uint64_t frame_host;
    int text_focus;
    NSString *marked;
    NSString *contents;
    NSRange selected;
    double caret_x;
    double caret_y;
    double caret_h;
    void *ax_nodes;
    int ax_count;
};

#define AURA_MAX_WINDOWS 32
static AuraWindow *g_windows[AURA_MAX_WINDOWS];

static AuraWindow *window_get(int64_t handle, int require_alive) {
    if (handle <= 0 || handle > AURA_MAX_WINDOWS) {
        return NULL;
    }
    AuraWindow *window = g_windows[handle - 1];
    if (window == NULL) {
        return NULL;
    }
    if (require_alive && !window->alive) {
        return NULL;
    }
    return window;
}

static void aura_push(AuraWindow *window, int kind, double x, double y, int64_t key, int64_t button) {
    if (window == NULL || !window->alive) {
        return;
    }
    if (window->event_count >= window->event_cap) {
        int cap = window->event_cap == 0 ? 32 : window->event_cap * 2;
        AuraEvent *next = realloc(window->events, (size_t)cap * sizeof(AuraEvent));
        if (next == NULL) {
            return;
        }
        window->events = next;
        window->event_cap = cap;
    }
    AuraEvent *event = &window->events[window->event_count];
    event->kind = kind;
    event->x = x;
    event->y = y;
    event->key = key;
    event->button = button;
    event->text = NULL;
    window->event_count += 1;
}

static void aura_free_event_text(AuraWindow *window) {
    int i = 0;
    for (i = 0; i < window->event_count; i++) {
        free(window->events[i].text);
        window->events[i].text = NULL;
    }
}

static void aura_push_text(AuraWindow *window, int kind, const char *text) {
    aura_push(window, kind, 0, 0, 0, 0);
    if (window == NULL || window->event_count <= 0) {
        return;
    }
    if (text == NULL) {
        text = "";
    }
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, text, length + 1);
    window->events[window->event_count - 1].text = copy;
}

static NSString *aura_input_string(id string) {
    if (string == nil) {
        return @"";
    }
    if ([string isKindOfClass:[NSAttributedString class]]) {
        return [(NSAttributedString *)string string];
    }
    return (NSString *)string;
}

static void ensure_app(void) {
    static int done = 0;
    if (done) {
        return;
    }
    done = 1;
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
}

static double aura_host_seconds(uint64_t host) {
    static mach_timebase_info_data_t info;
    static int ready = 0;
    if (!ready) {
        if (mach_timebase_info(&info) != KERN_SUCCESS || info.denom == 0) {
            info.numer = 1;
            info.denom = 1;
        }
        ready = 1;
    }
    return ((double)host * (double)info.numer) / ((double)info.denom * 1000000000.0);
}

static CVReturn aura_link_callback(CVDisplayLinkRef link, const CVTimeStamp *now, const CVTimeStamp *output, CVOptionFlags flags_in, CVOptionFlags *flags_out, void *context) {
    (void)link;
    (void)flags_in;
    (void)flags_out;
    AuraWindow *window = (AuraWindow *)context;
    if (window != NULL && window->alive) {
        const CVTimeStamp *stamp = output;
        if (stamp == NULL) {
            stamp = now;
        }
        if (stamp != NULL) {
            window->frame_host = stamp->hostTime;
        }
        if (window->vsync != NULL) {
            dispatch_semaphore_signal(window->vsync);
        }
    }
    return kCVReturnSuccess;
}

static void copy_frame_pixels(AuraWindow *window) {
    const uint8_t *source = aura_paint_pixels();
    int64_t width = aura_paint_pixel_width();
    int64_t height = aura_paint_pixel_height();
    int64_t stride = aura_paint_stride();
    size_t bytes = 0;
    uint8_t *next = NULL;
    if (source == NULL || width <= 0 || height <= 0 || stride <= 0) {
        return;
    }
    bytes = (size_t)stride * (size_t)height;
    next = (uint8_t *)malloc(bytes);
    if (next == NULL) {
        return;
    }
    memcpy(next, source, bytes);
    free(window->pixels);
    window->pixels = next;
    window->pixel_w = (int)width;
    window->pixel_h = (int)height;
    window->pixel_stride = (int)stride;
}

@implementation AuraAXNode
- (NSAccessibilityRole)accessibilityRole {
    if (self.roleName == nil) {
        return NSAccessibilityUnknownRole;
    }
    return self.roleName;
}
- (NSString *)accessibilityLabel {
    if (self.labelText == nil) {
        return @"";
    }
    return self.labelText;
}
- (id)accessibilityValue {
    if ([self.roleName isEqualToString:NSAccessibilityCheckBoxRole]) {
        if (self.checked != 0) {
            return @YES;
        }
        return @NO;
    }
    if (self.valueText == nil) {
        return @"";
    }
    return self.valueText;
}
- (NSRect)accessibilityFrame {
    if (self.owner == NULL || self.owner->view == nil || self.owner->window == nil) {
        return self.localFrame;
    }
    NSRect inWindow = [self.owner->view convertRect:self.localFrame toView:nil];
    return [self.owner->window convertRectToScreen:inWindow];
}
- (BOOL)accessibilityPerformPress {
    if (self.owner == NULL || self.action < 0) {
        return NO;
    }
    aura_push(self.owner, AURA_AX_ACTION, 0, 0, self.action, 0);
    return YES;
}
@end

@implementation AuraView
- (BOOL)isFlipped {
    return YES;
}
- (BOOL)isAccessibilityElement {
    return YES;
}
- (NSAccessibilityRole)accessibilityRole {
    return NSAccessibilityGroupRole;
}
- (NSString *)accessibilityLabel {
    if (self.owner == NULL || self.owner->window == nil) {
        return @"Aura";
    }
    return self.owner->window.title;
}
- (NSArray *)accessibilityChildren {
    if (self.owner == NULL || self.owner->ax_nodes == NULL) {
        return @[];
    }
    return (NSArray *)self.owner->ax_nodes;
}
- (BOOL)acceptsFirstResponder {
    return YES;
}
- (void)mouseDown:(NSEvent *)event {
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    aura_push(self.owner, AURA_POINTER_DOWN, point.x, point.y, 0, (int64_t)event.buttonNumber + 1);
}
- (void)mouseDragged:(NSEvent *)event {
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    aura_push(self.owner, AURA_POINTER_MOVE, point.x, point.y, 0, (int64_t)event.buttonNumber + 1);
}
- (void)mouseUp:(NSEvent *)event {
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    aura_push(self.owner, AURA_POINTER_UP, point.x, point.y, 0, (int64_t)event.buttonNumber + 1);
}
- (void)rightMouseDown:(NSEvent *)event {
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    aura_push(self.owner, AURA_POINTER_DOWN, point.x, point.y, 0, 2);
}
- (void)rightMouseUp:(NSEvent *)event {
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    aura_push(self.owner, AURA_POINTER_UP, point.x, point.y, 0, 2);
}
- (void)scrollWheel:(NSEvent *)event {
    aura_push(self.owner, AURA_SCROLL, event.scrollingDeltaX, event.scrollingDeltaY, 0, 0);
}
- (void)keyDown:(NSEvent *)event {
    int64_t mods = 0;
    if (event.modifierFlags & NSEventModifierFlagCommand) {
        mods |= 1;
    }
    if (event.modifierFlags & NSEventModifierFlagShift) {
        mods |= 2;
    }
    aura_push(self.owner, AURA_KEY_DOWN, 0, 0, (int64_t)event.keyCode, mods);
    if (self.owner != NULL && self.owner->text_focus) {
        [self interpretKeyEvents:@[event]];
        return;
    }
    NSString *chars = event.characters;
    if (chars != nil && chars.length > 0) {
        unichar code = [chars characterAtIndex:0];
        aura_push(self.owner, AURA_TEXT, 0, 0, (int64_t)code, 0);
    }
}
- (void)insertText:(id)string {
    [self insertText:string replacementRange:NSMakeRange(NSNotFound, 0)];
}
- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
    (void)replacementRange;
    NSString *value = aura_input_string(string);
    const char *utf8 = [value UTF8String];
    aura_push_text(self.owner, 10, utf8);
    if (self.owner != NULL && self.owner->marked != nil) {
        [self.owner->marked release];
        self.owner->marked = nil;
    }
}
- (void)doCommandBySelector:(SEL)selector {
    if (selector == @selector(deleteBackward:)) {
        aura_push(self.owner, 11, 0, 0, 0, 0);
    } else if (selector == @selector(insertNewline:)) {
        aura_push_text(self.owner, 10, "\n");
    } else if (selector == @selector(paste:) || selector == @selector(copy:) || selector == @selector(cut:)) {
        return;
    } else if (selector == @selector(moveLeft:) || selector == @selector(moveRight:) || selector == @selector(moveUp:) || selector == @selector(moveDown:)) {
        return;
    } else if (selector == @selector(moveLeftAndModifySelection:) || selector == @selector(moveRightAndModifySelection:) || selector == @selector(moveUpAndModifySelection:) || selector == @selector(moveDownAndModifySelection:)) {
        return;
    }
}
- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange {
    (void)selectedRange;
    (void)replacementRange;
    NSString *value = aura_input_string(string);
    if (self.owner == NULL) {
        return;
    }
    if (self.owner->marked != nil) {
        [self.owner->marked release];
    }
    self.owner->marked = [value copy];
    aura_push_text(self.owner, 12, [value UTF8String]);
}
- (void)unmarkText {
    if (self.owner != NULL && self.owner->marked != nil) {
        [self.owner->marked release];
        self.owner->marked = nil;
    }
}
- (NSRange)selectedRange {
    if (self.owner == NULL) {
        return NSMakeRange(0, 0);
    }
    return self.owner->selected;
}
- (NSRange)markedRange {
    if (self.owner == NULL || self.owner->marked == nil) {
        return NSMakeRange(NSNotFound, 0);
    }
    return NSMakeRange(self.owner->selected.location, self.owner->marked.length);
}
- (BOOL)hasMarkedText {
    return self.owner != NULL && self.owner->marked != nil && self.owner->marked.length > 0;
}
- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actualRange {
    NSString *contents = @"";
    if (self.owner != NULL && self.owner->contents != nil) {
        contents = self.owner->contents;
    }
    if (range.location == NSNotFound || range.location > contents.length) {
        return nil;
    }
    NSUInteger end = range.location + range.length;
    if (end > contents.length) {
        end = contents.length;
    }
    NSRange safe = NSMakeRange(range.location, end - range.location);
    if (actualRange != NULL) {
        *actualRange = safe;
    }
    return [[[NSAttributedString alloc] initWithString:[contents substringWithRange:safe]] autorelease];
}
- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText {
    return @[];
}
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange {
    (void)range;
    if (self.owner == NULL) {
        return NSZeroRect;
    }
    if (actualRange != NULL) {
        *actualRange = self.owner->selected;
    }
    NSRect rect = NSMakeRect(self.owner->caret_x, self.owner->caret_y, 2.0, self.owner->caret_h > 0.0 ? self.owner->caret_h : 16.0);
    return [self.window convertRectToScreen:[self convertRect:rect toView:nil]];
}
- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    (void)point;
    if (self.owner == NULL) {
        return 0;
    }
    return self.owner->selected.location;
}
- (void)keyUp:(NSEvent *)event {
    int64_t mods = 0;
    if (event.modifierFlags & NSEventModifierFlagCommand) {
        mods |= 1;
    }
    if (event.modifierFlags & NSEventModifierFlagShift) {
        mods |= 2;
    }
    aura_push(self.owner, AURA_KEY_UP, 0, 0, (int64_t)event.keyCode, mods);
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    (void)sender;
    return NSDragOperationCopy;
}
- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender {
    (void)sender;
    return YES;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSPasteboard *board = sender.draggingPasteboard;
    NSString *path = nil;
    NSString *url_string = [board stringForType:NSPasteboardTypeFileURL];
    if (url_string == nil && board.pasteboardItems.count > 0) {
        url_string = [board.pasteboardItems[0] stringForType:NSPasteboardTypeFileURL];
    }
    if (url_string != nil) {
        NSURL *url = [NSURL URLWithString:url_string];
        if (url != nil) {
            path = url.path;
        }
    }
    if (path == nil) {
        return NO;
    }
    NSPoint point = [self convertPoint:sender.draggingLocation fromView:nil];
    aura_push_text(self.owner, AURA_FILE_DROP, [path UTF8String]);
    if (self.owner != NULL && self.owner->event_count > 0) {
        self.owner->events[self.owner->event_count - 1].x = point.x;
        self.owner->events[self.owner->event_count - 1].y = point.y;
    }
    return YES;
}
- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    aura_push(self.owner, AURA_RESIZE, newSize.width, newSize.height, 0, 0);
}
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    AuraWindow *window = self.owner;
    unsigned char *planes[1];
    NSBitmapImageRep *rep = nil;
    NSImage *image = nil;
    if (window == NULL || window->pixels == NULL || window->pixel_w <= 0 || window->pixel_h <= 0) {
        return;
    }
    planes[0] = window->pixels;
    rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:planes pixelsWide:window->pixel_w pixelsHigh:window->pixel_h bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace bitmapFormat:0 bytesPerRow:window->pixel_stride bitsPerPixel:32];
    if (rep == nil) {
        return;
    }
    image = [[NSImage alloc] initWithSize:NSMakeSize(window->pixel_w, window->pixel_h)];
    [image addRepresentation:rep];
    [image drawInRect:self.bounds fromRect:NSZeroRect operation:NSCompositingOperationCopy fraction:1.0 respectFlipped:YES hints:nil];
    [image release];
    [rep release];
}
@end

@implementation AuraDelegate
- (BOOL)windowShouldClose:(NSWindow *)sender {
    (void)sender;
    if (self.owner != NULL) {
        self.owner->should_close = 1;
    }
    return YES;
}
- (void)windowDidBecomeKey:(NSNotification *)notification {
    (void)notification;
    if (self.owner != NULL) {
        aura_push(self.owner, AURA_WINDOW_FOCUS, 0, 0, 0, 0);
    }
}
- (void)windowDidResignKey:(NSNotification *)notification {
    (void)notification;
    if (self.owner != NULL) {
        aura_push(self.owner, AURA_POINTER_CANCEL, 0, 0, 0, 0);
    }
}
@end

int64_t aura_window_create(int64_t width, int64_t height, const char *title, int64_t visible) {
    @autoreleasepool {
        ensure_app();
        int slot = -1;
        int i = 0;
        for (i = 0; i < AURA_MAX_WINDOWS; i++) {
            if (g_windows[i] == NULL) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            return 0;
        }
        AuraWindow *window = calloc(1, sizeof(AuraWindow));
        if (window == NULL) {
            return 0;
        }
        window->alive = 1;
        window->vsync = dispatch_semaphore_create(0);
        NSRect content = NSMakeRect(0, 0, (CGFloat)width, (CGFloat)height);
        NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        window->window = [[NSWindow alloc] initWithContentRect:content styleMask:style backing:NSBackingStoreBuffered defer:NO];
        if (window->window == nil) {
            if (window->vsync != NULL) {
                dispatch_release(window->vsync);
            }
            free(window);
            return 0;
        }
        [window->window setReleasedWhenClosed:NO];
        NSString *ns_title = @"Aura";
        if (title != NULL) {
            NSString *converted = [NSString stringWithUTF8String:title];
            if (converted != nil) {
                ns_title = converted;
            }
        }
        [window->window setTitle:ns_title];
        window->view = [[AuraView alloc] initWithFrame:content];
        window->view.owner = window;
        [window->view registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
        [window->window setContentView:window->view];
        window->delegate = [AuraDelegate new];
        window->delegate.owner = window;
        [window->window setDelegate:window->delegate];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        if (CVDisplayLinkCreateWithActiveCGDisplays(&window->link) == kCVReturnSuccess && window->link != NULL) {
            CVDisplayLinkSetOutputCallback(window->link, aura_link_callback, window);
            CVDisplayLinkStart(window->link);
        } else {
            window->link = NULL;
        }
#pragma clang diagnostic pop
        g_windows[slot] = window;
        if (visible != 0) {
            [window->window center];
            [window->window makeKeyAndOrderFront:nil];
            [window->window makeFirstResponder:window->view];
            [NSApp activate];
        }
        return (int64_t)slot + 1;
    }
}

int64_t aura_window_destroy(int64_t handle) {
    @autoreleasepool {
        if (handle <= 0 || handle > AURA_MAX_WINDOWS) {
            return -1;
        }
        AuraWindow *window = g_windows[handle - 1];
        if (window == NULL) {
            return 0;
        }
        if (!window->alive) {
            return 0;
        }
        window->alive = 0;
        if (window->ax_nodes != NULL) {
            [(NSArray *)window->ax_nodes release];
            window->ax_nodes = NULL;
        }
        if (window->link != NULL) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            CVDisplayLinkStop(window->link);
            CVDisplayLinkRelease(window->link);
#pragma clang diagnostic pop
            window->link = NULL;
        }
        if (window->vsync != NULL) {
            dispatch_semaphore_signal(window->vsync);
        }
        [window->window setDelegate:nil];
        [window->window orderOut:nil];
        int plugin_i = 0;
        for (plugin_i = 0; plugin_i < 8; plugin_i++) {
            if (window->plugin_views[plugin_i] != nil) {
                [window->plugin_views[plugin_i] removeFromSuperview];
                [window->plugin_views[plugin_i] release];
                window->plugin_views[plugin_i] = nil;
            }
        }
        [window->view release];
        [window->delegate release];
        [window->window release];
        window->window = nil;
        window->view = nil;
        window->delegate = nil;
        free(window->pixels);
        aura_free_event_text(window);
        free(window->events);
        if (window->marked != nil) {
            [window->marked release];
        }
        if (window->contents != nil) {
            [window->contents release];
        }
        if (window->vsync != NULL) {
            dispatch_release(window->vsync);
        }
        g_windows[handle - 1] = NULL;
        free(window);
        return 0;
    }
}

int64_t aura_window_poll(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    @autoreleasepool {
        for (;;) {
            NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];
            if (event == nil) {
                break;
            }
            [NSApp sendEvent:event];
        }
    }
    return window->event_count;
}

int64_t aura_window_event_count(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return window->event_count;
}

int64_t aura_window_event_kind(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || index < 0 || index >= window->event_count) {
        return 0;
    }
    return window->events[index].kind;
}

double aura_window_event_x(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || index < 0 || index >= window->event_count) {
        return 0;
    }
    return window->events[index].x;
}

double aura_window_event_y(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || index < 0 || index >= window->event_count) {
        return 0;
    }
    return window->events[index].y;
}

int64_t aura_window_event_key(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || index < 0 || index >= window->event_count) {
        return 0;
    }
    return window->events[index].key;
}

int64_t aura_window_event_button(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || index < 0 || index >= window->event_count) {
        return 0;
    }
    return window->events[index].button;
}

int64_t aura_window_events_clear(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    aura_free_event_text(window);
    window->event_count = 0;
    return 0;
}

const char *aura_window_event_text(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || index < 0 || index >= window->event_count || window->events[index].text == NULL) {
        return "";
    }
    return window->events[index].text;
}

int64_t aura_window_event_text_len(int64_t handle, int64_t index) {
    const char *text = aura_window_event_text(handle, index);
    return (int64_t)strlen(text);
}

int64_t aura_window_event_text_byte(int64_t handle, int64_t index, int64_t offset) {
    const char *text = aura_window_event_text(handle, index);
    if (offset < 0 || offset >= (int64_t)strlen(text)) {
        return 0;
    }
    return (unsigned char)text[offset];
}

int64_t aura_text_focus(int64_t handle, int64_t enabled) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    window->text_focus = enabled != 0;
    return 0;
}

int64_t aura_text_selection(int64_t handle, int64_t start, int64_t length) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    if (start < 0) {
        start = 0;
    }
    if (length < 0) {
        length = 0;
    }
    window->selected = NSMakeRange((NSUInteger)start, (NSUInteger)length);
    return 0;
}

int64_t aura_text_contents(int64_t handle, const char *utf8) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    if (utf8 == NULL) {
        utf8 = "";
    }
    NSString *next = [[NSString alloc] initWithUTF8String:utf8];
    if (next == nil) {
        next = @"";
    }
    if (window->contents != nil) {
        [window->contents release];
    }
    window->contents = next;
    return 0;
}

int64_t aura_text_caret(int64_t handle, double x, double y, double height) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    window->caret_x = x;
    window->caret_y = y;
    window->caret_h = height;
    return 0;
}

int64_t aura_post_text(int64_t handle, const char *utf8) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    aura_push_text(window, 10, utf8);
    return 0;
}

int64_t aura_window_should_close(int64_t handle) {
    AuraWindow *window = window_get(handle, 0);
    if (window == NULL || !window->alive) {
        return 1;
    }
    return window->should_close;
}

double aura_window_width(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1.0;
    }
    return window->view.bounds.size.width;
}

double aura_window_height(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1.0;
    }
    return window->view.bounds.size.height;
}

double aura_window_scale(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1.0;
    }
    return window->window.backingScaleFactor;
}

double aura_frame_timestamp(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1.0;
    }
    return aura_host_seconds(window->frame_host);
}

int64_t aura_window_wait_frame(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    if (window->vsync == NULL) {
        return 0;
    }
    dispatch_semaphore_wait(window->vsync, dispatch_time(DISPATCH_TIME_NOW, 500000000LL));
    return 0;
}

int64_t aura_frame_begin(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    double scale = 1.0;
    int64_t width = 0;
    int64_t height = 0;
    if (window == NULL) {
        return -1;
    }
    scale = window->window.backingScaleFactor;
    if (scale < 1.0) {
        scale = 1.0;
    }
    window->scale = scale;
    width = (int64_t)(window->window.contentView.bounds.size.width * scale);
    height = (int64_t)(window->window.contentView.bounds.size.height * scale);
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }
    if (aura_paint_begin(width, height, scale) < 0) {
        return -1;
    }
    return 0;
}

int64_t aura_frame_end(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    if (aura_paint_end() < 0) {
        return -1;
    }
    copy_frame_pixels(window);
    [window->view setNeedsDisplay:YES];
    [window->view display];
    return 0;
}

int64_t aura_cmd_clear(int64_t handle, int64_t argb) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_clear(argb);
}

int64_t aura_cmd_rect(int64_t handle, double x, double y, double w, double h, int64_t argb) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_rect(x, y, w, h, argb);
}

int64_t aura_cmd_text(int64_t handle, double x, double y, const char *text, double size, int64_t weight, int64_t argb, const char *family) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_text(x, y, text, size, weight, argb, family);
}

int64_t aura_cmd_opacity(int64_t handle, double opacity) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_opacity(opacity);
}

int64_t aura_cmd_opacity_pop(int64_t handle) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_opacity_pop();
}

int64_t aura_sample(int64_t handle, double x, double y) {
    AuraWindow *window = window_get(handle, 1);
    int px = 0;
    int py = 0;
    uint8_t *pixel = NULL;
    int a = 0;
    int r = 0;
    int g = 0;
    int b = 0;
    if (window == NULL || window->pixels == NULL) {
        return -1;
    }
    px = (int)(x * window->scale);
    py = (int)(y * window->scale);
    if (px < 0 || py < 0 || px >= window->pixel_w || py >= window->pixel_h) {
        return -1;
    }
    pixel = window->pixels + (size_t)py * (size_t)window->pixel_stride + (size_t)px * 4;
    r = pixel[0];
    g = pixel[1];
    b = pixel[2];
    a = pixel[3];
    if (a > 0 && a < 255) {
        r = r * 255 / a;
        g = g * 255 / a;
        b = b * 255 / a;
    }
    return ((int64_t)a << 24) | ((int64_t)r << 16) | ((int64_t)g << 8) | (int64_t)b;
}

int64_t aura_post_drop(int64_t handle, double x, double y, const char *path) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    aura_push_text(window, AURA_FILE_DROP, path);
    if (window->event_count > 0) {
        window->events[window->event_count - 1].x = x;
        window->events[window->event_count - 1].y = y;
    }
    return 0;
}

int64_t aura_post_mouse(int64_t handle, int64_t kind, double x, double y) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    aura_push(window, (int)kind, x, y, 0, 1);
    return 0;
}

static char *g_clipboard = NULL;

int64_t aura_clipboard_set(const char *text) {
    @autoreleasepool {
        ensure_app();
        if (text == NULL) {
            text = "";
        }
        NSString *value = [[NSString alloc] initWithBytes:text length:strlen(text) encoding:NSUTF8StringEncoding];
        if (value == nil) {
            value = @"";
        }
        NSPasteboard *board = [NSPasteboard generalPasteboard];
        [board clearContents];
        if (![board setString:value forType:NSPasteboardTypeString]) {
            return -1;
        }
        return 0;
    }
}

int64_t aura_clipboard_len(void) {
    @autoreleasepool {
        ensure_app();
        free(g_clipboard);
        g_clipboard = NULL;
        NSString *value = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
        if (value == nil) {
            g_clipboard = strdup("");
            return 0;
        }
        const char *utf8 = [value UTF8String];
        if (utf8 == NULL) {
            g_clipboard = strdup("");
            return 0;
        }
        g_clipboard = strdup(utf8);
        if (g_clipboard == NULL) {
            return 0;
        }
        return (int64_t)strlen(g_clipboard);
    }
}

int64_t aura_clipboard_byte(int64_t offset) {
    if (g_clipboard == NULL || offset < 0 || offset >= (int64_t)strlen(g_clipboard)) {
        return 0;
    }
    return (unsigned char)g_clipboard[offset];
}

int64_t aura_cmd_image(int64_t handle, int64_t image, double x, double y, double w, double h) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_image(image, x, y, w, h);
}

int64_t aura_plugin_place(int64_t handle, int64_t plugin, double x, double y, double w, double h) {
    AuraWindow *window = window_get(handle, 1);
    int slot = -1;
    int i = 0;
    NSView *view = nil;
    if (window == NULL) {
        return -1;
    }
    if (aura_plugin_check(plugin) < 0) {
        return -1;
    }
    for (i = 0; i < 8; i++) {
        if (window->plugin_ids[i] == plugin) {
            slot = i;
            break;
        }
        if (slot < 0 && window->plugin_ids[i] == 0) {
            slot = i;
        }
    }
    if (slot < 0) {
        return -1;
    }
    view = window->plugin_views[slot];
    if (view == nil) {
        view = [[NSView alloc] initWithFrame:NSMakeRect(x, y, w, h)];
        window->plugin_views[slot] = view;
        window->plugin_ids[slot] = plugin;
        [window->view addSubview:view];
    } else {
        [view setFrame:NSMakeRect(x, y, w, h)];
    }
    return aura_plugin_frame(plugin, (int64_t)w, (int64_t)h);
}

static NSString *ax_string(const char *text) {
    NSString *value = nil;
    if (text == NULL) {
        return @"";
    }
    value = [[NSString alloc] initWithBytes:text length:strlen(text) encoding:NSUTF8StringEncoding];
    if (value == nil) {
        return @"";
    }
    return value;
}

static NSString *ax_role(const char *role) {
    if (role != NULL && strcmp(role, "button") == 0) {
        return NSAccessibilityButtonRole;
    }
    if (role != NULL && strcmp(role, "toggle") == 0) {
        return NSAccessibilityCheckBoxRole;
    }
    if (role != NULL && strcmp(role, "text") == 0) {
        return NSAccessibilityStaticTextRole;
    }
    if (role != NULL && strcmp(role, "text-field") == 0) {
        return NSAccessibilityTextFieldRole;
    }
    return NSAccessibilityGroupRole;
}

static AuraAXNode *ax_node(AuraWindow *window, int64_t index) {
    NSArray *nodes = NULL;
    if (window == NULL || window->ax_nodes == NULL || index < 0) {
        return nil;
    }
    nodes = (NSArray *)window->ax_nodes;
    if (index >= (int64_t)nodes.count) {
        return nil;
    }
    return nodes[(NSUInteger)index];
}

int64_t aura_ax_begin(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    if (window->ax_nodes != NULL) {
        [(NSArray *)window->ax_nodes release];
    }
    window->ax_nodes = [NSMutableArray new];
    window->ax_count = 0;
    return 0;
}

int64_t aura_ax_add(int64_t handle, const char *role, const char *label, const char *value, int64_t checked, double x, double y, double w, double h, int64_t press) {
    AuraWindow *window = window_get(handle, 1);
    AuraAXNode *node = nil;
    if (window == NULL || window->ax_nodes == NULL) {
        return -1;
    }
    node = [AuraAXNode new];
    node.owner = window;
    node.action = press;
    NSString *role_text = ax_string(role);
    NSString *label_text = ax_string(label);
    NSString *value_text = ax_string(value);
    node.roleKey = role_text;
    node.roleName = ax_role(role);
    node.labelText = label_text;
    node.valueText = value_text;
    if (role_text != nil && role_text.length > 0) {
        [role_text release];
    }
    if (label_text != nil && label_text.length > 0) {
        [label_text release];
    }
    if (value_text != nil && value_text.length > 0) {
        [value_text release];
    }
    node.checked = checked != 0 ? 1 : 0;
    node.localFrame = NSMakeRect(x, y, w, h);
    [(NSMutableArray *)window->ax_nodes addObject:node];
    [node release];
    window->ax_count += 1;
    return 0;
}

int64_t aura_ax_commit(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return window->ax_count;
}

int64_t aura_ax_count(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return window->ax_count;
}

static char *g_ax_query = NULL;

int64_t aura_ax_text_len(int64_t handle, int64_t index, int64_t field) {
    AuraWindow *window = window_get(handle, 1);
    AuraAXNode *node = NULL;
    NSString *text = @"";
    const char *utf8 = NULL;
    if (window == NULL) {
        return -1;
    }
    node = ax_node(window, index);
    if (node == nil) {
        return -1;
    }
    if (field == 0) {
        text = node.roleKey;
    } else if (field == 1) {
        text = node.labelText;
    } else {
        text = node.valueText;
    }
    if (text == nil) {
        text = @"";
    }
    utf8 = [text UTF8String];
    free(g_ax_query);
    g_ax_query = NULL;
    if (utf8 == NULL) {
        return 0;
    }
    g_ax_query = strdup(utf8);
    if (g_ax_query == NULL) {
        return 0;
    }
    return (int64_t)strlen(g_ax_query);
}

int64_t aura_ax_text_byte(int64_t offset) {
    if (g_ax_query == NULL || offset < 0 || offset >= (int64_t)strlen(g_ax_query)) {
        return 0;
    }
    return (unsigned char)g_ax_query[offset];
}

int64_t aura_ax_checked(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    AuraAXNode *node = NULL;
    if (window == NULL) {
        return -1;
    }
    node = ax_node(window, index);
    if (node == nil) {
        return -1;
    }
    return node.checked;
}

int64_t aura_ax_press(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    AuraAXNode *node = NULL;
    if (window == NULL) {
        return -1;
    }
    node = ax_node(window, index);
    if (node == nil) {
        return -1;
    }
    return node.action;
}

double aura_ax_x(int64_t handle, int64_t index) {
    AuraAXNode *node = ax_node(window_get(handle, 1), index);
    if (node == nil) {
        return -1.0;
    }
    return node.localFrame.origin.x;
}

double aura_ax_y(int64_t handle, int64_t index) {
    AuraAXNode *node = ax_node(window_get(handle, 1), index);
    if (node == nil) {
        return -1.0;
    }
    return node.localFrame.origin.y;
}

double aura_ax_w(int64_t handle, int64_t index) {
    AuraAXNode *node = ax_node(window_get(handle, 1), index);
    if (node == nil) {
        return -1.0;
    }
    return node.localFrame.size.width;
}

double aura_ax_h(int64_t handle, int64_t index) {
    AuraAXNode *node = ax_node(window_get(handle, 1), index);
    if (node == nil) {
        return -1.0;
    }
    return node.localFrame.size.height;
}

int64_t aura_post_ax(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    aura_push(window, AURA_AX_ACTION, 0, 0, index, 0);
    return 0;
}

int64_t aura_set_cursor(int64_t handle, int64_t kind) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    if (kind == 1) {
        [[NSCursor IBeamCursor] set];
    } else {
        [[NSCursor arrowCursor] set];
    }
    return 0;
}
