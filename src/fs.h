#ifdef CONFIG_PERSISTENT_STORAGE
int fs_init(void);
#else
static inline int fs_init(void) { return 0; }
#endif
