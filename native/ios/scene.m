#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <os/log.h>

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "app_runtime.h"
#include "ax_platform.h"
#include "events.h"
#include "shell.h"
#include "storage.h"
#include "text_units.h"

static NSString *const kAuraSceneActivity = @"com.noxlang.aura.scene";
static NSString *const kAuraSceneFormat = @"AURA-RS";

@class AuraCanvas;

static AuraCanvas *g_canvas[4];
static UIWindow *g_window;
static char *g_clip;
static int g_nox_started = 0;
static int g_surface_generation = 0;

static uint64_t aura_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void aura_post_platform_route(NSString *location, int source) {
    const char *utf8 = location.UTF8String;
    uint64_t timestamp = aura_now_ns();
    if (utf8 == NULL || utf8[0] == 0) {
        return;
    }
    aura_shell_platform_route(0, source, utf8, timestamp);
}

static void aura_runtime_log(int is_error, const char *message) {
    if (is_error) {
        os_log_error(OS_LOG_DEFAULT, "aura-runtime %{public}s", message != NULL ? message : "error");
    } else {
        os_log(OS_LOG_DEFAULT, "aura-runtime %{public}s", message != NULL ? message : "ok");
    }
}

static void aura_post_lifecycle_to_open_canvases(int state, int reason) {
    uint64_t timestamp = aura_now_ns();
    int i = 0;
    while (i < 4) {
        if (g_canvas[i] != nil) {
            aura_shell_lifecycle(i, state, reason, timestamp);
        }
        i += 1;
    }
}

static void aura_pixels_free(void *info, const void *data, size_t size) {
    (void)info;
    (void)size;
    free((void *)data);
}

typedef AuraTextBoundary AuraLayoutBoundary;

typedef struct AuraLayoutSnapshot {
    pthread_mutex_t mu;
    AuraLayoutBoundary *items;
    int count;
    int64_t revision;
    int64_t target;
} AuraLayoutSnapshot;

static AuraLayoutSnapshot g_layout[4] = {
    {PTHREAD_MUTEX_INITIALIZER, NULL, 0, 0, 0},
    {PTHREAD_MUTEX_INITIALIZER, NULL, 0, 0, 0},
    {PTHREAD_MUTEX_INITIALIZER, NULL, 0, 0, 0},
    {PTHREAD_MUTEX_INITIALIZER, NULL, 0, 0, 0}
};

static size_t g_clip_length = 0;

static int aura_keyboard_curve(int raw) {
    if (raw == UIViewAnimationCurveLinear) return AURA_KEYBOARD_CURVE_LINEAR;
    if (raw == UIViewAnimationCurveEaseIn) return AURA_KEYBOARD_CURVE_EASE_IN;
    if (raw == UIViewAnimationCurveEaseOut) return AURA_KEYBOARD_CURVE_EASE_OUT;
    if (raw == UIViewAnimationCurveEaseInOut) return AURA_KEYBOARD_CURVE_EASE_IN_OUT;
    return AURA_KEYBOARD_CURVE_UNSPECIFIED;
}

static int aura_rotation_degrees(UIInterfaceOrientation orientation) {
    if (orientation == UIInterfaceOrientationLandscapeRight) return 90;
    if (orientation == UIInterfaceOrientationPortraitUpsideDown) return 180;
    if (orientation == UIInterfaceOrientationLandscapeLeft) return 270;
    return 0;
}

static double aura_dynamic_type_scale(void) {
    UIFont *font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    if (font.pointSize <= 0.0) return 1.0;
    return font.pointSize / 17.0;
}

@interface AuraTextPosition : UITextPosition
@property (nonatomic, assign) NSInteger offset;
+ (instancetype)positionWithOffset:(NSInteger)offset;
@end

@implementation AuraTextPosition
+ (instancetype)positionWithOffset:(NSInteger)offset {
    AuraTextPosition *position = [[[AuraTextPosition alloc] init] autorelease];
    position.offset = offset;
    return position;
}
@end

@interface AuraTextRange : UITextRange
@property (nonatomic, retain) AuraTextPosition *rangeStart;
@property (nonatomic, retain) AuraTextPosition *rangeEnd;
+ (instancetype)rangeWithStart:(NSInteger)start end:(NSInteger)end;
@end

@implementation AuraTextRange
+ (instancetype)rangeWithStart:(NSInteger)start end:(NSInteger)end {
    AuraTextRange *range = [[[AuraTextRange alloc] init] autorelease];
    range.rangeStart = [AuraTextPosition positionWithOffset:start];
    range.rangeEnd = [AuraTextPosition positionWithOffset:end];
    return range;
}
- (UITextPosition *)start { return self.rangeStart; }
- (UITextPosition *)end { return self.rangeEnd; }
- (BOOL)isEmpty { return self.rangeStart.offset == self.rangeEnd.offset; }
- (void)dealloc {
    [_rangeStart release];
    [_rangeEnd release];
    [super dealloc];
}
@end

@interface AuraSelectionRect : UITextSelectionRect
@property (nonatomic, assign) CGRect storedRect;
@property (nonatomic, assign) BOOL storedStart;
@property (nonatomic, assign) BOOL storedEnd;
@end

@implementation AuraSelectionRect
- (CGRect)rect { return self.storedRect; }
- (NSWritingDirection)writingDirection { return NSWritingDirectionNatural; }
- (BOOL)containsStart { return self.storedStart; }
- (BOOL)containsEnd { return self.storedEnd; }
- (BOOL)isVertical { return NO; }
@end

@interface AuraAccessibilityElement : UIAccessibilityElement
@property (nonatomic, assign) int slot;
@property (nonatomic, assign) int axIndex;
@property (nonatomic, assign) int64_t nodeId;
@end

@implementation AuraAccessibilityElement

- (BOOL)accessibilityActivate {
    return aura_shell_activate_ax(self.slot, self.axIndex) == 1;
}

- (void)accessibilityIncrement {
    aura_shell_ax_adjust(self.slot, self.axIndex, 1);
}

- (void)accessibilityDecrement {
    aura_shell_ax_adjust(self.slot, self.axIndex, -1);
}

@end

@interface AuraWordElement : UIAccessibilityElement
@property (nonatomic, assign) NSInteger codeStart;
@property (nonatomic, assign) NSInteger codeEnd;
@end

@class AuraTextInputView;

@interface AuraCanvas : UIView {
    NSMutableDictionary *_touchIds;
    int64_t _nextPointerId;
}
@property (nonatomic, assign) int slot;
@property (nonatomic, assign) UIEdgeInsets safe;
@property (nonatomic, assign) CGFloat keyboard;
@property (nonatomic, assign) CGFloat keyboardDuration;
@property (nonatomic, assign) int keyboardCurve;
@property (nonatomic, copy) NSString *environmentSignature;
@property (nonatomic, retain) NSMutableArray *preferenceObservers;
@property (nonatomic, retain) id keyboardObserver;
@property (nonatomic, strong) CADisplayLink *link;
@property (nonatomic, assign) int frames;
@property (nonatomic, assign) CGFloat loggedW;
@property (nonatomic, assign) CGFloat loggedH;
@property (nonatomic, assign) int logFrame;
@property (nonatomic, assign) int surfaceGeneration;
@property (nonatomic, retain) NSString *editingText;
@property (nonatomic, assign) int64_t textGeneration;
@property (nonatomic, assign) int64_t textRevision;
@property (nonatomic, assign) int64_t selectionStart;
@property (nonatomic, assign) int64_t selectionEnd;
@property (nonatomic, assign) int64_t composingStart;
@property (nonatomic, assign) int64_t composingEnd;
@property (nonatomic, assign) CGRect caretRect;
@property (nonatomic, assign) int64_t caretRevision;
@property (nonatomic, assign) int caretRefreshDepth;
@property (nonatomic, assign) CGRect editableRect;
@property (nonatomic, assign) CGRect textContentRect;
@property (nonatomic, assign) BOOL textContentValid;
@property (nonatomic, assign) CGPoint selectionTouchOrigin;
@property (nonatomic, assign) NSInteger selectionTouchAnchor;
@property (nonatomic, assign) NSInteger selectionTouchGlyphLeft;
@property (nonatomic, assign) NSInteger selectionTouchGlyphRight;
@property (nonatomic, assign) BOOL selectionTouchActive;
@property (nonatomic, assign) int inputTypeConfig;
@property (nonatomic, assign) int actionConfig;
@property (nonatomic, assign) BOOL multilineConfig;
@property (nonatomic, assign) BOOL secureConfig;
@property (nonatomic, assign) BOOL autocorrectConfig;
@property (nonatomic, assign) int capitalizationConfig;
@property (nonatomic, retain) NSString *autofillHints;
@property (nonatomic, copy) NSString *inputConfigurationSignature;
@property (nonatomic, assign) BOOL caretAnnounceValid;
@property (nonatomic, assign) int64_t caretAnnounceStart;
@property (nonatomic, assign) int64_t caretAnnounceEnd;
@property (nonatomic, assign) int64_t caretAnnounceRevision;
@property (nonatomic, assign) CGFloat caretAnnounceX;
@property (nonatomic, assign) CGFloat caretAnnounceY;
@property (nonatomic, retain) NSMutableArray *axElements;
@property (nonatomic, retain) NSArray *wordElements;
@property (nonatomic, retain) UIAccessibilityCustomRotor *headingRotor;
@property (nonatomic, assign) int64_t wordElementTextRevision;
@property (nonatomic, assign) int64_t wordElementLayoutRevision;
@property (nonatomic, retain) AuraTextInputView *textInput;
- (CGRect)screenRectForLocal:(CGRect)local;
- (void)refreshTextContentRect;
- (BOOL)visualLineForUTF16:(NSInteger)index start:(NSInteger *)start end:(NSInteger *)end;
- (UITextPosition *)closestPositionToPoint:(CGPoint)point;
- (UITextPosition *)closestPositionToPoint:(CGPoint)point withinRange:(UITextRange *)range;
- (UITextRange *)characterRangeAtPoint:(CGPoint)point;
- (CGRect)caretRectForPosition:(UITextPosition *)position;
- (CGRect)firstRectForRange:(UITextRange *)range;
- (NSArray<UITextSelectionRect *> *)selectionRectsForRange:(UITextRange *)range;
@end

@interface AuraTextInputView : UIView <UITextInput>
@property (nonatomic, assign) AuraCanvas *canvas;
@property (nonatomic, assign) int axIndex;
@property (nonatomic, assign) BOOL accessibilityActive;
@property (nonatomic, assign) BOOL voiceOverEditing;
@property (nonatomic, copy) NSString *axName;
@property (nonatomic, copy) NSString *axValue;
@property (nonatomic, assign) id<UITextInputDelegate> inputDelegate;
@property (nonatomic, retain) id<UITextInputTokenizer> tokenizer;
@property (nonatomic, copy) NSDictionary *markedTextStyle;
@property (nonatomic, retain) UIView *dismissAccessory;
- (instancetype)initWithCanvas:(AuraCanvas *)canvas;
- (void)dismissKeyboard;
@end

@implementation AuraWordElement

- (BOOL)accessibilityActivate {
    AuraCanvas *canvas = (AuraCanvas *)self.accessibilityContainer;
    uint64_t timestamp = aura_now_ns();
    if (![canvas isKindOfClass:[AuraCanvas class]] || canvas.textInput == nil) {
        return NO;
    }
    canvas.textInput.voiceOverEditing = YES;
    canvas.textInput.accessibilityActive = YES;
    [canvas.textInput becomeFirstResponder];
    aura_shell_activate_ax(canvas.slot, canvas.textInput.axIndex);
    aura_shell_text_edit(canvas.slot, AURA_TEXT_EDIT_SET_SELECTION, "", self.codeStart, self.codeEnd, timestamp);
    UIAccessibilityPostNotification(UIAccessibilityScreenChangedNotification, canvas.textInput);
    return YES;
}

@end

@interface AuraCanvas (TextForward)
- (BOOL)hasText;
- (void)insertText:(NSString *)text;
- (void)deleteBackward;
- (UIKeyboardType)keyboardType;
- (UIReturnKeyType)returnKeyType;
- (BOOL)isSecureTextEntry;
- (UITextAutocorrectionType)autocorrectionType;
- (UITextAutocapitalizationType)autocapitalizationType;
- (UITextContentType)textContentType;
- (UITextRange *)selectedTextRange;
- (void)setSelectedTextRange:(UITextRange *)range;
- (UITextRange *)markedTextRange;
- (void)setMarkedText:(NSString *)markedText selectedRange:(NSRange)selectedRange;
- (void)unmarkText;
- (NSString *)textInRange:(UITextRange *)range;
- (void)replaceRange:(UITextRange *)range withText:(NSString *)text;
- (UITextPosition *)beginningOfDocument;
- (UITextPosition *)endOfDocument;
- (UITextRange *)textRangeFromPosition:(UITextPosition *)fromPosition toPosition:(UITextPosition *)toPosition;
- (UITextPosition *)positionFromPosition:(UITextPosition *)position offset:(NSInteger)offset;
- (UITextPosition *)positionFromPosition:(UITextPosition *)position inDirection:(UITextLayoutDirection)direction offset:(NSInteger)offset;
- (NSComparisonResult)comparePosition:(UITextPosition *)position toPosition:(UITextPosition *)other;
- (NSInteger)offsetFromPosition:(UITextPosition *)from toPosition:(UITextPosition *)toPosition;
- (UITextPosition *)positionWithinRange:(UITextRange *)range farthestInDirection:(UITextLayoutDirection)direction;
- (UITextRange *)characterRangeByExtendingPosition:(UITextPosition *)position inDirection:(UITextLayoutDirection)direction;
- (NSWritingDirection)baseWritingDirectionForPosition:(UITextPosition *)position inDirection:(UITextStorageDirection)direction;
- (void)setBaseWritingDirection:(NSWritingDirection)writingDirection forRange:(UITextRange *)range;
- (BOOL)textInputIsLeftToRight;
@end

