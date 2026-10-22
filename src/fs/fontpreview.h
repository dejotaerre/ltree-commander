#pragma once
#include "fs/preview.h"
namespace ltree {
void prepareFontPreview(PreviewSource &source,const Cancellation &cancel);
PreviewRaster renderFontPreview(const std::shared_ptr<PreviewSource> &source,int page,const Cancellation &cancel);
}
