/* X25519 as Claude 68 runs it today: 8 x 32-bit words on the Comba
   assembly of the Comba micro-ecc build (curve25519_sat.c). */
#define uECC_vli_mult    vCmb_uECC_vli_mult
#define uECC_vli_square  vCmb_uECC_vli_square
#define curve25519_donna x25519_sat
#include "curve25519_sat.c"
