#pragma once
#include "fs/preview.h"
namespace ltree {
bool desktopThumbnail(PreviewSource &source,const Cancellation &cancel);
std::shared_ptr<PreviewSource> refreshVideoThumbnail(const std::shared_ptr<PreviewSource> &source,const Cancellation &cancel);
}
