#pragma once

#include "nativehttptransport.h"

// Transitional NvHTTP owner adapter only. The native transport contains no Qt.
// Remove this adapter when the worker no longer uses Qt's event dispatcher.
namespace NativeHttp {
Response performWithQtEvents(const Request& request);
}