static int aura_ns_is_space(void *context, int64_t index) {
    NSString *text = (NSString *)context;
    if (index < 0 || index >= (int64_t)text.length) return 0;
    return [[NSCharacterSet whitespaceAndNewlineCharacterSet] characterIsMember:[text characterAtIndex:(NSUInteger)index]] ? 1 : 0;
}

static int64_t aura_ns_step(void *context, int64_t index, int forward) {
    NSString *text = (NSString *)context;
    if (forward) {
        if (index < 0 || index >= (int64_t)text.length) return AURA_TEXT_NONE;
        return (int64_t)NSMaxRange([text rangeOfComposedCharacterSequenceAtIndex:(NSUInteger)index]);
    }
    if (index <= 0 || index > (int64_t)text.length) return AURA_TEXT_NONE;
    return (int64_t)[text rangeOfComposedCharacterSequenceAtIndex:(NSUInteger)index - 1].location;
}

static AuraTextCursor aura_ns_cursor(NSString *text) {
    AuraTextCursor cursor;
    cursor.context = text != nil ? text : @"";
    cursor.length = (int64_t)((NSString *)cursor.context).length;
    cursor.is_space = aura_ns_is_space;
    cursor.step = aura_ns_step;
    return cursor;
}

static BOOL aura_direction_moves_forward(UITextGranularity granularity, UITextDirection direction, BOOL left_to_right) {
    return aura_text_direction_forward((int)granularity, (int64_t)direction, left_to_right ? 1 : 0) != 0;
}

@interface AuraTextTokenizer : NSObject <UITextInputTokenizer>
@property (nonatomic, assign) AuraTextInputView *textInput;
- (instancetype)initWithTextInput:(AuraTextInputView *)textInput;
@end

@implementation AuraTextTokenizer

- (instancetype)initWithTextInput:(AuraTextInputView *)textInput {
    self = [super init];
    if (self == nil) return nil;
    self.textInput = textInput;
    return self;
}

- (NSString *)documentText {
    NSString *text = self.textInput.canvas.editingText;
    return text != nil ? text : @"";
}

- (BOOL)movesForward:(UITextDirection)direction granularity:(UITextGranularity)granularity {
    return aura_direction_moves_forward(granularity, direction, [self.textInput.canvas textInputIsLeftToRight]);
}

- (NSInteger)boundaryFrom:(NSInteger)index granularity:(UITextGranularity)granularity forward:(BOOL)forward text:(NSString *)text {
    AuraTextCursor cursor = aura_ns_cursor(text);
    NSInteger length = (NSInteger)text.length;
    NSInteger lineStart = 0;
    NSInteger lineEnd = 0;
    if (granularity == UITextGranularityCharacter) {
        if (forward) {
            if (index >= length) return NSNotFound;
            return (NSInteger)NSMaxRange([text rangeOfComposedCharacterSequenceAtIndex:(NSUInteger)index]);
        }
        if (index <= 0) return NSNotFound;
        return (NSInteger)[text rangeOfComposedCharacterSequenceAtIndex:(NSUInteger)index - 1].location;
    }
    if (granularity == UITextGranularityLine || granularity == UITextGranularityParagraph) {
        if (self.textInput.canvas != nil && [self.textInput.canvas visualLineForUTF16:index start:&lineStart end:&lineEnd]) {
            if (forward) {
                if (index < lineEnd) return lineEnd;
                if (lineEnd >= length) return NSNotFound;
                if (![self.textInput.canvas visualLineForUTF16:lineEnd start:&lineStart end:&lineEnd]) return NSNotFound;
                return lineEnd;
            }
            if (index > lineStart) return lineStart;
            if (lineStart <= 0) return NSNotFound;
            if (![self.textInput.canvas visualLineForUTF16:lineStart - 1 start:&lineStart end:&lineEnd]) return NSNotFound;
            return lineStart;
        }
        return NSNotFound;
    }
    if (granularity == UITextGranularityDocument) {
        if (forward) return index < length ? length : NSNotFound;
        return index > 0 ? 0 : NSNotFound;
    }
    if (forward) {
        int64_t next = aura_text_next_word_start(&cursor, (int64_t)index);
        return next == AURA_TEXT_NONE ? NSNotFound : (NSInteger)next;
    }
    {
        int64_t previous = aura_text_previous_word_start(&cursor, (int64_t)index);
        return previous == AURA_TEXT_NONE ? NSNotFound : (NSInteger)previous;
    }
}

- (UITextRange *)rangeEnclosingPosition:(UITextPosition *)position withGranularity:(UITextGranularity)granularity inDirection:(UITextDirection)direction {
    NSString *text = [self documentText];
    AuraTextCursor cursor;
    AuraTextSpan span;
    NSInteger index = 0;
    NSInteger start = 0;
    NSInteger end = 0;
    (void)direction;
    if (![position isKindOfClass:[AuraTextPosition class]]) return nil;
    index = ((AuraTextPosition *)position).offset;
    if (text.length == 0) return nil;
    if (index < 0) index = 0;
    if (index >= (NSInteger)text.length) index = (NSInteger)text.length - 1;
    if (granularity == UITextGranularityCharacter) {
        NSRange sequence = [text rangeOfComposedCharacterSequenceAtIndex:(NSUInteger)index];
        return [AuraTextRange rangeWithStart:(NSInteger)sequence.location end:(NSInteger)NSMaxRange(sequence)];
    }
    if (granularity == UITextGranularityLine || granularity == UITextGranularityParagraph) {
        if (self.textInput.canvas != nil && [self.textInput.canvas visualLineForUTF16:index start:&start end:&end] && end > start) {
            return [AuraTextRange rangeWithStart:start end:end];
        }
        return nil;
    }
    if (granularity == UITextGranularityDocument) {
        return [AuraTextRange rangeWithStart:0 end:(NSInteger)text.length];
    }
    cursor = aura_ns_cursor(text);
    if (!aura_text_word_at(&cursor, (int64_t)index, &span) || span.end <= span.start) return nil;
    return [AuraTextRange rangeWithStart:(NSInteger)span.start end:(NSInteger)span.end];
}

- (BOOL)isPosition:(UITextPosition *)position atBoundary:(UITextGranularity)granularity inDirection:(UITextDirection)direction {
    NSString *text = [self documentText];
    NSInteger index = 0;
    NSInteger boundary = 0;
    BOOL forward = [self movesForward:direction granularity:granularity];
    if (![position isKindOfClass:[AuraTextPosition class]]) return NO;
    index = ((AuraTextPosition *)position).offset;
    if (!forward) {
        if (index <= 0) return YES;
        boundary = [self boundaryFrom:index granularity:granularity forward:NO text:text];
        return boundary == index;
    }
    if (index >= (NSInteger)text.length) return YES;
    boundary = [self boundaryFrom:index > 0 ? index - 1 : 0 granularity:granularity forward:YES text:text];
    return boundary == index;
}

- (UITextPosition *)positionFromPosition:(UITextPosition *)position toBoundary:(UITextGranularity)granularity inDirection:(UITextDirection)direction {
    NSString *text = [self documentText];
    NSInteger index = 0;
    NSInteger boundary = 0;
    if (![position isKindOfClass:[AuraTextPosition class]]) return nil;
    index = ((AuraTextPosition *)position).offset;
    boundary = [self boundaryFrom:index granularity:granularity forward:[self movesForward:direction granularity:granularity] text:text];
    if (boundary == NSNotFound) return nil;
    return [AuraTextPosition positionWithOffset:boundary];
}

- (BOOL)isPosition:(UITextPosition *)position withinTextUnit:(UITextGranularity)granularity inDirection:(UITextDirection)direction {
    return [self rangeEnclosingPosition:position withGranularity:granularity inDirection:direction] != nil;
}

@end

@implementation AuraTextInputView

- (instancetype)initWithCanvas:(AuraCanvas *)canvas {
    self = [super initWithFrame:CGRectZero];
    if (self == nil) {
        return nil;
    }
    self.canvas = canvas;
    self.backgroundColor = [UIColor clearColor];
    self.opaque = NO;
    self.userInteractionEnabled = NO;
    self.accessibilityActive = NO;
    self.tokenizer = [[[AuraTextTokenizer alloc] initWithTextInput:self] autorelease];
    self.semanticContentAttribute = UISemanticContentAttributeForceLeftToRight;
    return self;
}

- (void)dealloc {
    [_tokenizer release];
    [_axName release];
    [_axValue release];
    [_markedTextStyle release];
    [_dismissAccessory release];
    [super dealloc];
}

- (BOOL)canBecomeFirstResponder {
    return YES;
}

- (void)dismissKeyboard {
    self.voiceOverEditing = NO;
    [self resignFirstResponder];
}

- (BOOL)accessibilityPerformEscape {
    if (!self.isFirstResponder) {
        return NO;
    }
    [self dismissKeyboard];
    return YES;
}

- (UIView *)inputAccessoryView {
    UIToolbar *bar = nil;
    UIBarButtonItem *space = nil;
    UIBarButtonItem *done = nil;
    if (self.dismissAccessory != nil) {
        return self.dismissAccessory;
    }
    bar = [[UIToolbar alloc] initWithFrame:CGRectMake(0, 0, UIScreen.mainScreen.bounds.size.width, 44)];
    bar.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    space = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace target:nil action:NULL];
    done = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self action:@selector(dismissKeyboard)];
    bar.items = @[space, done];
    [space release];
    [done release];
    self.dismissAccessory = bar;
    [bar release];
    return self.dismissAccessory;
}

- (BOOL)isAccessibilityElement {
    return self.accessibilityActive;
}

- (NSString *)accessibilityLabel {
    if (self.canvas != nil && self.canvas.secureConfig) {
        return self.axName.length > 0 ? self.axName : nil;
    }
    if (self.axValue.length > 0) {
        return nil;
    }
    return self.axName.length > 0 ? self.axName : nil;
}

- (NSString *)accessibilityValue {
    return nil;
}

- (CGRect)accessibilityFrame {
    if (self.canvas != nil && self.canvas.textContentValid) {
        return [self.canvas screenRectForLocal:self.canvas.textContentRect];
    }
    return [super accessibilityFrame];
}

- (BOOL)accessibilityActivate {
    self.voiceOverEditing = YES;
    [self becomeFirstResponder];
    if (self.canvas == nil) {
        return self.isFirstResponder;
    }
    aura_shell_activate_ax(self.canvas.slot, self.axIndex);
    return YES;
}

- (BOOL)hasText { return [self.canvas hasText]; }
- (void)insertText:(NSString *)text { [self.canvas insertText:text]; }
- (void)deleteBackward { [self.canvas deleteBackward]; }
- (UIKeyboardType)keyboardType { return [self.canvas keyboardType]; }
- (UIReturnKeyType)returnKeyType { return [self.canvas returnKeyType]; }
- (BOOL)isSecureTextEntry { return [self.canvas isSecureTextEntry]; }
- (UITextAutocorrectionType)autocorrectionType { return [self.canvas autocorrectionType]; }
- (UITextAutocapitalizationType)autocapitalizationType { return [self.canvas autocapitalizationType]; }
- (UITextContentType)textContentType { return [self.canvas textContentType]; }
- (UITextRange *)selectedTextRange { return [self.canvas selectedTextRange]; }
- (void)setSelectedTextRange:(UITextRange *)range { [self.canvas setSelectedTextRange:range]; }
- (UITextRange *)markedTextRange { return [self.canvas markedTextRange]; }
- (void)setMarkedText:(NSString *)markedText selectedRange:(NSRange)selectedRange { [self.canvas setMarkedText:markedText selectedRange:selectedRange]; }
- (void)unmarkText { [self.canvas unmarkText]; }
- (NSString *)textInRange:(UITextRange *)range { return [self.canvas textInRange:range]; }
- (void)replaceRange:(UITextRange *)range withText:(NSString *)text { [self.canvas replaceRange:range withText:text]; }
- (UITextPosition *)beginningOfDocument { return [self.canvas beginningOfDocument]; }
- (UITextPosition *)endOfDocument { return [self.canvas endOfDocument]; }
- (UITextRange *)textRangeFromPosition:(UITextPosition *)fromPosition toPosition:(UITextPosition *)toPosition { return [self.canvas textRangeFromPosition:fromPosition toPosition:toPosition]; }
- (UITextPosition *)positionFromPosition:(UITextPosition *)position offset:(NSInteger)offset { return [self.canvas positionFromPosition:position offset:offset]; }
- (UITextPosition *)positionFromPosition:(UITextPosition *)position inDirection:(UITextLayoutDirection)direction offset:(NSInteger)offset { return [self.canvas positionFromPosition:position inDirection:direction offset:offset]; }
- (NSComparisonResult)comparePosition:(UITextPosition *)position toPosition:(UITextPosition *)other { return [self.canvas comparePosition:position toPosition:other]; }
- (NSInteger)offsetFromPosition:(UITextPosition *)from toPosition:(UITextPosition *)toPosition { return [self.canvas offsetFromPosition:from toPosition:toPosition]; }
- (UITextPosition *)positionWithinRange:(UITextRange *)range farthestInDirection:(UITextLayoutDirection)direction { return [self.canvas positionWithinRange:range farthestInDirection:direction]; }
- (UITextRange *)characterRangeByExtendingPosition:(UITextPosition *)position inDirection:(UITextLayoutDirection)direction { return [self.canvas characterRangeByExtendingPosition:position inDirection:direction]; }
- (NSWritingDirection)baseWritingDirectionForPosition:(UITextPosition *)position inDirection:(UITextStorageDirection)direction { return [self.canvas baseWritingDirectionForPosition:position inDirection:direction]; }
- (void)setBaseWritingDirection:(NSWritingDirection)writingDirection forRange:(UITextRange *)range { [self.canvas setBaseWritingDirection:writingDirection forRange:range]; }

