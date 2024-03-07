#ifndef sd11_util_h
#define sd11_util_h

#include <3ds.h>

#define ERR_RET(msg) { printf("\n" CONSOLE_RED "%s: %08lX" CONSOLE_RESET "\n", msg, res); return res; }
#define ERR_EXIT(msg,__lbl) { printf("\n" CONSOLE_RED "%s: %08lX" CONSOLE_RESET "\n", msg, res); goto __lbl; }
#define TR(x,m) if(R_FAILED(res=(x)))ERR_RET((m));
#define TRE(x,m,l) if(R_FAILED(res=(x)))ERR_EXIT(m,l);
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define PERCENTAGE(part,whole) ((float)part / whole * 100.0f)

#endif
