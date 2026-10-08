#include "paint.h"

#include "include/codec/SkJpegDecoder.h"
#include "include/codec/SkPngDecoder.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontArguments.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"
#include "include/ports/SkFontMgr_empty.h"

#include <chrono>
#include <cstring>
#include <memory>
#include <vector>

namespace {

constexpr int kMaxImages = 64;
constexpr int kMaxOpacity = 32;

struct Picture {
    int alive;
    int width;
    int height;
    sk_sp<SkImage> image;
};

sk_sp<SkSurface> g_surface;
double g_scale = 1.0;
int g_opacity = 0;
Picture g_pictures[kMaxImages];
std::vector<uint8_t> g_bytes;
sk_sp<SkTypeface> g_regular;
sk_sp<SkTypeface> g_bold;
int g_font_ready = 0;

double logical_scale(void) {
    if (g_scale < 1.0) {
        return 1.0;
    }
    return g_scale;
}

void ensure_font(void) {
    sk_sp<SkFontMgr> manager;
    sk_sp<SkData> data;
    if (g_font_ready) {
        return;
    }
    g_font_ready = 1;
    data = SkData::MakeFromFileName("native/fonts/NotoSans-Regular.ttf");
    if (!data) {
        return;
    }
    manager = SkFontMgr_New_Custom_Empty();
    if (!manager) {
        return;
    }
    g_regular = manager->makeFromData(data, 0);
    if (!g_regular) {
        return;
    }
    SkFontArguments::VariationPosition::Coordinate coord;
    coord.axis = SkSetFourByteTag('w', 'g', 'h', 't');
    coord.value = 700.0f;
    SkFontArguments args;
    args.setVariationDesignPosition({&coord, 1});
    g_bold = g_regular->makeClone(args);
    if (!g_bold) {
        g_bold = g_regular;
    }
}

SkFont make_font(double size, int64_t weight) {
    sk_sp<SkTypeface> face = weight >= 600 ? g_bold : g_regular;
    SkFont font(face, static_cast<float>(size * logical_scale()));
    font.setEdging(SkFont::Edging::kAntiAlias);
    font.setSubpixel(true);
    if (weight >= 600) {
        font.setEmbolden(true);
    }
    return font;
}

SkColor sk_color(int64_t argb) {
    return static_cast<SkColor>(static_cast<uint32_t>(argb));
}

std::unique_ptr<SkCodec> decode_data(sk_sp<SkData> data) {
    SkCodec::Result result = SkCodec::kInvalidParameters;
    std::unique_ptr<SkCodec> codec;
    if (!data) {
        return nullptr;
    }
    if (SkPngDecoder::IsPng(data->data(), data->size())) {
        codec = SkPngDecoder::Decode(data, &result);
        return codec;
    }
    if (SkJpegDecoder::IsJpeg(data->data(), data->size())) {
        codec = SkJpegDecoder::Decode(data, &result);
        return codec;
    }
    return nullptr;
}

int64_t store_image(sk_sp<SkImage> image, int width, int height) {
    int i = 0;
    if (!image || width <= 0 || height <= 0) {
        return 0;
    }
    for (i = 0; i < kMaxImages; i++) {
        if (!g_pictures[i].alive) {
            g_pictures[i].alive = 1;
            g_pictures[i].width = width;
            g_pictures[i].height = height;
            g_pictures[i].image = std::move(image);
            return static_cast<int64_t>(i + 1);
        }
    }
    return 0;
}

int64_t store_codec(std::unique_ptr<SkCodec> codec) {
    SkImageInfo info;
    SkBitmap bitmap;
    SkCodec::Result result;
    sk_sp<SkImage> image;
    if (!codec) {
        return 0;
    }
    info = codec->getInfo().makeColorType(kRGBA_8888_SkColorType).makeAlphaType(kPremul_SkAlphaType);
    if (info.width() <= 0 || info.height() <= 0) {
        return 0;
    }
    if (!bitmap.tryAllocPixels(info)) {
        return 0;
    }
    result = codec->getPixels(info, bitmap.getPixels(), bitmap.rowBytes());
    if (result != SkCodec::kSuccess) {
        return 0;
    }
    image = SkImages::RasterFromBitmap(bitmap);
    return store_image(image, info.width(), info.height());
}

Picture *picture_get(int64_t handle) {
    if (handle <= 0 || handle > kMaxImages) {
        return nullptr;
    }
    if (!g_pictures[handle - 1].alive || !g_pictures[handle - 1].image) {
        return nullptr;
    }
    return &g_pictures[handle - 1];
}

}  // namespace