- (UITextPosition *)closestPositionToPoint:(CGPoint)point {
    if (self.canvas == nil) {
        return [AuraTextPosition positionWithOffset:0];
    }
    return [self.canvas closestPositionToPoint:[self convertPoint:point toView:self.canvas]];
}

- (UITextPosition *)closestPositionToPoint:(CGPoint)point withinRange:(UITextRange *)range {
    if (self.canvas == nil) {
        return [AuraTextPosition positionWithOffset:0];
    }
    return [self.canvas closestPositionToPoint:[self convertPoint:point toView:self.canvas] withinRange:range];
}

- (UITextRange *)characterRangeAtPoint:(CGPoint)point {
    UITextPosition *position = nil;
    UITextRange *word = nil;
    if (self.canvas == nil) {
        return nil;
    }
    position = [self closestPositionToPoint:point];
    if (position != nil && self.tokenizer != nil) {
        word = [self.tokenizer rangeEnclosingPosition:position withGranularity:UITextGranularityWord inDirection:UITextStorageDirectionForward];
        if (word != nil) {
            return word;
        }
    }
    return [self.canvas characterRangeAtPoint:[self convertPoint:point toView:self.canvas]];
}

- (CGRect)caretRectForPosition:(UITextPosition *)position {
    if (self.canvas == nil) {
        return CGRectZero;
    }
    return [self.canvas convertRect:[self.canvas caretRectForPosition:position] toView:self];
}

- (CGRect)firstRectForRange:(UITextRange *)range {
    if (self.canvas == nil) {
        return CGRectZero;
    }
    return [self.canvas convertRect:[self.canvas firstRectForRange:range] toView:self];
}

- (NSArray<UITextSelectionRect *> *)selectionRectsForRange:(UITextRange *)range {
    NSArray<UITextSelectionRect *> *rects = nil;
    NSMutableArray<UITextSelectionRect *> *local = nil;
    if (self.canvas == nil) {
        return @[];
    }
    rects = [self.canvas selectionRectsForRange:range];
    local = [NSMutableArray arrayWithCapacity:rects.count];
    for (AuraSelectionRect *rect in rects) {
        AuraSelectionRect *copy = [[[AuraSelectionRect alloc] init] autorelease];
        copy.storedRect = [self.canvas convertRect:rect.rect toView:self];
        copy.storedStart = rect.containsStart;
        copy.storedEnd = rect.containsEnd;
        [local addObject:copy];
    }
    return local;
}

@end

@implementation AuraCanvas

- (instancetype)initWithSlot:(int)slot {
    self = [super initWithFrame:CGRectZero];
    if (self == nil) {
        return nil;
    }
    self.slot = slot;
    self.backgroundColor = [UIColor blackColor];
    self.contentMode = UIViewContentModeScaleToFill;
    self.multipleTouchEnabled = YES;
    self.isAccessibilityElement = NO;
    self.shouldGroupAccessibilityChildren = YES;
    self.logFrame = 1;
    _touchIds = [[NSMutableDictionary alloc] init];
    _nextPointerId = 1;
    self.editingText = @"";
    self.composingStart = -1;
    self.composingEnd = -1;
    self.inputTypeConfig = 1;
    self.autocorrectConfig = YES;
    self.autofillHints = @"";
    self.environmentSignature = @"";
    self.preferenceObservers = [[NSMutableArray alloc] init];
    AuraTextInputView *input = [[AuraTextInputView alloc] initWithCanvas:self];
    self.textInput = input;
    [self addSubview:input];
    [input release];
    if (@available(iOS 13.4, *)) {
        UIHoverGestureRecognizer *hover = [[[UIHoverGestureRecognizer alloc] initWithTarget:self action:@selector(pointerHover:)] autorelease];
        UIPanGestureRecognizer *scroll = [[[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(pointerScroll:)] autorelease];
        scroll.allowedScrollTypesMask = UIScrollTypeMaskAll;
        scroll.allowedTouchTypes = @[ @(UITouchTypeIndirect), @(UITouchTypeIndirectPointer) ];
        [self addGestureRecognizer:hover];
        [self addGestureRecognizer:scroll];
    }
    return self;
}

- (void)dealloc {
    [_touchIds release];
    [_editingText release];
    [_autofillHints release];
    [_inputConfigurationSignature release];
    [_environmentSignature release];
    [_preferenceObservers release];
    [_axElements release];
    [_wordElements release];
    [_headingRotor release];
    [_textInput release];
    [super dealloc];
}

- (void)didMoveToWindow {
    uint64_t timestamp = aura_now_ns();
    [super didMoveToWindow];
    if (self.window == nil) {
        if (self.surfaceGeneration > 0) {
            aura_shell_surface(self.slot, AURA_SURFACE_DESTROYED, self.surfaceGeneration, (int64_t)(self.bounds.size.width * UIScreen.mainScreen.scale), (int64_t)(self.bounds.size.height * UIScreen.mainScreen.scale), AURA_SURFACE_FORMAT_RGBA8888, self.surfaceGeneration, timestamp);
            self.surfaceGeneration = 0;
        }
        return;
    }
    if (self.surfaceGeneration == 0) {
        g_surface_generation += 1;
        self.surfaceGeneration = g_surface_generation;
        CGFloat scale = self.window.screen.scale > 0.0 ? self.window.screen.scale : 1.0;
        aura_shell_surface(self.slot, AURA_SURFACE_CREATED, self.surfaceGeneration, (int64_t)(self.bounds.size.width * scale), (int64_t)(self.bounds.size.height * scale), AURA_SURFACE_FORMAT_RGBA8888, self.surfaceGeneration, timestamp);
    }
    if (self.link != nil) {
        return;
    }
    self.link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    [self.link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)tick:(CADisplayLink *)link {
    (void)link;
    aura_shell_vsync(self.slot);
}

- (void)setDisplayLinkSuspended:(BOOL)suspended {
    if (self.link != nil) {
        self.link.paused = suspended;
    }
}

- (void)layoutSubviews {
    uint64_t timestamp = aura_now_ns();
    [super layoutSubviews];
    [self publishSize];
    if (self.surfaceGeneration > 0 && self.bounds.size.width > 0.0 && self.bounds.size.height > 0.0) {
        CGFloat scale = self.window.screen.scale > 0.0 ? self.window.screen.scale : 1.0;
        aura_shell_surface(self.slot, AURA_SURFACE_CHANGED, self.surfaceGeneration, (int64_t)(self.bounds.size.width * scale), (int64_t)(self.bounds.size.height * scale), AURA_SURFACE_FORMAT_RGBA8888, self.surfaceGeneration, timestamp);
    }
}

- (void)safeAreaInsetsDidChange {
    [super safeAreaInsetsDidChange];
    self.safe = self.safeAreaInsets;
    [self publishSize];
}

- (void)publishSize {
    uint64_t timestamp = aura_now_ns();
    AuraMetricsPayload metrics;
    CGFloat scale = self.window.screen.scale > 0.0 ? self.window.screen.scale : UIScreen.mainScreen.scale;
    CGFloat width = self.bounds.size.width;
    CGFloat height = self.bounds.size.height;
    if (width < 1.0 || height < 1.0 || scale <= 0.0) {
        return;
    }
    if (width != self.loggedW || height != self.loggedH) {
        self.loggedW = width;
        self.loggedH = height;
        os_log(OS_LOG_DEFAULT, "aura-resize %.0f %.0f", width, height);
    }
    memset(&metrics, 0, sizeof(metrics));
    metrics.logical_width = width;
    metrics.logical_height = height;
    metrics.physical_width = (int64_t)(width * scale);
    metrics.physical_height = (int64_t)(height * scale);
    metrics.device_pixel_ratio = scale;
    metrics.refresh_rate = self.window.screen.maximumFramesPerSecond;
    metrics.view_padding.left = self.safe.left;
    metrics.view_padding.top = self.safe.top;
    metrics.view_padding.right = self.safe.right;
    metrics.view_padding.bottom = self.safe.bottom;
    metrics.view_insets.bottom = self.keyboard;
    metrics.orientation = width > height ? 2 : 1;
    if (self.window.windowScene != nil) {
        metrics.rotation_degrees = aura_rotation_degrees(self.window.windowScene.interfaceOrientation);
    }
    metrics.font_scale = aura_dynamic_type_scale();
    metrics.keyboard_duration_s = self.keyboardDuration;
    metrics.keyboard_curve = self.keyboardCurve;
    aura_shell_metrics_v1(self.slot, &metrics, timestamp);
    [self publishEnvironment];
}

- (void)setKeyboardHeight:(CGFloat)height duration:(CGFloat)duration curve:(int)curve {
    self.keyboard = height;
    self.keyboardDuration = duration;
    self.keyboardCurve = curve;
    [self publishSize];
}

- (void)traitCollectionDidChange:(UITraitCollection *)previousTraitCollection {
    [super traitCollectionDidChange:previousTraitCollection];
    [self publishSize];
}

- (void)publishEnvironment {
    uint64_t timestamp = aura_now_ns();
    AuraEnvironmentPayload payload;
    NSMutableString *locales = [NSMutableString string];
    NSString *signature = nil;
    const char *utf8 = NULL;
    for (NSString *language in [NSLocale preferredLanguages]) {
        NSString *tag = [language stringByReplacingOccurrencesOfString:@"_" withString:@"-"];
        if (locales.length > 0) [locales appendString:@"\n"];
        [locales appendString:tag];
    }
    memset(&payload, 0, sizeof(payload));
    payload.brightness = self.traitCollection.userInterfaceStyle == UIUserInterfaceStyleDark ? 1 : 0;
    payload.high_contrast = self.traitCollection.accessibilityContrast == UIAccessibilityContrastHigh ? 1 : 0;
    payload.reduce_motion = UIAccessibilityIsReduceMotionEnabled() ? 1 : 0;
    payload.accessible_navigation = UIAccessibilityIsVoiceOverRunning() ? 1 : 0;
    payload.text_direction = self.traitCollection.layoutDirection == UITraitEnvironmentLayoutDirectionRightToLeft ? 1 : 0;
    payload.bold_text = UIAccessibilityIsBoldTextEnabled() ? 1 : 0;
    payload.font_scale = aura_dynamic_type_scale();
    signature = [NSString stringWithFormat:@"%d|%d|%d|%d|%d|%d|%.4f|%@", payload.brightness, payload.high_contrast, payload.reduce_motion, payload.accessible_navigation, payload.text_direction, payload.bold_text, payload.font_scale, locales];
    if ([signature isEqualToString:self.environmentSignature]) {
        return;
    }
    utf8 = locales.UTF8String;
    if (aura_shell_environment(self.slot, &payload, utf8 != NULL ? utf8 : "", timestamp) != 0) {
        self.environmentSignature = signature;
    }
}

- (CGFloat)surfaceScale {
    CGFloat scale = self.window.screen.scale;
    if (!(scale > 0.0)) {
        scale = UIScreen.mainScreen.scale;
    }
    if (!(scale > 0.0)) {
        scale = 1.0;
    }
    return scale;
}

- (void)pointerHover:(UIHoverGestureRecognizer *)gesture {
    CGPoint point = [gesture locationInView:self];
    CGFloat scale = [self surfaceScale];
    AuraPlatformEvent event;
    int phase = gesture.state == UIGestureRecognizerStateCancelled ? AURA_PHASE_CANCELLED : AURA_PHASE_MOVED;
    uint64_t timestamp = aura_now_ns();
    if (!aura_mobile_pointer_v1(&event, phase, point.x * scale, point.y * scale, scale, 0, timestamp, 1, 1LL << 20, AURA_POINTER_KIND_MOUSE, 0, 0, 0.0, 0.0, 0.0)) {
        return;
    }
    aura_shell_post_event(self.slot, &event);
}

- (void)pointerScroll:(UIPanGestureRecognizer *)gesture {
    CGPoint delta = [gesture translationInView:self];
    if (gesture.state != UIGestureRecognizerStateChanged) {
        return;
    }
    [gesture setTranslation:CGPointZero inView:self];
    if (delta.x == 0.0 && delta.y == 0.0) {
        return;
    }
    aura_shell_post(self.slot, AURA_SCROLL, delta.x, delta.y, 0, 0, NULL);
}

- (void)cancelTrackedPointers {
    CGFloat scale = [self surfaceScale];
    uint64_t timestamp = aura_now_ns();
    for (NSNumber *pointerId in [_touchIds allValues]) {
        AuraPlatformEvent event;
        if (aura_mobile_pointer_v1(&event, AURA_PHASE_CANCELLED, 0.0, 0.0, scale, 0, timestamp, 1, pointerId.longLongValue, AURA_POINTER_KIND_TOUCH, 0, 0, 0.0, 0.0, 0.0)) {
            aura_shell_post_event(self.slot, &event);
        }
    }
    [_touchIds removeAllObjects];
}

- (void)selectTouch:(UITouch *)touch extend:(BOOL)extend {
    CGPoint point = [touch locationInView:self];
    AuraLayoutBoundary *items = NULL;
    int count = 0;
    NSInteger codePoint = 0;
    NSInteger glyphLeft = 0;
    NSInteger glyphRight = 0;
    NSInteger start = 0;
    NSInteger end = 0;
    BOOL forward = YES;
    CGRect hit = CGRectInset(self.editableRect, -12.0, -12.0);
    CGFloat dx = 0.0;
    CGFloat dy = 0.0;
    if (!extend) {
        if (self.editableRect.size.width < 1.0 || self.editableRect.size.height < 1.0 || !CGRectContainsPoint(hit, point)) {
            self.selectionTouchActive = NO;
            return;
        }
        self.selectionTouchActive = YES;
        self.selectionTouchOrigin = point;
    } else if (!self.selectionTouchActive) {
        return;
    } else {
        dx = point.x - self.selectionTouchOrigin.x;
        dy = point.y - self.selectionTouchOrigin.y;
        if (fabs(dx) < 8.0 && fabs(dy) < 8.0) {
            return;
        }
        forward = fabs(dy) > fabs(dx) ? dy >= 0.0 : dx >= 0.0;
    }
    count = [self copyLayout:&items];
    codePoint = [self codePointAtPoint:point items:items count:count];
    if (![self glyphSpanAtPoint:point items:items count:count left:&glyphLeft right:&glyphRight]) {
        glyphLeft = codePoint;
        glyphRight = codePoint;
    }
    free(items);
    if (codePoint < 0) {
        return;
    }
    if (!extend) {
        self.selectionTouchAnchor = codePoint;
        self.selectionTouchGlyphLeft = glyphLeft;
        self.selectionTouchGlyphRight = glyphRight;
        start = codePoint;
        end = codePoint;
    } else if (forward) {
        start = self.selectionTouchGlyphLeft;
        end = glyphRight;
    } else {
        start = self.selectionTouchGlyphRight;
        end = glyphLeft;
    }
    [self setSelectedTextRange:[AuraTextRange rangeWithStart:[self utf16OffsetForCodePoint:start] end:[self utf16OffsetForCodePoint:end]]];
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self deliver:touches event:event phase:AURA_PHASE_BEGAN];
    if (touches.anyObject != nil) {
        [self selectTouch:touches.anyObject extend:NO];
    }
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self deliver:touches event:event phase:AURA_PHASE_MOVED];
    if (touches.anyObject != nil) {
        [self selectTouch:touches.anyObject extend:YES];
    }
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self deliver:touches event:event phase:AURA_PHASE_ENDED];
    self.selectionTouchActive = NO;
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self deliver:touches event:event phase:AURA_PHASE_CANCELLED];
    self.selectionTouchActive = NO;
}

