#import <UIKit/UIKit.h>

#include "events.h"
#include "paint.h"

static void (*aura_ios_post)(const AuraMobileEvent *event);

void aura_ios_set_poster(void (*post)(const AuraMobileEvent *event)) {
    aura_ios_post = post;
}

@interface AuraHostView : UIView
@end

@implementation AuraHostView

- (void)sendTouch:(UITouch *)touch phase:(int)phase {
    CGPoint point = [touch locationInView:self];
    UIEdgeInsets inset = self.safeAreaInsets;
    AuraMobileEvent event;
    if (!aura_mobile_pointer(&event, phase, point.x * self.contentScaleFactor, point.y * self.contentScaleFactor, self.contentScaleFactor, inset.left, inset.top)) return;
    if (aura_ios_post != NULL) aura_ios_post(&event);
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;
    for (UITouch *touch in touches) [self sendTouch:touch phase:AURA_PHASE_BEGAN];
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;
    for (UITouch *touch in touches) [self sendTouch:touch phase:AURA_PHASE_MOVED];
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;
    for (UITouch *touch in touches) [self sendTouch:touch phase:AURA_PHASE_ENDED];
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;
    for (UITouch *touch in touches) [self sendTouch:touch phase:AURA_PHASE_CANCELLED];
}

- (void)drawRect:(CGRect)rect {
    (void)rect;
    const uint8_t *pixels = aura_paint_pixels();
    if (pixels == NULL) return;
    int64_t width = aura_paint_pixel_width();
    int64_t height = aura_paint_pixel_height();
    int64_t stride = aura_paint_stride();
    if (width <= 0 || height <= 0 || stride < width * 4) return;
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, pixels, (size_t)(stride * height), NULL);
    CGImageRef image = CGImageCreate((size_t)width, (size_t)height, 8, 32, (size_t)stride, space, kCGBitmapByteOrder32Big | kCGImageAlphaPremultipliedLast, provider, NULL, false, kCGRenderingIntentDefault);
    CGContextRef context = UIGraphicsGetCurrentContext();
    if (context != NULL && image != NULL) {
        CGContextDrawImage(context, self.bounds, image);
    }
    if (image != NULL) CGImageRelease(image);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(space);
}

@end
