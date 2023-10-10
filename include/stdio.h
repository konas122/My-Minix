/* the assert macro */
#define ASSERT
#ifdef ASSERT
void assertion_failure(char *exp, char *file, char *base_file, int line);
#define assert(exp)  if (exp) ; \
        else assertion_failure(#exp, __FILE__, __BASE_FILE__, __LINE__)
#else
#define assert(exp)
#endif

/* EXTERN */
#define	EXTERN	extern	/* EXTERN is defined as extern except in global.c */

/* string */
#define	STR_DEFAULT_LEN	1024

#define	O_CREAT		1
#define	O_RDWR		2

#define SEEK_SET	1
#define SEEK_CUR	2
#define SEEK_END	3

#define	MAX_PATH	128

/*--------*/
/* 库函数 */
/*--------*/

#ifdef ENABLE_DISK_LOG
#define SYSLOG syslog
#endif

/* lib/open.c */
int	open		(const char *pathname, int flags);

/* lib/close.c */
int	close		(int fd);

/* lib/read.c */
int	read		(int fd, void *buf, int count);

/* lib/write.c */
int	write		(int fd, const void *buf, int count);

/* lib/getpid.c */
int	getpid		();

/* lib/syslog.c */
int	syslog		(const char *fmt, ...);

/* lib/unlink.c */
int	unlink		(const char *pathname);