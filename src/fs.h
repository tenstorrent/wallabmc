#ifdef CONFIG_PERSISTENT_STORAGE
int fs_init(void);
int fs_exit(void);
#else
static inline int fs_init(void) { return 0; }
static inline int fs_exit(void) { return 0; }
#endif