- (int64_t)logicalKeyForPress:(UIPress *)press API_AVAILABLE(ios(13.4)) {
    UIKey *key = press.key;
    if (key == nil) {
        return 0;
    }
    NSInteger code = key.keyCode;
    if ((code >= AURA_KEY_A && code <= AURA_KEY_Z) ||
        code == AURA_KEY_ENTER || code == AURA_KEY_ESCAPE ||
        code == AURA_KEY_BACKSPACE || code == AURA_KEY_TAB ||
        code == AURA_KEY_SPACE || code == AURA_KEY_HOME ||
        code == AURA_KEY_DELETE || code == AURA_KEY_END ||
        code == AURA_KEY_RIGHT || code == AURA_KEY_LEFT ||
        code == AURA_KEY_DOWN_ARROW || code == AURA_KEY_UP_ARROW) {
        return code;
    }
    return 0;
}

- (uint32_t)modifiersForPress:(UIPress *)press API_AVAILABLE(ios(13.4)) {
    UIKeyModifierFlags flags = press.key.modifierFlags;
    uint32_t modifiers = 0;
    if ((flags & UIKeyModifierCommand) != 0) modifiers |= AURA_MOD_META;
    if ((flags & UIKeyModifierShift) != 0) modifiers |= AURA_MOD_SHIFT;
    if ((flags & UIKeyModifierControl) != 0) modifiers |= AURA_MOD_CONTROL;
    if ((flags & UIKeyModifierAlternate) != 0) modifiers |= AURA_MOD_ALT;
    return modifiers;
}

- (BOOL)deliverPresses:(NSSet<UIPress *> *)presses down:(BOOL)down API_AVAILABLE(ios(13.4)) {
    BOOL handled = NO;
    for (UIPress *press in presses) {
        int64_t logical = [self logicalKeyForPress:press];
        AuraPlatformEvent event;
        if (logical == 0) {
            continue;
        }
        if (!aura_mobile_key_v1(&event, down ? 1 : 0, logical, [self modifiersForPress:press], 0, (uint64_t)(press.timestamp * 1000000000.0), 1)) {
            continue;
        }
        aura_shell_post_event(self.slot, &event);
        handled = YES;
    }
    return handled;
}

- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    if (@available(iOS 13.4, *)) {
        if ([self deliverPresses:presses down:YES]) {
            return;
        }
    }
    [super pressesBegan:presses withEvent:event];
}

- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    if (@available(iOS 13.4, *)) {
        if ([self deliverPresses:presses down:NO]) {
            return;
        }
    }
    [super pressesEnded:presses withEvent:event];
}

- (int64_t)pointerIdForTouch:(UITouch *)touch {
    NSValue *key = [NSValue valueWithNonretainedObject:touch];
    NSNumber *stored = [_touchIds objectForKey:key];
    if (stored != nil) {
        return stored.longLongValue;
    }
    int64_t issued = _nextPointerId;
    _nextPointerId += 1;
    [_touchIds setObject:[NSNumber numberWithLongLong:issued] forKey:key];
    return issued;
}

- (uint32_t)pointerKindForTouch:(UITouch *)touch {
    if (touch.type == UITouchTypeStylus) {
        return AURA_POINTER_KIND_STYLUS;
    }
    if (touch.type == UITouchTypeIndirectPointer) {
        return AURA_POINTER_KIND_MOUSE;
    }
    if (touch.type == UITouchTypeIndirect) {
        return AURA_POINTER_KIND_TRACKPAD;
    }
    return AURA_POINTER_KIND_TOUCH;
}

- (void)deliver:(NSSet<UITouch *> *)touches event:(UIEvent *)uiEvent phase:(int)phase {
    CGFloat scale = [self surfaceScale];
    NSArray<UITouch *> *ordered = [[touches allObjects] sortedArrayUsingComparator:^NSComparisonResult(UITouch *left, UITouch *right) {
        int64_t leftId = [self pointerIdForTouch:left];
        int64_t rightId = [self pointerIdForTouch:right];
        if (leftId < rightId) return NSOrderedAscending;
        if (leftId > rightId) return NSOrderedDescending;
        return NSOrderedSame;
    }];
    for (UITouch *touch in ordered) {
        AuraMobileEvent ev;
        int64_t pointerId = [self pointerIdForTouch:touch];
        double pressure = 0.0;
        if (touch.maximumPossibleForce > 0.0) {
            pressure = touch.force / touch.maximumPossibleForce;
        }
        NSArray<UITouch *> *samples = @[touch];
        double tiltX = 0.0;
        double tiltY = 0.0;
        if (phase == AURA_PHASE_MOVED && uiEvent != nil) {
            NSArray<UITouch *> *coalesced = [uiEvent coalescedTouchesForTouch:touch];
            if (coalesced.count > 0) samples = coalesced;
        }
        if (touch.type == UITouchTypeStylus) {
            double tilt = M_PI_2 - touch.altitudeAngle;
            double azimuth = [touch azimuthAngleInView:self];
            if (tilt < 0.0) tilt = 0.0;
            tiltX = tilt * cos(azimuth);
            tiltY = tilt * sin(azimuth);
        }
        for (UITouch *sample in samples) {
            CGPoint samplePoint = [sample locationInView:self];
            double samplePressure = pressure;
            if (sample.maximumPossibleForce > 0.0) {
                samplePressure = sample.force / sample.maximumPossibleForce;
            }
            if (!aura_mobile_pointer_v1(&ev, phase, samplePoint.x * scale, samplePoint.y * scale, scale, 0, (uint64_t)(sample.timestamp * 1000000000.0), 1, pointerId, [self pointerKindForTouch:touch], phase == AURA_PHASE_CANCELLED ? 0 : 1, 0, samplePressure, tiltX, tiltY)) {
                continue;
            }
            aura_shell_post_event(self.slot, &ev);
        }
        if (phase == AURA_PHASE_ENDED || phase == AURA_PHASE_CANCELLED) {
            [_touchIds removeObjectForKey:[NSValue valueWithNonretainedObject:touch]];
        }
    }
}

- (void)drawRect:(CGRect)rect {
    uint8_t *pixels = NULL;
    int width = 0;
    int height = 0;
    int stride = 0;
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    (void)rect;
    if (ctx == NULL || aura_shell_copy_pixels(self.slot, &pixels, &width, &height, &stride) != 0) {
        return;
    }
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, pixels, (size_t)stride * (size_t)height, aura_pixels_free);
    CGImageRef image = CGImageCreate((size_t)width, (size_t)height, 8, 32, (size_t)stride, space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big, provider, NULL, NO, kCGRenderingIntentDefault);
    if (image != NULL) {
        CGContextSaveGState(ctx);
        CGContextTranslateCTM(ctx, 0, self.bounds.size.height);
        CGContextScaleCTM(ctx, 1.0, -1.0);
        CGContextDrawImage(ctx, self.bounds, image);
        CGContextRestoreGState(ctx);
        CGImageRelease(image);
    }
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(space);
    self.frames += 1;
    if (self.logFrame) {
        os_log(OS_LOG_DEFAULT, "aura-frame");
        self.logFrame = 0;
    }
}

- (BOOL)canBecomeFirstResponder {
    return NO;
}

- (CGRect)screenRectForLocal:(CGRect)local {
    if (self.window == nil) {
        return local;
    }
    CGRect inWindow = [self convertRect:local toView:nil];
    return [self.window convertRect:inWindow toWindow:nil];
}

- (BOOL)hasText {
    return self.editingText.length > 0;
}

- (UIKeyboardType)keyboardType {
    switch (self.inputTypeConfig) {
        case 2: return UIKeyboardTypeEmailAddress;
        case 3: return UIKeyboardTypeURL;
        case 4: return UIKeyboardTypePhonePad;
        case 5: return UIKeyboardTypeNumberPad;
        default: return UIKeyboardTypeDefault;
    }
}

- (UIReturnKeyType)returnKeyType {
    switch (self.actionConfig) {
        case 1: return UIReturnKeyDone;
        case 2: return UIReturnKeyGo;
        case 3: return UIReturnKeyNext;
        case 4: return UIReturnKeySearch;
        case 5: return UIReturnKeySend;
        default: return UIReturnKeyDefault;
    }
}

- (BOOL)isSecureTextEntry {
    return self.secureConfig;
}

- (UITextAutocorrectionType)autocorrectionType {
    return self.autocorrectConfig ? UITextAutocorrectionTypeYes : UITextAutocorrectionTypeNo;
}

- (UITextAutocapitalizationType)autocapitalizationType {
    switch (self.capitalizationConfig) {
        case 1: return UITextAutocapitalizationTypeSentences;
        case 2: return UITextAutocapitalizationTypeWords;
        case 3: return UITextAutocapitalizationTypeAllCharacters;
        default: return UITextAutocapitalizationTypeNone;
    }
}

- (UITextContentType)textContentType {
    NSString *hint = [[self.autofillHints componentsSeparatedByString:@"\n"] firstObject];
    if ([hint isEqualToString:@"username"]) return UITextContentTypeUsername;
    if ([hint isEqualToString:@"password"]) return UITextContentTypePassword;
    if ([hint isEqualToString:@"newPassword"]) return UITextContentTypeNewPassword;
    if ([hint isEqualToString:@"emailAddress"]) return UITextContentTypeEmailAddress;
    if ([hint isEqualToString:@"name"]) return UITextContentTypeName;
    if ([hint isEqualToString:@"oneTimeCode"]) return UITextContentTypeOneTimeCode;
    return nil;
}

- (NSInteger)clampUTF16Offset:(NSInteger)offset {
    if (offset < 0) return 0;
    if (offset > (NSInteger)self.editingText.length) return (NSInteger)self.editingText.length;
    if (offset > 0 && offset < (NSInteger)self.editingText.length) {
        unichar previous = [self.editingText characterAtIndex:(NSUInteger)(offset - 1)];
        unichar current = [self.editingText characterAtIndex:(NSUInteger)offset];
        if (previous >= 0xd800 && previous <= 0xdbff && current >= 0xdc00 && current <= 0xdfff) {
            return offset - 1;
        }
    }
    return offset;
}

- (NSInteger)utf16OffsetForCodePoint:(NSInteger)codePointOffset {
    NSInteger target = MAX(0, codePointOffset);
    NSInteger codePoints = 0;
    NSInteger utf16 = 0;
    while (utf16 < (NSInteger)self.editingText.length && codePoints < target) {
        unichar first = [self.editingText characterAtIndex:(NSUInteger)utf16];
        utf16 += 1;
        if (first >= 0xd800 && first <= 0xdbff && utf16 < (NSInteger)self.editingText.length) {
            unichar second = [self.editingText characterAtIndex:(NSUInteger)utf16];
            if (second >= 0xdc00 && second <= 0xdfff) utf16 += 1;
        }
        codePoints += 1;
    }
    return utf16;
}

- (NSInteger)codePointOffsetForUTF16:(NSInteger)utf16Offset {
    NSInteger limit = [self clampUTF16Offset:utf16Offset];
    NSInteger codePoints = 0;
    NSInteger utf16 = 0;
    while (utf16 < limit) {
        unichar first = [self.editingText characterAtIndex:(NSUInteger)utf16];
        utf16 += 1;
        if (first >= 0xd800 && first <= 0xdbff && utf16 < limit) {
            unichar second = [self.editingText characterAtIndex:(NSUInteger)utf16];
            if (second >= 0xdc00 && second <= 0xdfff) utf16 += 1;
        }
        codePoints += 1;
    }
    return codePoints;
}

