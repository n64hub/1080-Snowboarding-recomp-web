// Force-included in MinGW builds. ultramodern's ultra64.h redefines NULL, which
// breaks MinGW's inline secure CRT functions if they're parsed afterwards, so
// parse those headers first.
#ifdef __cplusplus
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <clocale>
#include <thread>
#include <mutex>
#include <string>
#else
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#endif
