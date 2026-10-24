/* micro-ecc with the Comba multiply and square in 68020 assembly but its
   own C reduction: the Comba step without Solinas, so the chart can show
   the two apart (renamed vCmc_*). Flags: CMakeLists.txt */
#define UECC_PFX(s) vCmc_##s
#include "uecc_variant.inc"
UECC_VARIANT(kUeccCombaC, "Comba asm, C reduction")