- (NSRange)selectedUTF16Range {
    NSInteger start = [self utf16OffsetForCodePoint:self.selectionStart];
    NSInteger end = [self utf16OffsetForCodePoint:self.selectionEnd];
    return NSMakeRange((NSUInteger)MIN(start, end), (NSUInteger)labs(end - start));
}

- (NSRange)markedUTF16Range {
    if (self.composingStart < 0 || self.composingEnd < self.composingStart) return NSMakeRange(NSNotFound, 0);
    NSInteger start = [self utf16OffsetForCodePoint:self.composingStart];
    NSInteger end = [self utf16OffsetForCodePoint:self.composingEnd];
    return NSMakeRange((NSUInteger)start, (NSUInteger)MAX(0, end - start));
}

- (void)notifyTextWillChange {
    AuraTextInputView *input = self.textInput;
    if (input.inputDelegate != nil) [input.inputDelegate textWillChange:input];
}

- (void)notifyTextDidChange {
    AuraTextInputView *input = self.textInput;
    if (input.inputDelegate != nil) [input.inputDelegate textDidChange:input];
}

- (void)notifySelectionWillChange {
    AuraTextInputView *input = self.textInput;
    if (input.inputDelegate != nil) [input.inputDelegate selectionWillChange:input];
}

- (void)notifySelectionDidChange {
    AuraTextInputView *input = self.textInput;
    if (input.inputDelegate != nil) [input.inputDelegate selectionDidChange:input];
}

- (UITextRange *)selectedTextRange {
    NSRange range = [self selectedUTF16Range];
    return [AuraTextRange rangeWithStart:(NSInteger)range.location end:(NSInteger)NSMaxRange(range)];
}

- (void)setSelectedTextRange:(UITextRange *)range {
    uint64_t timestamp = aura_now_ns();
    if (self.caretRefreshDepth > 0) return;
    if (![range isKindOfClass:[AuraTextRange class]]) return;
    AuraTextRange *typed = (AuraTextRange *)range;
    NSInteger start = [self clampUTF16Offset:typed.rangeStart.offset];
    NSInteger end = [self clampUTF16Offset:typed.rangeEnd.offset];
    NSInteger codeStart = [self codePointOffsetForUTF16:start];
    NSInteger codeEnd = [self codePointOffsetForUTF16:end];
    if (self.selectionStart == codeStart && self.selectionEnd == codeEnd) return;
    [self notifySelectionWillChange];
    self.selectionStart = codeStart;
    self.selectionEnd = codeEnd;
    [self notifySelectionDidChange];
    aura_shell_text_edit(self.slot, AURA_TEXT_EDIT_SET_SELECTION, "", self.selectionStart, self.selectionEnd, timestamp);
}

- (UITextRange *)markedTextRange {
    NSRange range = [self markedUTF16Range];
    if (range.location == NSNotFound) return nil;
    return [AuraTextRange rangeWithStart:(NSInteger)range.location end:(NSInteger)NSMaxRange(range)];
}

- (void)setMarkedText:(NSString *)markedText selectedRange:(NSRange)selectedRange {
    uint64_t timestamp = aura_now_ns();
    NSString *replacement = markedText != nil ? markedText : @"";
    NSRange target = [self markedUTF16Range];
    if (target.location == NSNotFound) target = [self selectedUTF16Range];
    [self notifyTextWillChange];
    [self notifySelectionWillChange];
    NSMutableString *buffer = [self.editingText mutableCopy];
    [buffer replaceCharactersInRange:target withString:replacement];
    self.editingText = buffer;
    [buffer release];
    NSInteger markedStartUTF16 = (NSInteger)target.location;
    NSInteger markedEndUTF16 = markedStartUTF16 + (NSInteger)replacement.length;
    self.composingStart = [self codePointOffsetForUTF16:markedStartUTF16];
    self.composingEnd = [self codePointOffsetForUTF16:markedEndUTF16];
    NSInteger selectedStartUTF16 = markedStartUTF16 + (NSInteger)selectedRange.location;
    NSInteger selectedEndUTF16 = selectedStartUTF16 + (NSInteger)selectedRange.length;
    self.selectionStart = [self codePointOffsetForUTF16:selectedStartUTF16];
    self.selectionEnd = [self codePointOffsetForUTF16:selectedEndUTF16];
    [self notifySelectionDidChange];
    [self notifyTextDidChange];
    const char *utf8 = replacement.UTF8String;
    aura_shell_text_edit(self.slot, AURA_TEXT_EDIT_SET_COMPOSING, utf8 != NULL ? utf8 : "", 1, 0, timestamp);
    aura_shell_text_edit(self.slot, AURA_TEXT_EDIT_SET_SELECTION, "", self.selectionStart, self.selectionEnd, timestamp);
}

- (void)unmarkText {
    uint64_t timestamp = aura_now_ns();
    if (self.composingStart < 0) return;
    [self notifyTextWillChange];
    self.composingStart = -1;
    self.composingEnd = -1;
    aura_shell_text_edit(self.slot, AURA_TEXT_EDIT_FINISH_COMPOSING, "", 0, 0, timestamp);
    [self notifyTextDidChange];
}

- (NSString *)textInRange:(UITextRange *)range {
    if (![range isKindOfClass:[AuraTextRange class]]) return @"";
    AuraTextRange *typed = (AuraTextRange *)range;
    NSInteger start = [self clampUTF16Offset:typed.rangeStart.offset];
    NSInteger end = [self clampUTF16Offset:typed.rangeEnd.offset];
    NSRange value = NSMakeRange((NSUInteger)MIN(start, end), (NSUInteger)labs(end - start));
    return [self.editingText substringWithRange:value];
}

- (void)replaceRange:(UITextRange *)range withText:(NSString *)text {
    uint64_t timestamp = aura_now_ns();
    if (![range isKindOfClass:[AuraTextRange class]]) return;
    AuraTextRange *typed = (AuraTextRange *)range;
    NSInteger start = [self clampUTF16Offset:typed.rangeStart.offset];
    NSInteger end = [self clampUTF16Offset:typed.rangeEnd.offset];
    NSInteger codePointStart = [self codePointOffsetForUTF16:MIN(start, end)];
    NSInteger codePointEnd = [self codePointOffsetForUTF16:MAX(start, end)];
    NSString *replacement = text != nil ? text : @"";
    [self notifyTextWillChange];
    [self notifySelectionWillChange];
    NSMutableString *buffer = [self.editingText mutableCopy];
    [buffer replaceCharactersInRange:NSMakeRange((NSUInteger)MIN(start, end), (NSUInteger)labs(end - start)) withString:replacement];
    self.editingText = buffer;
    [buffer release];
    NSInteger caret = MIN(start, end) + (NSInteger)replacement.length;
    self.selectionStart = [self codePointOffsetForUTF16:caret];
    self.selectionEnd = self.selectionStart;
    self.composingStart = -1;
    self.composingEnd = -1;
    [self notifySelectionDidChange];
    [self notifyTextDidChange];
    const char *utf8 = replacement.UTF8String;
    aura_shell_text_edit(self.slot, AURA_TEXT_EDIT_REPLACE_RANGE, utf8 != NULL ? utf8 : "", codePointStart, codePointEnd, timestamp);
}

- (UITextPosition *)beginningOfDocument { return [AuraTextPosition positionWithOffset:0]; }
- (UITextPosition *)endOfDocument { return [AuraTextPosition positionWithOffset:(NSInteger)self.editingText.length]; }

- (UITextRange *)textRangeFromPosition:(UITextPosition *)fromPosition toPosition:(UITextPosition *)toPosition {
    if (![fromPosition isKindOfClass:[AuraTextPosition class]] || ![toPosition isKindOfClass:[AuraTextPosition class]]) return nil;
    return [AuraTextRange rangeWithStart:((AuraTextPosition *)fromPosition).offset end:((AuraTextPosition *)toPosition).offset];
}

- (UITextPosition *)positionFromPosition:(UITextPosition *)position offset:(NSInteger)offset {
    if (![position isKindOfClass:[AuraTextPosition class]]) return nil;
    NSInteger next = ((AuraTextPosition *)position).offset + offset;
    if (next < 0 || next > (NSInteger)self.editingText.length) return nil;
    return [AuraTextPosition positionWithOffset:next];
}

- (UITextPosition *)positionFromPosition:(UITextPosition *)position inDirection:(UITextLayoutDirection)direction offset:(NSInteger)offset {
    NSInteger signedOffset = offset;
    if (direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp) signedOffset = -offset;
    return [self positionFromPosition:position offset:signedOffset];
}

- (NSComparisonResult)comparePosition:(UITextPosition *)position toPosition:(UITextPosition *)other {
    NSInteger left = ((AuraTextPosition *)position).offset;
    NSInteger right = ((AuraTextPosition *)other).offset;
    if (left < right) return NSOrderedAscending;
    if (left > right) return NSOrderedDescending;
    return NSOrderedSame;
}

- (NSInteger)offsetFromPosition:(UITextPosition *)from toPosition:(UITextPosition *)toPosition {
    return ((AuraTextPosition *)toPosition).offset - ((AuraTextPosition *)from).offset;
}

- (UITextPosition *)positionWithinRange:(UITextRange *)range farthestInDirection:(UITextLayoutDirection)direction {
    AuraTextRange *typed = (AuraTextRange *)range;
    if (direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp) return typed.rangeStart;
    return typed.rangeEnd;
}

- (UITextRange *)characterRangeByExtendingPosition:(UITextPosition *)position inDirection:(UITextLayoutDirection)direction {
    NSInteger offset = [self clampUTF16Offset:((AuraTextPosition *)position).offset];
    if (direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp) {
        if (offset == 0) return [AuraTextRange rangeWithStart:0 end:0];
        NSRange sequence = [self.editingText rangeOfComposedCharacterSequenceAtIndex:(NSUInteger)(offset - 1)];
        return [AuraTextRange rangeWithStart:(NSInteger)sequence.location end:(NSInteger)NSMaxRange(sequence)];
    }
    if (offset >= (NSInteger)self.editingText.length) return [AuraTextRange rangeWithStart:offset end:offset];
    NSRange sequence = [self.editingText rangeOfComposedCharacterSequenceAtIndex:(NSUInteger)offset];
    return [AuraTextRange rangeWithStart:(NSInteger)sequence.location end:(NSInteger)NSMaxRange(sequence)];
}

- (BOOL)textInputIsLeftToRight {
    return self.traitCollection.layoutDirection != UITraitEnvironmentLayoutDirectionRightToLeft;
}

- (NSWritingDirection)baseWritingDirectionForPosition:(UITextPosition *)position inDirection:(UITextStorageDirection)direction {
    (void)position;
    (void)direction;
    if ([self textInputIsLeftToRight]) {
        self.textInput.semanticContentAttribute = UISemanticContentAttributeForceLeftToRight;
        return NSWritingDirectionLeftToRight;
    }
    self.textInput.semanticContentAttribute = UISemanticContentAttributeForceRightToLeft;
    return NSWritingDirectionRightToLeft;
}

- (void)setBaseWritingDirection:(NSWritingDirection)writingDirection forRange:(UITextRange *)range {
    (void)writingDirection;
    (void)range;
}

- (int)copyLayout:(AuraLayoutBoundary **)out {
    AuraLayoutSnapshot *snapshot = NULL;
    AuraLayoutBoundary *copy = NULL;
    int count = 0;
    if (self.slot < 0 || self.slot >= 4 || out == NULL) {
        return 0;
    }
    snapshot = &g_layout[self.slot];
    pthread_mutex_lock(&snapshot->mu);
    count = snapshot->count;
    if (count > 0 && snapshot->items != NULL) {
        copy = (AuraLayoutBoundary *)calloc((size_t)count, sizeof(AuraLayoutBoundary));
        if (copy != NULL) {
            memcpy(copy, snapshot->items, (size_t)count * sizeof(AuraLayoutBoundary));
        } else {
            count = 0;
        }
    } else {
        count = 0;
    }
    pthread_mutex_unlock(&snapshot->mu);
    *out = copy;
    return count;
}

- (CGRect)rectFromLayout:(AuraLayoutBoundary *)items count:(int)count codePoint:(NSInteger)codePoint {
    int index = 0;
    CGFloat width = MAX(1.0, self.caretRect.size.width);
    if (items == NULL || count <= 0) {
        return self.caretRect;
    }
    while (index < count && items[index].offset < codePoint) {
        index += 1;
    }
    if (index >= count) {
        index = count - 1;
    }
    if (index + 1 < count && fabs(items[index + 1].y - items[index].y) < 0.5) {
        width = items[index + 1].x - items[index].x;
        if (width < 1.0) width = 1.0;
    }
    return CGRectMake(self.editableRect.origin.x + items[index].x, self.editableRect.origin.y + items[index].y, width, items[index].height);
}

- (BOOL)glyphSpanAtPoint:(CGPoint)point items:(AuraLayoutBoundary *)items count:(int)count left:(NSInteger *)left right:(NSInteger *)right {
    CGFloat localX = point.x - self.editableRect.origin.x;
    CGFloat localY = point.y - self.editableRect.origin.y;
    int index = 0;
    int first = -1;
    int last = -1;
    int best = 0;
    double bestDistance = 0.0;
    CGFloat lineY = 0.0;
    if (items == NULL || count <= 0 || left == NULL || right == NULL) {
        return NO;
    }
    while (index < count) {
        double dy = items[index].y + items[index].height * 0.5 - localY;
        double distance = dy * dy;
        if (index == 0 || distance < bestDistance) {
            best = index;
            bestDistance = distance;
            lineY = items[index].y;
        }
        index += 1;
    }
    index = 0;
    while (index < count) {
        if (fabs(items[index].y - lineY) < 0.5) {
            if (first < 0) first = index;
            last = index;
        }
        index += 1;
    }
    if (first < 0) {
        return NO;
    }
    if (first == last || localX <= items[first].x) {
        *left = items[first].offset;
        *right = items[first].offset;
        return YES;
    }
    index = first;
    while (index < last) {
        if (localX < items[index + 1].x) {
            *left = items[index].offset;
            *right = items[index + 1].offset;
            return YES;
        }
        index += 1;
    }
    *left = items[last].offset;
    *right = items[last].offset;
    return YES;
}