extern "C" int aura_paint_begin(int64_t width, int64_t height, double scale) {
    SkImageInfo info;
    if (width <= 0 || height <= 0) {
        return -1;
    }
    if (width > 16384 || height > 16384) {
        return -1;
    }
    g_scale = scale < 1.0 ? 1.0 : scale;
    g_opacity = 0;
    ensure_font();
    info = SkImageInfo::Make(static_cast<int>(width), static_cast<int>(height), kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    g_surface = SkSurfaces::Raster(info);
    if (!g_surface) {
        return -1;
    }
    return 0;
}

extern "C" int aura_paint_clear(int64_t argb) {
    if (!g_surface) {
        return -1;
    }
    g_surface->getCanvas()->clear(sk_color(argb));
    return 0;
}

extern "C" int aura_paint_rect(double x, double y, double w, double h, int64_t argb) {
    SkPaint paint;
    double scale;
    if (!g_surface) {
        return -1;
    }
    scale = logical_scale();
    paint.setColor(sk_color(argb));
    paint.setAntiAlias(true);
    g_surface->getCanvas()->drawRect(SkRect::MakeXYWH(static_cast<float>(x * scale), static_cast<float>(y * scale), static_cast<float>(w * scale), static_cast<float>(h * scale)), paint);
    return 0;
}

extern "C" int aura_paint_text(double x, double y, const char *text, double size, int64_t weight, int64_t argb, const char *family) {
    SkFont font;
    SkFontMetrics metrics;
    SkPaint paint;
    double scale;
    (void)family;
    if (!g_surface) {
        return -1;
    }
    if (text == nullptr) {
        text = "";
    }
    ensure_font();
    scale = logical_scale();
    font = make_font(size, weight);
    font.getMetrics(&metrics);
    paint.setColor(sk_color(argb));
    paint.setAntiAlias(true);
    g_surface->getCanvas()->drawString(text, static_cast<float>(x * scale), static_cast<float>(y * scale - metrics.fAscent), font, paint);
    return 0;
}

extern "C" int aura_paint_opacity(double opacity) {
    SkPaint paint;
    if (!g_surface) {
        return -1;
    }
    if (g_opacity >= kMaxOpacity) {
        return -1;
    }
    if (opacity < 0.0) {
        opacity = 0.0;
    }
    if (opacity > 1.0) {
        opacity = 1.0;
    }
    paint.setAlphaf(static_cast<float>(opacity));
    g_surface->getCanvas()->saveLayer(nullptr, &paint);
    g_opacity += 1;
    return 0;
}

extern "C" int aura_paint_opacity_pop(void) {
    if (!g_surface || g_opacity <= 0) {
        return -1;
    }
    g_surface->getCanvas()->restore();
    g_opacity -= 1;
    return 0;
}

extern "C" int aura_paint_image(int64_t image, double x, double y, double w, double h) {
    Picture *picture;
    SkSamplingOptions sampling(SkFilterMode::kLinear, SkMipmapMode::kNone);
    double scale;
    if (!g_surface) {
        return -1;
    }
    picture = picture_get(image);
    if (picture == nullptr) {
        return -1;
    }
    scale = logical_scale();
    g_surface->getCanvas()->drawImageRect(picture->image, SkRect::MakeXYWH(static_cast<float>(x * scale), static_cast<float>(y * scale), static_cast<float>(w * scale), static_cast<float>(h * scale)), sampling, nullptr);
    return 0;
}

extern "C" int aura_paint_end(void) {
    while (g_opacity > 0) {
        if (g_surface) {
            g_surface->getCanvas()->restore();
        }
        g_opacity -= 1;
    }
    return g_surface ? 0 : -1;
}

extern "C" const uint8_t *aura_paint_pixels(void) {
    SkPixmap pixmap;
    if (!g_surface || !g_surface->peekPixels(&pixmap)) {
        return nullptr;
    }
    return static_cast<const uint8_t *>(pixmap.addr());
}

extern "C" int64_t aura_paint_pixel_width(void) {
    if (!g_surface) {
        return 0;
    }
    return g_surface->width();
}

extern "C" int64_t aura_paint_pixel_height(void) {
    if (!g_surface) {
        return 0;
    }
    return g_surface->height();
}

extern "C" int64_t aura_paint_stride(void) {
    SkPixmap pixmap;
    if (!g_surface || !g_surface->peekPixels(&pixmap)) {
        return 0;
    }
    return static_cast<int64_t>(pixmap.rowBytes());
}

extern "C" double aura_text_width(const char *text, double size, int64_t weight, const char *family) {
    SkFont font;
    (void)family;
    if (text == nullptr) {
        text = "";
    }
    ensure_font();
    if (!g_regular) {
        return 0.0;
    }
    font = make_font(size, weight);
    font.setSize(static_cast<float>(size));
    return static_cast<double>(font.measureText(text, std::strlen(text), SkTextEncoding::kUTF8));
}

extern "C" double aura_text_height(double size, int64_t weight, const char *family) {
    SkFont font;
    SkFontMetrics metrics;
    float height;
    (void)family;
    ensure_font();
    if (!g_regular) {
        return size;
    }
    font = make_font(size, weight);
    font.setSize(static_cast<float>(size));
    font.getMetrics(&metrics);
    height = metrics.fDescent - metrics.fAscent + metrics.fLeading;
    if (height < 1.0f) {
        return size;
    }
    return static_cast<double>(height);
}

extern "C" int64_t aura_image_open_file(const char *path) {
    sk_sp<SkData> data;
    if (path == nullptr) {
        return 0;
    }
    data = SkData::MakeFromFileName(path);
    return store_codec(decode_data(data));
}

extern "C" int64_t aura_image_buffer_reset(void) {
    g_bytes.clear();
    return 0;
}

extern "C" int64_t aura_image_buffer_byte(int64_t value) {
    g_bytes.push_back(static_cast<uint8_t>(value));
    return 0;
}

extern "C" int64_t aura_image_buffer_commit(void) {
    sk_sp<SkData> data;
    if (g_bytes.empty()) {
        return 0;
    }
    data = SkData::MakeWithCopy(g_bytes.data(), g_bytes.size());
    return store_codec(decode_data(data));
}

extern "C" int64_t aura_image_width(int64_t handle) {
    Picture *picture = picture_get(handle);
    if (picture == nullptr) {
        return -1;
    }
    return picture->width;
}

extern "C" int64_t aura_image_height(int64_t handle) {
    Picture *picture = picture_get(handle);
    if (picture == nullptr) {
        return -1;
    }
    return picture->height;
}

extern "C" int64_t aura_image_dispose(int64_t handle) {
    if (handle <= 0 || handle > kMaxImages) {
        return -1;
    }
    if (!g_pictures[handle - 1].alive) {
        return 0;
    }
    g_pictures[handle - 1].alive = 0;
    g_pictures[handle - 1].image.reset();
    g_pictures[handle - 1].width = 0;
    g_pictures[handle - 1].height = 0;
    return 0;
}

extern "C" double aura_mono_time(void) {
    static const auto origin = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - origin).count();
}
