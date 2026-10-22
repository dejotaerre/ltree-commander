#include "core/buildinfo.h"
#include "build_identity.h"

namespace ltree {
String buildId() { return LTREE_BUILD_ID; }
String versionDescription() { return String(LTREE_VERSION) + " (build " + buildId() + ")"; }
}
