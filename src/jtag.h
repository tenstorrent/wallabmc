#ifdef CONFIG_JTAG
int jtag_init(void);
#else
static inline int jtag_init(void) { return 0; }
#endif
