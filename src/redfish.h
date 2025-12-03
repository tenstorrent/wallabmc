#ifdef CONFIG_REDFISH
int redfish_init(void);
#else
static inline int redfish_init(void) { return 0; }
#endif
