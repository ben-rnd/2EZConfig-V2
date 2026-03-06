#pragma once

// Installs a vtable hook on IDirectDraw2/7 SetDisplayMode.
// If force60hz is true, the hook overrides dwRefreshRate to 60 on every call.
void installDDrawHook(bool force60hz);
