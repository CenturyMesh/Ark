// Emit the SDK's MSAA annotation GUIDs once. Some Windows SDK import libraries
// declare these identifiers in oleacc.h without providing their definitions.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <oleacc.h>
