#ifdef CONFIG_PERSISTENT_STORAGE
int button_init(void);
#else
static inline int button_init(void) { return 0; }
#endif
