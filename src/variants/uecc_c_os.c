/* micro-ecc in plain C at -Os: Notion Classic's flags from May 15 to June 5,
   2026, when the project-wide -Ofast became -Os (before that the curve code
   was built -Ofast, since June 5 -O3). Flags: CMakeLists.txt */
#define UECC_PFX(s) vCOs_##s
#include "uecc_variant.inc"
UECC_VARIANT(kUeccCOs, "C, -Os")
