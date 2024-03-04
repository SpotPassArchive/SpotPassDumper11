#ifndef sd11_util_h
#define sd11_util_h

#define ERR_RET(msg) { printf("\n\x1b[31m%s: %08lX\x1b[0m\n", msg, res); return res; }
#define ERR_EXIT(msg,__lbl) { printf("\n\x1b[31m%s: %08lX\x1b[0m\n", msg, res); goto __lbl; }
#define TR(x,m) if(R_FAILED(res=(x)))ERR_RET((m));
#define TRE(x,m,l) if(R_FAILED(res=(x)))ERR_EXIT(m,l);
#define MIN(x,y) ((x) < (y) ? (x) : (y))

#endif
