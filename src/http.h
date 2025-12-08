#ifdef CONFIG_HTTP_SERVER
int app_http_server_init(void);
#else
static inline int app_http_server_init(void) { return 0; }
#endif
