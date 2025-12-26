#ifdef CONFIG_PERSISTENT_STORAGE
int fs_init(void);
int fs_exit(void);
bool fs_enabled(void);
#else
static inline int fs_init(void) { return 0; }
static inline int fs_exit(void) { return 0; }
static inline bool fs_enabled(void) { return false; }
#endif