- (NSInteger)codePointAtPoint:(CGPoint)point items:(AuraLayoutBoundary *)items count:(int)count {
    CGFloat localX = point.x - self.editableRect.origin.x;
    CGFloat localY = point.y - self.editableRect.origin.y;
    int best = 0;
    int index = 0;
    double bestDistance = 0.0;
    if (items == NULL || count <= 0) {
        return -1;
    }
    while (index < count) {
        double dx = items[index].x - localX;
        double dy = items[index].y + items[index].height * 0.5 - localY;
        double distance = dx * dx + dy * dy;
        if (index == 0 || distance < bestDistance) {
            best = index;
            bestDistance = distance;
        }
        index += 1;
    }
    return (NSInteger)items[best].offset;
}

- (CGRect)firstRectForRange:(UITextRange *)range {
    AuraTextRange *typed = (AuraTextRange *)range;
    AuraLayoutBoundary *items = NULL;
    int count = [self copyLayout:&items];
    NSInteger start = [self codePointOffsetForUTF16:typed.rangeStart.offset];
    CGRect rect = [self rectFromLayout:items count:count codePoint:start];
    free(items);
    return rect;
}

- (void)refreshCaret {
    AuraTextInputView *input = self.textInput;
    CGFloat localX = 0.0;
    CGFloat localY = 0.0;
    if (self.caretRefreshDepth > 0 || input == nil || input.inputDelegate == nil || !input.isFirstResponder) {
        return;
    }
    localX = self.caretRect.origin.x - self.editableRect.origin.x;
    localY = self.caretRect.origin.y - self.editableRect.origin.y;
    if (self.caretAnnounceValid && self.caretAnnounceStart == self.selectionStart && self.caretAnnounceEnd == self.selectionEnd && self.caretAnnounceRevision == self.textRevision && fabs(self.caretAnnounceX - localX) < 0.5 && fabs(self.caretAnnounceY - localY) < 0.5) {
        return;
    }
    self.caretAnnounceValid = YES;
    self.caretAnnounceStart = self.selectionStart;
    self.caretAnnounceEnd = self.selectionEnd;
    self.caretAnnounceRevision = self.textRevision;
    self.caretAnnounceX = localX;
    self.caretAnnounceY = localY;
    self.caretRefreshDepth += 1;
    [input.inputDelegate selectionWillChange:input];
    [input.inputDelegate selectionDidChange:input];
    self.caretRefreshDepth -= 1;
}

- (CGRect)caretRectForPosition:(UITextPosition *)position {
    AuraLayoutBoundary *items = NULL;
    int count = 0;
    NSInteger codePoint = 0;
    CGRect rect;
    if (![position isKindOfClass:[AuraTextPosition class]]) {
        return self.caretRect;
    }
    codePoint = [self codePointOffsetForUTF16:((AuraTextPosition *)position).offset];
    if (codePoint == self.selectionEnd && self.caretRevision == self.textRevision && self.caretRect.size.height >= 1.0) {
        rect = self.caretRect;
        if (rect.size.width < 2.0) {
            rect.size.width = 2.0;
        }
        return rect;
    }
    count = [self copyLayout:&items];
    rect = [self rectFromLayout:items count:count codePoint:codePoint];
    free(items);
    rect.size.width = 2.0;
    return rect;
}

- (BOOL)layout:(AuraLayoutBoundary *)items count:(int)count containsPoint:(CGPoint)point {
    return aura_text_point_on_text(items, count, point.x - self.editableRect.origin.x, point.y - self.editableRect.origin.y, 12.0) != 0;
}

- (void)refreshTextContentRect {
    AuraLayoutBoundary *items = NULL;
    int count = [self copyLayout:&items];
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    if (aura_text_content_rect(items, count, 12.0, &x, &y, &width, &height)) {
        self.textContentRect = CGRectMake(self.editableRect.origin.x + (CGFloat)x, self.editableRect.origin.y + (CGFloat)y, (CGFloat)width, (CGFloat)height);
        self.textContentValid = YES;
    } else {
        self.textContentValid = NO;
    }
    free(items);
}

- (BOOL)visualLineForUTF16:(NSInteger)index start:(NSInteger *)start end:(NSInteger *)end {
    AuraLayoutBoundary *items = NULL;
    int count = [self copyLayout:&items];
    AuraTextSpan span;
    NSInteger codePoint = [self codePointOffsetForUTF16:index];
    BOOL found = aura_text_visual_line(items, count, (int64_t)codePoint, &span) != 0;
    free(items);
    if (!found || start == NULL || end == NULL) return NO;
    *start = [self utf16OffsetForCodePoint:(NSInteger)span.start];
    *end = [self utf16OffsetForCodePoint:(NSInteger)span.end];
    return *end > *start;
}

- (UITextPosition *)closestPositionToPoint:(CGPoint)point {
    AuraLayoutBoundary *items = NULL;
    int count = [self copyLayout:&items];
    NSInteger codePoint = [self codePointAtPoint:point items:items count:count];
    free(items);
    if (codePoint < 0) return nil;
    return [AuraTextPosition positionWithOffset:[self utf16OffsetForCodePoint:codePoint]];
}

- (UITextPosition *)closestPositionToPoint:(CGPoint)point withinRange:(UITextRange *)range {
    AuraTextPosition *position = (AuraTextPosition *)[self closestPositionToPoint:point];
    AuraTextRange *typed = (AuraTextRange *)range;
    if (position == nil || typed == nil) return nil;
    position.offset = MIN(typed.rangeEnd.offset, MAX(typed.rangeStart.offset, position.offset));
    return position;
}

- (UITextRange *)characterRangeAtPoint:(CGPoint)point {
    UITextPosition *position = [self closestPositionToPoint:point];
    if (position == nil) return nil;
    return [self characterRangeByExtendingPosition:position inDirection:UITextLayoutDirectionRight];
}

- (NSArray<UITextSelectionRect *> *)selectionRectsForRange:(UITextRange *)range {
    AuraTextRange *typed = (AuraTextRange *)range;
    AuraLayoutBoundary *items = NULL;
    int count = [self copyLayout:&items];
    NSInteger start = [self codePointOffsetForUTF16:MIN(typed.rangeStart.offset, typed.rangeEnd.offset)];
    NSInteger end = [self codePointOffsetForUTF16:MAX(typed.rangeStart.offset, typed.rangeEnd.offset)];
    NSMutableArray<UITextSelectionRect *> *rects = [NSMutableArray array];
    int index = 0;
    if (items == NULL || start == end) {
        free(items);
        return @[];
    }
    while (index < count) {
        if (items[index].offset >= start && items[index].offset < end) {
            AuraSelectionRect *rect = [[[AuraSelectionRect alloc] init] autorelease];
            rect.storedRect = [self rectFromLayout:items count:count codePoint:(NSInteger)items[index].offset];
            rect.storedStart = items[index].offset == start;
            rect.storedEnd = index + 1 >= count || items[index + 1].offset >= end;
            [rects addObject:rect];
        }
        index += 1;
    }
    free(items);
    return rects;
}

- (void)insertText:(NSString *)text {
    uint64_t timestamp = aura_now_ns();
    if ([text isEqualToString:@"\n"] || [text isEqualToString:@"\r"]) {
        if (!self.multilineConfig && self.actionConfig != 6) {
            aura_shell_text_edit(self.slot, AURA_TEXT_EDIT_PERFORM_ACTION, "", self.actionConfig, 0, timestamp);
            return;
        }
    }
    NSRange target = [self markedUTF16Range];
    if (target.location == NSNotFound) target = [self selectedUTF16Range];
    [self replaceRange:[AuraTextRange rangeWithStart:(NSInteger)target.location end:(NSInteger)NSMaxRange(target)] withText:text];
}

- (void)deleteBackward {
    NSRange target = [self selectedUTF16Range];
    if (target.length == 0 && target.location > 0) {
        target = [self.editingText rangeOfComposedCharacterSequenceAtIndex:target.location - 1];
    }
    if (target.length > 0) {
        [self replaceRange:[AuraTextRange rangeWithStart:(NSInteger)target.location end:(NSInteger)NSMaxRange(target)] withText:@""];
    }
}

- (void)trimAccessibilityCache {
    NSInteger count = aura_shell_ax_count(self.slot);
    if (self.axElements == nil) {
        return;
    }
    while ((NSInteger)self.axElements.count > count) {
        [self.axElements removeLastObject];
    }
}

- (BOOL)exploresWords {
    return UIAccessibilityIsVoiceOverRunning() && (self.textInput == nil || !self.textInput.voiceOverEditing || !self.textInput.isFirstResponder);
}

- (CGRect)rectForCodeRange:(NSInteger)start end:(NSInteger)end items:(AuraLayoutBoundary *)items count:(int)count {
    CGRect rect = CGRectNull;
    int index = 0;
    while (items != NULL && index < count) {
        if (items[index].offset >= start && items[index].offset < end) {
            CGRect glyph = [self rectFromLayout:items count:count codePoint:(NSInteger)items[index].offset];
            rect = CGRectIsNull(rect) ? glyph : CGRectUnion(rect, glyph);
        }
        index += 1;
    }
    if (CGRectIsNull(rect)) {
        rect = [self rectFromLayout:items count:count codePoint:start];
    }
    return rect;
}

- (NSArray *)currentWordElements {
    AuraLayoutBoundary *items = NULL;
    int count = 0;
    int64_t layoutRevision = 0;
    NSString *text = self.editingText != nil ? self.editingText : @"";
    AuraTextCursor cursor = aura_ns_cursor(text);
    NSMutableArray *built = nil;
    int64_t index = 0;
    pthread_mutex_lock(&g_layout[self.slot].mu);
    layoutRevision = g_layout[self.slot].revision;
    pthread_mutex_unlock(&g_layout[self.slot].mu);
    if (self.wordElements != nil && self.wordElementTextRevision == self.textRevision && self.wordElementLayoutRevision == layoutRevision) {
        return self.wordElements;
    }
    count = [self copyLayout:&items];
    if (count <= 0 || text.length == 0) {
        free(items);
        self.wordElements = @[];
        self.wordElementTextRevision = self.textRevision;
        self.wordElementLayoutRevision = layoutRevision;
        return self.wordElements;
    }
    built = [[NSMutableArray alloc] init];
    while (index >= 0 && index < cursor.length) {
        AuraTextSpan span;
        NSInteger utf16Start = 0;
        NSInteger utf16End = 0;
        NSInteger codeStart = 0;
        NSInteger codeEnd = 0;
        NSString *word = nil;
        CGRect local;
        AuraWordElement *element = nil;
        if (aura_text_word_at(&cursor, index, &span) == 0 || span.end <= span.start) break;
        utf16Start = (NSInteger)span.start;
        utf16End = (NSInteger)span.end;
        if (utf16Start < 0 || utf16End > (NSInteger)text.length || utf16End <= utf16Start) break;
        word = [text substringWithRange:NSMakeRange((NSUInteger)utf16Start, (NSUInteger)(utf16End - utf16Start))];
        codeStart = [self codePointOffsetForUTF16:utf16Start];
        codeEnd = [self codePointOffsetForUTF16:utf16End];
        local = [self rectForCodeRange:codeStart end:codeEnd items:items count:count];
        element = [[AuraWordElement alloc] initWithAccessibilityContainer:self];
        element.codeStart = codeStart;
        element.codeEnd = codeEnd;
        element.accessibilityLabel = word;
        element.accessibilityValue = nil;
        element.accessibilityTraits = UIAccessibilityTraitStaticText;
        element.accessibilityFrameInContainerSpace = local;
        element.accessibilityFrame = [self screenRectForLocal:local];
        if (@available(iOS 17.0, *)) {
            element.accessibilityRespondsToUserInteraction = YES;
        }
        [built addObject:element];
        [element release];
        int64_t next = aura_text_next_word_start(&cursor, span.start);
        if (next == AURA_TEXT_NONE || next <= index) break;
        index = next;
    }
    free(items);
    self.wordElements = built;
    self.wordElementTextRevision = self.textRevision;
    self.wordElementLayoutRevision = layoutRevision;
    [built release];
    return self.wordElements != nil ? self.wordElements : @[];
}

- (void)publishWordElements {
    int64_t previous = self.wordElementLayoutRevision;
    NSArray *words = [self currentWordElements];
    if (words.count > 0 && self.wordElementLayoutRevision != previous && [self exploresWords]) {
        UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification, nil);
    }
}

