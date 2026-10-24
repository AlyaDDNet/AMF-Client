#ifndef ENGINE_SHARED_HTTP_H
#define ENGINE_SHARED_HTTP_H

// Keep the AMF 19.9 request type name available to existing components while
// routing all requests through DDNet 20.1's native IHttp/IHttpRequest layer.
#include <engine/http.h>

using CHttpRequest = IHttpRequest;

#endif
