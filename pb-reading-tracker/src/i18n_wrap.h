#pragma once

// This header is force-included only for main.cpp from CMake.
// It keeps the first localization pass small and reversible: existing UI
// drawing calls are translated at the edge, while database/tracker logic stays
// untouched.
#include <inkview.h>
#include "i18n.h"

#define StringWidth(text) StringWidth(tr(text))
#define DrawString(x, y, text) DrawString((x), (y), tr(text))
#define Message(icon, title, message, timeout) Message((icon), tr(title), tr(message), (timeout))