- (void)applyAccessibility:(AuraAccessibilityElement *)element atIndex:(NSInteger)index {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    const char *role = aura_shell_ax_role(self.slot, (int)index);
    const char *trait = aura_ax_ios_trait(role);
    const char *label = aura_shell_ax_label(self.slot, (int)index);
    const char *value = aura_shell_ax_value(self.slot, (int)index);
    UIAccessibilityTraits traits = UIAccessibilityTraitNone;
    NSString *name = [NSString stringWithUTF8String:label != NULL ? label : ""];
    NSString *detail = nil;
    int interactive = 0;
    if (trait == NULL) {
        trait = "none";
    }
    element.slot = self.slot;
    element.axIndex = (int)index;
    element.nodeId = aura_shell_ax_node_id(self.slot, (int)index);
    if (value != NULL && value[0] != 0) {
        detail = [NSString stringWithUTF8String:value];
    }
    if (name.length == 0) {
        element.accessibilityLabel = detail;
        element.accessibilityValue = nil;
    } else if (detail != nil && ![detail isEqualToString:name]) {
        element.accessibilityLabel = name;
        element.accessibilityValue = detail;
    } else {
        element.accessibilityLabel = name;
        element.accessibilityValue = nil;
    }
    aura_shell_ax_frame(self.slot, (int)index, &x, &y, &w, &h);
    CGRect local = CGRectMake((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)h);
    element.accessibilityFrameInContainerSpace = local;
    element.accessibilityFrame = [self screenRectForLocal:local];
    if (strcmp(trait, "button") == 0 || strcmp(trait, "toggle") == 0) {
        traits = UIAccessibilityTraitButton;
        interactive = 1;
    } else if (strcmp(trait, "static-text") == 0) {
        traits = UIAccessibilityTraitStaticText;
    } else if (strcmp(trait, "image") == 0) {
        traits = UIAccessibilityTraitImage;
    } else if (strcmp(trait, "adjustable") == 0) {
        traits = UIAccessibilityTraitAdjustable;
        interactive = 1;
    } else if (strcmp(trait, "header") == 0) {
        traits = UIAccessibilityTraitHeader | UIAccessibilityTraitStaticText;
    } else if (strcmp(trait, "text-field") == 0) {
        traits = UIAccessibilityTraitNone;
        interactive = 1;
    }
    if (aura_shell_ax_heading(self.slot, (int)index)) {
        traits |= UIAccessibilityTraitHeader;
        interactive = 1;
    }
    if (aura_shell_ax_checked(self.slot, (int)index)) {
        traits |= UIAccessibilityTraitSelected;
    }
    element.accessibilityTraits = traits;
    if (@available(iOS 17.0, *)) {
        element.accessibilityRespondsToUserInteraction = interactive != 0;
    }
}

- (UIAccessibilityCustomRotorItemResult *)headingResultForPredicate:(UIAccessibilityCustomRotorSearchPredicate *)predicate {
    NSArray *items = [self expandedAccessibilityItems];
    NSMutableArray *headings = [NSMutableArray array];
    NSInteger position = -1;
    NSInteger index = 0;
    id current = predicate.currentItem.targetElement;
    id chosen = nil;
    while (index < (NSInteger)items.count) {
        id item = items[(NSUInteger)index];
        if (([item accessibilityTraits] & UIAccessibilityTraitHeader) != 0) {
            [headings addObject:item];
        }
        if (current != nil && item == current) {
            position = index;
        }
        index += 1;
    }
    if (headings.count == 0) {
        return nil;
    }
    if (position < 0) {
        chosen = predicate.searchDirection == UIAccessibilityCustomRotorDirectionNext ? headings.firstObject : headings.lastObject;
    } else if (predicate.searchDirection == UIAccessibilityCustomRotorDirectionNext) {
        index = 0;
        while (index < (NSInteger)headings.count) {
            id heading = headings[(NSUInteger)index];
            if ([items indexOfObjectIdenticalTo:heading] > position) {
                chosen = heading;
                break;
            }
            index += 1;
        }
    } else {
        index = (NSInteger)headings.count - 1;
        while (index >= 0) {
            id heading = headings[(NSUInteger)index];
            if ([items indexOfObjectIdenticalTo:heading] < position) {
                chosen = heading;
                break;
            }
            index -= 1;
        }
    }
    if (chosen == nil) {
        return nil;
    }
    return [[[UIAccessibilityCustomRotorItemResult alloc] initWithTargetElement:chosen targetRange:nil] autorelease];
}

- (NSArray<UIAccessibilityCustomRotor *> *)accessibilityCustomRotors {
    if (self.headingRotor == nil) {
        __unsafe_unretained AuraCanvas *canvas = self;
        self.headingRotor = [[[UIAccessibilityCustomRotor alloc] initWithSystemType:UIAccessibilityCustomSystemRotorTypeHeading itemSearchBlock:^UIAccessibilityCustomRotorItemResult *(UIAccessibilityCustomRotorSearchPredicate *predicate) {
            return [canvas headingResultForPredicate:predicate];
        }] autorelease];
    }
    return @[self.headingRotor];
}

- (BOOL)isAccessibilityElement {
    return NO;
}

- (void)setIsAccessibilityElement:(BOOL)isAccessibilityElement {
    (void)isAccessibilityElement;
}

- (void)applyTextInputAtIndex:(NSInteger)index {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    const char *label = aura_shell_ax_label(self.slot, (int)index);
    const char *value = aura_shell_ax_value(self.slot, (int)index);
    NSString *name = [NSString stringWithUTF8String:label != NULL ? label : ""];
    NSString *detail = [NSString stringWithUTF8String:value != NULL ? value : ""];
    AuraTextInputView *input = self.textInput;
    aura_shell_ax_frame(self.slot, (int)index, &x, &y, &w, &h);
    input.axIndex = (int)index;
    input.axName = name.length > 0 ? name : nil;
    input.axValue = detail.length > 0 ? detail : nil;
    input.accessibilityActive = w >= 1.0 && h >= 1.0;
    if (input.accessibilityActive) {
        input.frame = CGRectMake((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)h);
    }
}

- (id)shellAccessibilityElementAtIndex:(NSInteger)index {
    AuraAccessibilityElement *element = nil;
    int64_t node_id = 0;
    const char *role = NULL;
    if (index < 0 || index >= aura_shell_ax_count(self.slot)) {
        return nil;
    }
    role = aura_shell_ax_role(self.slot, (int)index);
    if (role != NULL && strcmp(role, "text-field") == 0) {
        [self applyTextInputAtIndex:index];
        return self.textInput.accessibilityActive ? self.textInput : nil;
    }
    if (self.axElements == nil) {
        self.axElements = [[NSMutableArray alloc] init];
    }
    while ((NSInteger)self.axElements.count <= index) {
        [self.axElements addObject:[NSNull null]];
    }
    node_id = aura_shell_ax_node_id(self.slot, (int)index);
    if (self.axElements[(NSUInteger)index] != [NSNull null]) {
        element = self.axElements[(NSUInteger)index];
        if (element.nodeId == node_id && node_id != 0) {
            [self applyAccessibility:element atIndex:index];
            return element;
        }
    }
    element = [[AuraAccessibilityElement alloc] initWithAccessibilityContainer:self];
    [self applyAccessibility:element atIndex:index];
    [self.axElements replaceObjectAtIndex:(NSUInteger)index withObject:element];
    [element release];
    return element;
}

- (NSArray *)expandedAccessibilityItems {
    NSInteger count = aura_shell_ax_count(self.slot);
    NSMutableArray *items = [NSMutableArray arrayWithCapacity:(NSUInteger)MAX(count, 0)];
    NSInteger index = 0;
    [self trimAccessibilityCache];
    while (index < count) {
        const char *role = aura_shell_ax_role(self.slot, (int)index);
        if (role != NULL && strcmp(role, "text-field") == 0) {
            NSArray *words = nil;
            [self applyTextInputAtIndex:index];
            if ([self exploresWords]) {
                words = [self currentWordElements];
                if (words.count > 0) {
                    self.textInput.accessibilityActive = NO;
                    [items addObjectsFromArray:words];
                    index += 1;
                    continue;
                }
            }
            if (self.textInput.accessibilityActive) {
                [items addObject:self.textInput];
            }
        } else {
            id element = [self shellAccessibilityElementAtIndex:index];
            if (element != nil) {
                [items addObject:element];
            }
        }
        index += 1;
    }
    return items;
}

- (NSInteger)accessibilityElementCount {
    return (NSInteger)[self expandedAccessibilityItems].count;
}

- (NSArray *)accessibilityElements {
    return [self expandedAccessibilityItems];
}

- (nullable id)accessibilityElementAtIndex:(NSInteger)index {
    NSArray *items = [self expandedAccessibilityItems];
    if (index < 0 || index >= (NSInteger)items.count) {
        return nil;
    }
    return items[(NSUInteger)index];
}

- (NSInteger)indexOfAccessibilityElement:(id)element {
    NSUInteger found = NSNotFound;
    if (element == nil) {
        return NSNotFound;
    }
    found = [[self expandedAccessibilityItems] indexOfObjectIdenticalTo:element];
    if (found == NSNotFound) {
        return NSNotFound;
    }
    return (NSInteger)found;
}

@end

static void on_main(void (^block)(void)) {
    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_sync(dispatch_get_main_queue(), block);
    }
}

void aura_host_open(int slot, double width, double height, const char *title, int visible) {
    (void)width;
    (void)height;
    (void)title;
    (void)visible;
    if (slot < 0 || slot >= 4) {
        return;
    }
    on_main(^{
        AuraCanvas *canvas = g_canvas[slot];
        UIView *root = nil;
        if (canvas == nil) {
            canvas = [[AuraCanvas alloc] initWithSlot:slot];
            g_canvas[slot] = canvas;
        }
        if (g_window == nil) {
            g_window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
            g_window.rootViewController = [UIViewController new];
            g_window.backgroundColor = [UIColor blackColor];
            [g_window makeKeyAndVisible];
        }
        root = g_window.rootViewController.view;
        if (canvas.superview != root) {
            canvas.frame = root.bounds;
            canvas.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
            [root addSubview:canvas];
        }
        if (canvas.keyboardObserver == nil) {
            canvas.keyboardObserver = [[NSNotificationCenter defaultCenter] addObserverForName:UIKeyboardWillChangeFrameNotification object:nil queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification *note) {
                CGRect frame = [note.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
                CGRect local = [canvas convertRect:frame fromView:nil];
                CGFloat overlap = CGRectIntersection(canvas.bounds, local).size.height;
                NSTimeInterval duration = [note.userInfo[UIKeyboardAnimationDurationUserInfoKey] doubleValue];
                int curve = aura_keyboard_curve((int)[note.userInfo[UIKeyboardAnimationCurveUserInfoKey] integerValue]);
                [canvas setKeyboardHeight:overlap duration:duration curve:curve];
            }];
        }
        if (canvas.preferenceObservers.count == 0) {
            NSArray<NSNotificationName> *names = @[
                UIContentSizeCategoryDidChangeNotification,
                UIAccessibilityBoldTextStatusDidChangeNotification,
                UIAccessibilityReduceMotionStatusDidChangeNotification,
                UIAccessibilityVoiceOverStatusDidChangeNotification,
                UIAccessibilityDarkerSystemColorsStatusDidChangeNotification
            ];
            for (NSNotificationName name in names) {
                id observer = [[NSNotificationCenter defaultCenter] addObserverForName:name object:nil queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification *note) {
                    if ([note.name isEqualToString:UIAccessibilityVoiceOverStatusDidChangeNotification] && UIAccessibilityIsVoiceOverRunning()) {
                        canvas.textInput.voiceOverEditing = NO;
                        [canvas.textInput resignFirstResponder];
                    }
                    [canvas publishEnvironment];
                    [canvas publishSize];
                }];
                [canvas.preferenceObservers addObject:observer];
            }
        }
    });
}

void aura_host_close(int slot) {
    if (slot < 0 || slot >= 4) {
        return;
    }
    on_main(^{
        AuraCanvas *canvas = g_canvas[slot];
        if (canvas == nil) {
            return;
        }
        [canvas.link invalidate];
        canvas.link = nil;
        if (canvas.keyboardObserver != nil) {
            [[NSNotificationCenter defaultCenter] removeObserver:canvas.keyboardObserver];
            canvas.keyboardObserver = nil;
        }
        for (id observer in canvas.preferenceObservers) {
            [[NSNotificationCenter defaultCenter] removeObserver:observer];
        }
        [canvas.preferenceObservers removeAllObjects];
        pthread_mutex_lock(&g_layout[slot].mu);
        free(g_layout[slot].items);
        g_layout[slot].items = NULL;
        g_layout[slot].count = 0;
        pthread_mutex_unlock(&g_layout[slot].mu);
        [canvas.textInput resignFirstResponder];
        [canvas removeFromSuperview];
        g_canvas[slot] = nil;
        [canvas release];
    });
}

void aura_host_present(int slot) {
    if (slot < 0 || slot >= 4) {
        return;
    }
    on_main(^{
        [g_canvas[slot] setNeedsDisplay];
    });
}

void aura_host_accessibility_update(int slot, int change) {
    if (slot < 0 || slot >= 4) {
        return;
    }
    on_main(^{
        id focus = nil;
        os_log(OS_LOG_DEFAULT, "aura-ax %d change %d", aura_shell_ax_count(slot), change);
        if (change == 0 || g_canvas[slot] == nil) {
            return;
        }
        if (change == 2) {
            focus = [g_canvas[slot] accessibilityElementAtIndex:0];
            if (focus == nil) {
                focus = g_canvas[slot];
            }
            UIAccessibilityPostNotification(UIAccessibilityScreenChangedNotification, focus);
            return;
        }
        UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification, nil);
    });
}

void aura_host_accessibility(int slot) {
    aura_host_accessibility_update(slot, 1);
}

void aura_host_text_focus(int slot, int enabled) {
    if (slot < 0 || slot >= 4) {
        return;
    }
    on_main(^{
        AuraTextInputView *input = g_canvas[slot].textInput;
        if (input == nil) {
            return;
        }
        if (enabled) {
            if (!input.isFirstResponder && (!UIAccessibilityIsVoiceOverRunning() || input.voiceOverEditing)) {
                [input becomeFirstResponder];
            }
        } else {
            input.voiceOverEditing = NO;
            [input resignFirstResponder];
        }
    });
}

