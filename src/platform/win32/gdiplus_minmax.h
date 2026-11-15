// Force-included in the Win32 target: GDI+ headers use unqualified min/max,
// which NOMINMAX removes. Provide them inside the Gdiplus namespace instead.
#pragma once
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
