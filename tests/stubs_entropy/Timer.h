/* host stub: the Time Manager's Microseconds() (see test_mac_entropy.c) */
#pragma once
typedef struct { unsigned long hi, lo; } UnsignedWide;
void Microseconds(UnsignedWide *w);
