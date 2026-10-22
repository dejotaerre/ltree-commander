#pragma once
#include "platform/platform.h"
#include "fs/scanner.h"
#include <memory>

namespace ltree {
enum class PreviewKind { None, Image, Pdf, Office, Video, Font };
struct FontPreviewData;
PreviewKind previewKind(const String &path);
struct PreviewSource {
    PreviewKind kind = PreviewKind::None;
    String path, pdf, error;
    int pages = 1;
    int initialPage = 1;
    int64 durationMs = 0;
    String details;
    Bytes thumbnailRgb;
    int thumbnailWidth=0, thumbnailHeight=0;
    bool videoProbed=false;
    std::shared_ptr<FontPreviewData> font;
    std::shared_ptr<TemporaryDirectory> temporary;
};
struct PreviewRaster {
    Bytes rgb;
    int width = 0, height = 0, page = 1;
    String error;
    bool cancelled = false;
    std::shared_ptr<PreviewSource> source;
};
std::shared_ptr<PreviewSource> preparePreview(const String &path, const Cancellation &cancel);
PreviewRaster renderPreview(const std::shared_ptr<PreviewSource> &source, int page,
                            int width, int height, const Cancellation &cancel);
String previewPosition(const PreviewSource &source,int page);
}