void aura_host_text_state(int slot, const char *text, int64_t generation, int64_t revision, int64_t selection_start, int64_t selection_end, int64_t composing_start, int64_t composing_end) {
    NSString *value = [[NSString alloc] initWithUTF8String:text != NULL ? text : ""];
    if (slot < 0 || slot >= 4) {
        [value release];
        return;
    }
    if (value == nil) {
        value = [@"" retain];
    }
    on_main(^{
        AuraCanvas *canvas = g_canvas[slot];
        if (canvas == nil) {
            return;
        }
        if (generation < canvas.textGeneration || (generation == canvas.textGeneration && revision < canvas.textRevision)) {
            return;
        }
        BOOL textChanged = ![canvas.editingText isEqualToString:value] || canvas.composingStart != composing_start || canvas.composingEnd != composing_end;
        BOOL selectionChanged = canvas.selectionStart != selection_start || canvas.selectionEnd != selection_end;
        if (textChanged) [canvas notifyTextWillChange];
        if (selectionChanged) [canvas notifySelectionWillChange];
        canvas.editingText = value;
        canvas.textGeneration = generation;
        canvas.textRevision = revision;
        canvas.selectionStart = selection_start;
        canvas.selectionEnd = selection_end;
        canvas.composingStart = composing_start;
        canvas.composingEnd = composing_end;
        if (selectionChanged) [canvas notifySelectionDidChange];
        if (textChanged) [canvas notifyTextDidChange];
    });
    [value release];
}

void aura_host_text_geometry(int slot, double caret_x, double caret_y, double caret_width, double caret_height, double editable_x, double editable_y, double editable_width, double editable_height) {
    if (slot < 0 || slot >= 4) {
        return;
    }
    on_main(^{
        AuraCanvas *canvas = g_canvas[slot];
        if (canvas == nil) {
            return;
        }
        canvas.caretRect = CGRectMake(caret_x, caret_y, caret_width, caret_height);
        canvas.editableRect = CGRectMake(editable_x, editable_y, editable_width, editable_height);
        if (canvas.textInput != nil && editable_width >= 1.0 && editable_height >= 1.0) {
            canvas.textInput.frame = canvas.editableRect;
        }
        canvas.caretRevision = canvas.textRevision;
        [canvas refreshTextContentRect];
        [canvas refreshCaret];
    });
}

void aura_host_text_configuration(int slot, int input_type, int action, int multiline, int secure, int autocorrect, int capitalization, const char *autofill_hints) {
    NSString *hints = [[NSString alloc] initWithUTF8String:autofill_hints != NULL ? autofill_hints : ""];
    if (slot < 0 || slot >= 4) {
        [hints release];
        return;
    }
    if (hints == nil) {
        hints = [@"" retain];
    }
    on_main(^{
        AuraCanvas *canvas = g_canvas[slot];
        if (canvas == nil) {
            return;
        }
        canvas.inputTypeConfig = input_type;
        canvas.actionConfig = action;
        canvas.multilineConfig = multiline != 0;
        canvas.secureConfig = secure != 0;
        canvas.autocorrectConfig = autocorrect != 0;
        canvas.capitalizationConfig = capitalization;
        canvas.autofillHints = hints;
        NSString *signature = [NSString stringWithFormat:@"%d|%d|%d|%d|%d|%d|%@", input_type, action, multiline, secure, autocorrect, capitalization, hints];
        BOOL configurationChanged = ![signature isEqualToString:canvas.inputConfigurationSignature];
        canvas.inputConfigurationSignature = signature;
        if (configurationChanged && canvas.textInput.isFirstResponder) {
            [canvas.textInput reloadInputViews];
        }
    });
    [hints release];
}

void aura_host_system_back_result(int slot, int handled) {
    (void)slot;
    (void)handled;
}

int aura_host_capabilities(int slot) {
    (void)slot;
    return AURA_CAPABILITY_INTERACTIVE_EDGE_BACK;
}

int aura_host_clipboard_set(const char *text) {
    NSString *value = [NSString stringWithUTF8String:text != NULL ? text : ""];
    __block int ok = 0;
    if (value == nil) {
        return -1;
    }
    on_main(^{
        [UIPasteboard generalPasteboard].string = value;
        ok = [UIPasteboard generalPasteboard].string != nil;
    });
    return ok ? 0 : -1;
}

const char *aura_host_clipboard_get(void) {
    __block char *copy = NULL;
    __block size_t length = 0;
    on_main(^{
        NSString *text = [UIPasteboard generalPasteboard].string;
        NSData *data = [(text != nil ? text : @"") dataUsingEncoding:NSUTF8StringEncoding];
        length = data != nil ? data.length : 0;
        copy = (char *)malloc(length + 1);
        if (copy != NULL) {
            if (length > 0 && data.bytes != NULL) memcpy(copy, data.bytes, length);
            copy[length] = 0;
        }
    });
    free(g_clip);
    g_clip = copy != NULL ? copy : strdup("");
    g_clip_length = copy != NULL ? length : 0;
    return g_clip;
}

size_t aura_host_clipboard_length(void) {
    return g_clip_length;
}

void aura_host_text_geometry_snapshot(int slot, int64_t target, int64_t revision, int count) {
    AuraLayoutBoundary *items = NULL;
    int i = 0;
    if (slot < 0 || slot >= 4 || count < 0) {
        return;
    }
    if (count > 0) {
        items = (AuraLayoutBoundary *)calloc((size_t)count, sizeof(AuraLayoutBoundary));
        if (items == NULL) {
            return;
        }
        for (i = 0; i < count; i++) {
            if (aura_shell_text_geometry_boundary(slot, i, &items[i].offset, &items[i].x, &items[i].y, &items[i].height) != 0) {
                free(items);
                return;
            }
        }
    }
    pthread_mutex_lock(&g_layout[slot].mu);
    free(g_layout[slot].items);
    g_layout[slot].items = items;
    g_layout[slot].count = count;
    g_layout[slot].target = target;
    g_layout[slot].revision = revision;
    pthread_mutex_unlock(&g_layout[slot].mu);
    on_main(^{
        AuraCanvas *canvas = g_canvas[slot];
        if (canvas != nil) {
            [canvas refreshTextContentRect];
            [canvas refreshCaret];
            [canvas publishWordElements];
        }
    });
}

static NSString *aura_make_directory(NSSearchPathDirectory which) {
    NSArray<NSString *> *paths = NSSearchPathForDirectoriesInDomains(which, NSUserDomainMask, YES);
    NSString *path = paths.firstObject;
    if (path != nil) {
        [[NSFileManager defaultManager] createDirectoryAtPath:path withIntermediateDirectories:YES attributes:nil error:nil];
    }
    return path;
}

static void aura_exclude_backup(NSString *path) {
    NSURL *url = [NSURL fileURLWithPath:path isDirectory:YES];
    [url setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:nil];
}

static void aura_start_nox(void) {
    NSString *documents = aura_make_directory(NSDocumentDirectory);
    NSString *support = aura_make_directory(NSApplicationSupportDirectory);
    NSString *cache = aura_make_directory(NSCachesDirectory);
    NSString *temp = NSTemporaryDirectory();
    NSString *assets = NSBundle.mainBundle.bundlePath;
    AuraStorageSpec spec;
    if (documents == nil || support == nil || cache == nil || temp == nil || assets == nil) {
        os_log(OS_LOG_DEFAULT, "aura storage roots are missing");
        return;
    }
    [[NSFileManager defaultManager] createDirectoryAtPath:temp withIntermediateDirectories:YES attributes:nil error:nil];
    if (aura_storage_is_backup_excluded(AURA_STORAGE_CACHE)) {
        aura_exclude_backup(cache);
    }
    if (aura_storage_is_backup_excluded(AURA_STORAGE_TEMP)) {
        aura_exclude_backup(temp);
    }
    spec.documents = documents.UTF8String;
    spec.support = support.UTF8String;
    spec.cache = cache.UTF8String;
    spec.temp_dir = temp.UTF8String;
    spec.assets = assets.UTF8String;
    aura_mobile_runtime_start(&spec, aura_runtime_log);
}

@interface AuraAppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation AuraAppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    (void)application;
    (void)launchOptions;
    return YES;
}

- (void)applicationDidReceiveMemoryWarning:(UIApplication *)application {
    uint64_t timestamp = aura_now_ns();
    int i = 0;
    (void)application;
    while (i < 4) {
        if (g_canvas[i] != nil) {
            aura_shell_memory_pressure(i, AURA_MEMORY_PRESSURE_CRITICAL, timestamp);
        }
        i += 1;
    }
    aura_storage_purge_cache();
    os_log(OS_LOG_DEFAULT, "aura-memory");
}

- (void)applicationWillTerminate:(UIApplication *)application {
    (void)application;
    aura_post_lifecycle_to_open_canvases(AURA_LIFECYCLE_TERMINATED, AURA_LIFECYCLE_REASON_SYSTEM);
    aura_mobile_runtime_request_stop();
}

@end

@interface AuraSceneDelegate : NSObject <UIWindowSceneDelegate>
@end

@implementation AuraSceneDelegate

- (NSUserActivity *)stateRestorationActivityForScene:(UIScene *)scene {
    NSUserActivity *activity = nil;
    (void)scene;
    activity = [[NSUserActivity alloc] initWithActivityType:kAuraSceneActivity];
    activity.userInfo = @{ @"format": kAuraSceneFormat, @"version": @1 };
    return [activity autorelease];
}

- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    NSUserActivity *saved = session.stateRestorationActivity;
    (void)options;
    if (saved != nil && [saved.activityType isEqualToString:kAuraSceneActivity]) {
        id version = saved.userInfo[@"version"];
        id format = saved.userInfo[@"format"];
        const char *format_utf8 = [format isKindOfClass:[NSString class]] ? [(NSString *)format UTF8String] : NULL;
        int version_number = [version isKindOfClass:[NSNumber class]] ? [(NSNumber *)version intValue] : 0;
        if (!aura_scene_restoration_prepare(format_utf8, version_number)) {
            os_log(OS_LOG_DEFAULT, "aura scene restoration rejected");
        }
    }
    if (![scene isKindOfClass:[UIWindowScene class]]) {
        return;
    }
    g_window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    g_window.rootViewController = [UIViewController new];
    g_window.backgroundColor = [UIColor blackColor];
    [g_window makeKeyAndVisible];
    for (UIOpenURLContext *context in options.URLContexts) {
        aura_post_platform_route(context.URL.absoluteString, AURA_ROUTE_APP_LINK);
    }
    for (NSUserActivity *activity in options.userActivities) {
        if ([activity.activityType isEqualToString:NSUserActivityTypeBrowsingWeb]) {
            aura_post_platform_route(activity.webpageURL.absoluteString, AURA_ROUTE_UNIVERSAL_LINK);
        }
    }
    if (g_nox_started) {
        return;
    }
    g_nox_started = 1;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        aura_start_nox();
    });
}

- (void)scene:(UIScene *)scene openURLContexts:(NSSet<UIOpenURLContext *> *)URLContexts {
    (void)scene;
    for (UIOpenURLContext *context in URLContexts) {
        aura_post_platform_route(context.URL.absoluteString, AURA_ROUTE_APP_LINK);
    }
}

- (void)scene:(UIScene *)scene continueUserActivity:(NSUserActivity *)userActivity {
    (void)scene;
    if ([userActivity.activityType isEqualToString:NSUserActivityTypeBrowsingWeb]) {
        aura_post_platform_route(userActivity.webpageURL.absoluteString, AURA_ROUTE_UNIVERSAL_LINK);
    }
}

- (void)sceneDidEnterBackground:(UIScene *)scene {
    (void)scene;
    int i = 0;
    while (i < 4) {
        [g_canvas[i] setDisplayLinkSuspended:YES];
        i += 1;
    }
    aura_post_lifecycle_to_open_canvases(AURA_LIFECYCLE_PAUSED, AURA_LIFECYCLE_REASON_SYSTEM);
    os_log(OS_LOG_DEFAULT, "aura-background");
}

- (void)sceneWillEnterForeground:(UIScene *)scene {
    (void)scene;
    aura_post_lifecycle_to_open_canvases(AURA_LIFECYCLE_INACTIVE, AURA_LIFECYCLE_REASON_SYSTEM);
    int i = 0;
    while (i < 4) {
        g_canvas[i].logFrame = 1;
        [g_canvas[i] setDisplayLinkSuspended:NO];
        i += 1;
    }
    os_log(OS_LOG_DEFAULT, "aura-foreground");
}

- (void)sceneDidBecomeActive:(UIScene *)scene {
    int i = 0;
    (void)scene;
    while (i < 4) {
        [g_canvas[i] publishEnvironment];
        [g_canvas[i] setDisplayLinkSuspended:NO];
        i += 1;
    }
    aura_post_lifecycle_to_open_canvases(AURA_LIFECYCLE_RESUMED, AURA_LIFECYCLE_REASON_SYSTEM);
}

- (void)sceneWillResignActive:(UIScene *)scene {
    (void)scene;
    int i = 0;
    while (i < 4) {
        [g_canvas[i] cancelTrackedPointers];
        [g_canvas[i] setDisplayLinkSuspended:YES];
        i += 1;
    }
    aura_post_lifecycle_to_open_canvases(AURA_LIFECYCLE_INACTIVE, AURA_LIFECYCLE_REASON_INTERRUPTION);
}

- (void)sceneDidDisconnect:(UIScene *)scene {
    (void)scene;
    aura_post_lifecycle_to_open_canvases(AURA_LIFECYCLE_DETACHED, AURA_LIFECYCLE_REASON_SYSTEM);
    aura_mobile_runtime_request_stop();
}

@end

int main(int argc, char **argv) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AuraAppDelegate class]));
    }
}
