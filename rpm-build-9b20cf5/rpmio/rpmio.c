/*@-type@*/ /* LCL: function typedefs */
/** \ingroup rpmio
 * \file rpmio/rpmio.c
 */

#include "system.h"
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <sys/personality.h>

#if HAVE_MACHINE_TYPES_H
# include <machine/types.h>
#endif

#include <netinet/in.h>
#include <arpa/inet.h>		/* XXX for inet_aton and HP-UX */

#if HAVE_NETINET_IN_SYSTM_H
# include <sys/types.h>

#if defined(__LCLINT__)
/*@-redef@*/ /* FIX: rpmdb/db3.c also declares */
typedef unsigned int u_int32_t;
typedef unsigned short u_int16_t;
typedef unsigned char u_int8_t;
/*@-incondefs@*/        /* LCLint 3.0.0.15 */
typedef int int32_t;
/*@=incondefs@*/
/*@=redef@*/
#endif

# include <netinet/in_systm.h>
#endif

#if !defined(HAVE_HERRNO) && defined(__hpux) /* XXX HP-UX w/o -D_XOPEN_SOURCE needs */
/*@unchecked@*/
extern int h_errno;
#endif

#ifndef IPPORT_FTP
#define IPPORT_FTP	21
#endif
#ifndef	IPPORT_HTTP
#define	IPPORT_HTTP	80
#endif

#if !defined(HAVE_INET_ATON)
static int inet_aton(const char *cp, struct in_addr *inp)
	/*@modifies *inp @*/
{
    long addr;

    addr = inet_addr(cp);
    if (addr == ((long) -1)) return 0;

    memcpy(inp, &addr, sizeof(addr));
    return 1;
}
#endif

#if defined(USE_ALT_DNS) && USE_ALT_DNS
#include "dns.h"
#endif

#include "rpmio_internal.h"
#undef	fdFileno
#undef	fdOpen
#undef	fdRead
#undef	fdWrite
#undef	fdClose

#include "system.h"

#include "ugid.h"
#include "rpmmessages.h"
#include "rpmmacro.h"

#include "debug.h"

/*@access urlinfo @*/
/*@access FDSTAT_t @*/

#define FDNREFS(fd)	(fd ? ((FD_t)fd)->nrefs : -9)
#define FDTO(fd)	(fd ? ((FD_t)fd)->rd_timeoutsecs : -99)
#define FDCPIOPOS(fd)	(fd ? ((FD_t)fd)->fd_cpioPos : -99)

#define	FDONLY(fd)	assert(fdGetIo(fd) == fdio)
#define	GZDONLY(fd)	assert(fdGetIo(fd) == gzdio)
#define	BZDONLY(fd)	assert(fdGetIo(fd) == bzdio)
#define	LZDONLY(fd)	assert(fdGetIo(fd) == xzdio || fdGetIo(fd) == lzdio)

#define	UFDONLY(fd)	/* assert(fdGetIo(fd) == ufdio) */

#define	fdGetFILE(_fd)	((FILE *)fdGetFp(_fd))

unsigned int rpmioThreads = 0; /* For build/pack.c */
static char *lzinfo = NULL; /* Extended lzma info for error messages. */

/**
 */
/*@unchecked@*/
#ifdef HAVE_COOKIE_IO_FUNCTIONS_T
int noLibio = 0;
#else
int noLibio = 1;
#endif

#define TIMEOUT_SECS 60

/**
 */
/*@unchecked@*/
static int ftpTimeoutSecs = TIMEOUT_SECS;

/**
 */
/*@unchecked@*/
static int httpTimeoutSecs = TIMEOUT_SECS;

/**
 */
/*@unchecked@*/
int _ftp_debug = 0;

/**
 */
/*@unchecked@*/
int _rpmio_debug = 0;

/**
 * Wrapper to free(3), hides const compilation noise, permit NULL, return NULL.
 * @param p		memory to free
 * @retval		NULL always
 */
/*@unused@*/ static inline /*@null@*/ void *
_free(/*@only@*/ /*@null@*/ /*@out@*/ const void * p)
	/*@modifies p@*/
{
    if (p != NULL)	free((void *)p);
    return NULL;
}

/* =============================================================== */

/*@-modfilesys@*/
static /*@observer@*/ const char * fdbg(/*@null@*/ FD_t fd)
	/*@*/
{
    static char buf[BUFSIZ];
    char *be = buf;
    int i;

    buf[0] = '\0';
    if (fd == NULL)
	return buf;

#if DYING
    sprintf(be, "fd %p", fd);	be += strlen(be);
    if (fd->rd_timeoutsecs >= 0) {
	sprintf(be, " secs %d", fd->rd_timeoutsecs);
	be += strlen(be);
    }
#endif
    if (fd->bytesRemain != -1) {
	sprintf(be, " clen %d", (int)fd->bytesRemain);
	be += strlen(be);
     }
    if (fd->wr_chunked) {
	strcpy(be, " chunked");
	be += strlen(be);
     }
    *be++ = '\t';
    for (i = fd->nfps; i >= 0; i--) {
	FDSTACK_t * fps = &fd->fps[i];
	if (i != fd->nfps)
	    *be++ = ' ';
	*be++ = '|';
	*be++ = ' ';
	if (fps->io == fdio) {
	    sprintf(be, "FD %d fp %p", fps->fdno, fps->fp);
	} else if (fps->io == ufdio) {
	    sprintf(be, "UFD %d fp %p", fps->fdno, fps->fp);
	} else if (fps->io == fadio) {
	    sprintf(be, "FAD %d fp %p", fps->fdno, fps->fp);
	} else if (fps->io == gzdio) {
	    sprintf(be, "GZD %p fdno %d", fps->fp, fps->fdno);
#ifdef HAVE_BZLIB_H
	} else if (fps->io == bzdio) {
	    sprintf(be, "BZD %p fdno %d", fps->fp, fps->fdno);
#endif
	} else if (fps->io == lzdio) {
	    sprintf(be, "LZD %p fdno %d", fps->fp, fps->fdno);
	} else if (fps->io == xzdio) {
	    sprintf(be, "XZD %p fdno %d", fps->fp, fps->fdno);
	} else if (fps->io == fpio) {
	    /*@+voidabstract@*/
	    sprintf(be, "%s %p(%d) fdno %d",
		(fps->fdno < 0 ? "LIBIO" : "FP"),
		fps->fp, fileno(((FILE *)fps->fp)), fps->fdno);
	    /*@=voidabstract@*/
	} else {
	    sprintf(be, "??? io %p fp %p fdno %d ???",
		fps->io, fps->fp, fps->fdno);
	}
	be += strlen(be);
	*be = '\0';
    }
    return buf;
}
/*@=modfilesys@*/

/* =============================================================== */
off_t fdSize(FD_t fd)
{
    struct stat sb;
    off_t rc = -1; 

#ifdef	NOISY
DBGIO(0, (stderr, "==>\tfdSize(%p) rc %ld\n", fd, (long)rc));
#endif
    FDSANE(fd);
    if (fd->contentLength >= 0)
	rc = fd->contentLength;
    else switch (fd->urlType) {
    case URL_IS_PATH:
    case URL_IS_UNKNOWN:
	if (fstat(Fileno(fd), &sb) == 0)
	    rc = sb.st_size;
	/*@fallthrough@*/
    case URL_IS_FTP:
    case URL_IS_HTTP:
    case URL_IS_DASH:
	break;
    }
    return rc;
}

FD_t fdDup(int fdno)
{
    FD_t fd;
    int nfdno;

    if ((nfdno = dup(fdno)) < 0)
	return NULL;
    fd = fdNew("open (fdDup)");
    fdSetFdno(fd, nfdno);
/*@-modfilesys@*/
DBGIO(fd, (stderr, "==> fdDup(%d) fd %p %s\n", fdno, (fd ? fd : NULL), fdbg(fd)));
/*@=modfilesys@*/
    /*@-refcounttrans@*/ return fd; /*@=refcounttrans@*/
}

static inline /*@unused@*/ int fdSeekNot(void * cookie,
		/*@unused@*/ _libio_pos_t pos,  /*@unused@*/ int whence)
	/*@*/
{
    FD_t fd = c2f(cookie);
    FDSANE(fd);		/* XXX keep gcc quiet */
    return -2;
}

#ifdef UNUSED
FILE *fdFdopen(void * cookie, const char *fmode)
{
    FD_t fd = c2f(cookie);
    int fdno;
    FILE * fp;

    if (fmode == NULL) return NULL;
    fdno = fdFileno(fd);
    if (fdno < 0) return NULL;
    fp = fdopen(fdno, fmode);
/*@-modfilesys@*/
DBGIO(fd, (stderr, "==> fdFdopen(%p,\"%s\") fdno %d -> fp %p fdno %d\n", cookie, fmode, fdno, fp, fileno(fp)));
/*@=modfilesys@*/
    fd = fdFree(fd, "open (fdFdopen)");
    return fp;
}
#endif

/* =============================================================== */
/*@-modfilesys@*/
/*@-mustmod@*/ /* FIX: cookie is modified */
static inline /*@null@*/ FD_t XfdLink(void * cookie, const char * msg,
		const char * file, unsigned line)
	/*@modifies *cookie @*/
{
    FD_t fd;
if (cookie == NULL)
    /*@-castexpose@*/
DBGREFS(0, (stderr, "--> fd  %p ++ %d %s at %s:%u\n", cookie, FDNREFS(cookie)+1, msg, file, line));
    /*@=castexpose@*/
    fd = c2f(cookie);
    if (fd) {
	fd->nrefs++;
DBGREFS(fd, (stderr, "--> fd  %p ++ %d %s at %s:%u %s\n", fd, fd->nrefs, msg, file, line, fdbg(fd)));
    }
    return fd;
}
/*@=mustmod@*/
/*@=modfilesys@*/

/*@-modfilesys@*/
static inline /*@null@*/ FD_t XfdFree( /*@killref@*/ FD_t fd, const char *msg,
		const char *file, unsigned line)
	/*@modifies fd @*/
{
	int i;

if (fd == NULL)
DBGREFS(0, (stderr, "--> fd  %p -- %d %s at %s:%u\n", fd, FDNREFS(fd), msg, file, line));
    FDSANE(fd);
    if (fd) {
DBGREFS(fd, (stderr, "--> fd  %p -- %d %s at %s:%u %s\n", fd, fd->nrefs, msg, file, line, fdbg(fd)));
	if (--fd->nrefs > 0)
	    /*@-refcounttrans -retalias@*/ return fd; /*@=refcounttrans =retalias@*/
	fd->stats = _free(fd->stats);
	for (i = fd->ndigests - 1; i >= 0; i--) {
	    FDDIGEST_t fddig = fd->digests + i;
	    if (fddig->hashctx == NULL)
		continue;
	    (void) rpmDigestFinal(fddig->hashctx, NULL, NULL, 0);
	    fddig->hashctx = NULL;
	}
	fd->ndigests = 0;
	/*@-refcounttrans@*/ free(fd); /*@=refcounttrans@*/
    }
    return NULL;
}
/*@=modfilesys@*/

static inline /*@null@*/ FD_t XfdNew(const char * msg,
		const char * file, unsigned line)
	/*@*/
{
    FD_t fd = xcalloc(1, sizeof(*fd));
    if (fd == NULL) /* XXX xmalloc never returns NULL */
	return NULL;
    fd->nrefs = 0;
    fd->flags = 0;
    fd->magic = FDMAGIC;
    fd->urlType = URL_IS_UNKNOWN;

    fd->nfps = 0;
    memset(fd->fps, 0, sizeof(fd->fps));

    /*@-assignexpose@*/
    fd->fps[0].io = fdio;
    /*@=assignexpose@*/
    fd->fps[0].fp = NULL;
    fd->fps[0].fdno = -1;

    fd->url = NULL;
    fd->rd_timeoutsecs = 1;	/* XXX default value used to be -1 */
    fd->contentLength = fd->bytesRemain = -1;
    fd->wr_chunked = 0;
    fd->syserrno = 0;
    fd->errcookie = NULL;
    fd->stats = xcalloc(1, sizeof(*fd->stats));

    fd->ndigests = 0;
    memset(fd->digests, 0, sizeof(fd->digests));

    (void) gettimeofday(&fd->stats->create, NULL);
    fd->stats->begin = fd->stats->create;	/* structure assignment */

    fd->ftpFileDoneNeeded = 0;
    fd->firstFree = 0;
    fd->fileSize = 0;
    fd->fd_cpioPos = 0;

    return XfdLink(fd, msg, file, line);
}

/*@-redef@*/	/* FIX: legacy API should be made static */
ssize_t fdRead(void * cookie, /*@out@*/ char * buf, size_t count)
/*@=redef@*/
{
    FD_t fd = c2f(cookie);
    ssize_t rc;

    if (fd->bytesRemain == 0) return 0;	/* XXX simulate EOF */

    fdstat_enter(fd, FDSTAT_READ);
    rc = read(fdFileno(fd), buf, (count > fd->bytesRemain ? fd->bytesRemain : count));
    fdstat_exit(fd, FDSTAT_READ, rc);

    if (fd->ndigests && rc > 0)
	fdUpdateDigests(fd, (const unsigned char *)buf, rc);

/*@-modfilesys@*/
DBGIO(fd, (stderr, "==>\tfdRead(%p,%p,%ld) rc %ld %s\n", cookie, buf, (long)count, (long)rc, fdbg(fd)));
/*@=modfilesys@*/

    return rc;
}

/*@-redef@*/	/* FIX: legacy API should be made static */
ssize_t fdWrite(void * cookie, const char * buf, size_t count)
/*@=redef@*/
{
    FD_t fd = c2f(cookie);
    int fdno = fdFileno(fd);
    ssize_t rc;

    if (fd->bytesRemain == 0) return 0;	/* XXX simulate EOF */

    if (fd->ndigests && count > 0)
	fdUpdateDigests(fd, (const unsigned char *)buf, count);

    if (fd->wr_chunked) {
	char chunksize[20];
	sprintf(chunksize, "%x\r\n", (unsigned)count);
	rc = write(fdno, chunksize, strlen(chunksize));
	if (rc == -1)	fd->syserrno = errno;
    }
    if (count == 0) return 0;

    fdstat_enter(fd, FDSTAT_WRITE);
    rc = write(fdno, buf, (count > fd->bytesRemain ? fd->bytesRemain : count));
    fdstat_exit(fd, FDSTAT_WRITE, rc);

    if (fd->wr_chunked) {
	int ec;
	ec = write(fdno, "\r\n", sizeof("\r\n")-1);
	if (ec == -1)	fd->syserrno = errno;
    }

/*@-modfilesys@*/
DBGIO(fd, (stderr, "==>\tfdWrite(%p,%p,%ld) rc %ld %s\n", cookie, buf, (long)count, (long)rc, fdbg(fd)));
/*@=modfilesys@*/

    return rc;
}

static inline int fdSeek(void * cookie, _libio_pos_t pos, int whence)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
#ifdef USE_COOKIE_SEEK_POINTER
    off64_t p = *pos;
#else
    off_t p = pos;
#endif
    FD_t fd = c2f(cookie);
    off_t rc;

    assert(fd->bytesRemain == -1);	/* XXX FIXME fadio only for now */
    fdstat_enter(fd, FDSTAT_SEEK);
    rc = lseek(fdFileno(fd), p, whence);
    fdstat_exit(fd, FDSTAT_SEEK, rc);

/*@-modfilesys@*/
DBGIO(fd, (stderr, "==>\tfdSeek(%p,%ld,%d) rc %lx %s\n", cookie, (long)p, whence, (unsigned long)rc, fdbg(fd)));
/*@=modfilesys@*/

    return rc;
}

/*@-redef@*/	/* FIX: legacy API should be made static */
int fdClose( /*@only@*/ void * cookie)
/*@=redef@*/
{
    FD_t fd;
    int fdno;
    int rc;

    if (cookie == NULL) return -2;
    fd = c2f(cookie);
    fdno = fdFileno(fd);

    fdSetFdno(fd, -1);

    fdstat_enter(fd, FDSTAT_CLOSE);
    rc = ((fdno >= 0) ? close(fdno) : -2);
    fdstat_exit(fd, FDSTAT_CLOSE, rc);

/*@-modfilesys@*/
DBGIO(fd, (stderr, "==>\tfdClose(%p) rc %lx %s\n", (fd ? fd : NULL), (unsigned long)rc, fdbg(fd)));
/*@=modfilesys@*/

    fd = fdFree(fd, "open (fdClose)");
    return rc;
}

/*@-redef@*/	/* FIX: legacy API should be made static */
/*@null@*/ FD_t fdOpen(const char *path, int flags, mode_t mode)
/*@=redef@*/
{
    FD_t fd;
    int fdno;

    fdno = open(path, flags, mode);
    if (fdno < 0) return NULL;
    if (fcntl(fdno, F_SETFD, FD_CLOEXEC)) {
	(void) close(fdno);
	return NULL;
    }
    fd = fdNew("open (fdOpen)");
    fdSetFdno(fd, fdno);
    fd->flags = flags;
/*@-modfilesys@*/
DBGIO(fd, (stderr, "==>\tfdOpen(\"%s\",%x,0%o) %s\n", path, (unsigned)flags, (unsigned)mode, fdbg(fd)));
/*@=modfilesys@*/
    /*@-refcounttrans@*/ return fd; /*@=refcounttrans@*/
}

static struct FDIO_s fdio_s = {
  fdRead, fdWrite, fdSeek, fdClose, XfdLink, XfdFree, XfdNew, fdFileno,
  fdOpen, NULL, fdGetFp, NULL,	mkdir, chdir, rmdir, rename, unlink
};
FDIO_t fdio = /*@-compmempass@*/ &fdio_s /*@=compmempass@*/ ;

/*@-redef@*/	/* see lib/falloc.c */
FDIO_t fadio;	/* XXX usually NULL, filled in when linked with rpm */
/*@=redef@*/

int fdWritable(FD_t fd, int secs)
{
    int fdno;
    fd_set wrfds;
    struct timeval timeout, *tvp = (secs >= 0 ? &timeout : NULL);
    int rc;
	
    if ((fdno = fdFileno(fd)) < 0)
	return -1;	/* XXX W2DO? */
	
    FD_ZERO(&wrfds);
    do {
	FD_SET(fdno, &wrfds);

	if (tvp) {
	    tvp->tv_sec = secs;
	    tvp->tv_usec = 0;
	}
	errno = 0;
	/*@-compdef -nullpass@*/
	rc = select(fdno + 1, NULL, &wrfds, NULL, tvp);
	/*@=compdef =nullpass@*/

if (_rpmio_debug && !(rc == 1 && errno == 0))
fprintf(stderr, "*** fdWritable fdno %d rc %d %s\n", fdno, rc, strerror(errno));
	if (rc < 0) {
	    switch (errno) {
	    case EINTR:
		continue;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    default:
		return rc;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    }
	}
	return rc;
    } while (1);
    /*@notreached@*/
}

int fdReadable(FD_t fd, int secs)
{
    int fdno;
    fd_set rdfds;
    struct timeval timeout, *tvp = (secs >= 0 ? &timeout : NULL);
    int rc;

    if ((fdno = fdFileno(fd)) < 0)
	return -1;	/* XXX W2DO? */
	
    FD_ZERO(&rdfds);
    do {
	FD_SET(fdno, &rdfds);

	if (tvp) {
	    tvp->tv_sec = secs;
	    tvp->tv_usec = 0;
	}
	errno = 0;
	/*@-compdef -nullpass@*/
	rc = select(fdno + 1, &rdfds, NULL, NULL, tvp);
	/*@=compdef =nullpass@*/

	if (rc < 0) {
	    switch (errno) {
	    case EINTR:
		continue;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    default:
		return rc;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    }
	}
	return rc;
    } while (1);
    /*@notreached@*/
}

int fdFgets(FD_t fd, char * buf, size_t len)
{
    int fdno;
    int secs = fd->rd_timeoutsecs;
    size_t nb = 0;
    int ec = 0;
    char lastchar = '\0';

    if ((fdno = fdFileno(fd)) < 0)
	return 0;	/* XXX W2DO? */
	
    do {
	int rc;

	/* Is there data to read? */
	rc = fdReadable(fd, secs);

	switch (rc) {
	case -1:	/* error */
	    ec = -1;
	    continue;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	case  0:	/* timeout */
	    ec = -1;
	    continue;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	default:	/* data to read */
	    /*@switchbreak@*/ break;
	}

	errno = 0;
#ifdef	NOISY
	rc = fdRead(fd, buf + nb, 1);
#else
	rc = read(fdFileno(fd), buf + nb, 1);
#endif
	if (rc < 0) {
	    fd->syserrno = errno;
	    switch (errno) {
	    case EWOULDBLOCK:
		continue;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    default:
		/*@switchbreak@*/ break;
	    }
if (_rpmio_debug)
fprintf(stderr, "*** read: fd %p rc %d errno %d %s \"%s\"\n", fd, rc, errno, strerror(errno), buf);
	    ec = -1;
	    break;
	} else if (rc == 0) {
if (_rpmio_debug)
fprintf(stderr, "*** read: fd %p rc %d EOF errno %d %s \"%s\"\n", fd, rc, errno, strerror(errno), buf);
	    break;
	} else {
	    nb += rc;
	    buf[nb] = '\0';
	    lastchar = buf[nb - 1];
	}
    } while (ec == 0 && nb < len && lastchar != '\n');

    return (ec >= 0 ? nb : ec);
}

/* =============================================================== */
/* Support for FTP/HTTP I/O.
 */
const char * ftpStrerror(int errorNumber) {
  switch (errorNumber) {
    case 0:
	return _("Success");

    case FTPERR_BAD_SERVER_RESPONSE:
	return _("Bad server response");

    case FTPERR_SERVER_IO_ERROR:
	return _("Server I/O error");

    case FTPERR_SERVER_TIMEOUT:
	return _("Server timeout");

    case FTPERR_BAD_HOST_ADDR:
	return _("Unable to lookup server host address");

    case FTPERR_BAD_HOSTNAME:
	return _("Unable to lookup server host name");

    case FTPERR_FAILED_CONNECT:
	return _("Failed to connect to server");

    case FTPERR_FAILED_DATA_CONNECT:
	return _("Failed to establish data connection to server");

    case FTPERR_FILE_IO_ERROR:
	return _("I/O error to local file");

    case FTPERR_PASSIVE_ERROR:
	return _("Error setting remote server to passive mode");

    case FTPERR_FILE_NOT_FOUND:
	return _("File not found on server");

    case FTPERR_NIC_ABORT_IN_PROGRESS:
	return _("Abort in progress");

    case FTPERR_UNKNOWN:
    default:
	return _("Unknown or unexpected error");
  }
}

const char *urlStrerror(const char *url)
{
    const char *retstr;
    /*@-branchstate@*/
    switch (urlIsURL(url)) {
    case URL_IS_FTP:
    case URL_IS_HTTP:
    {	urlinfo u;
/* XXX This only works for httpReq/ftpLogin/ftpReq failures */
	if (urlSplit(url, &u) == 0) {
	    retstr = ftpStrerror(u->openError);
	} else
	    retstr = "Malformed URL";
    }	break;
    default:
	retstr = strerror(errno);
	break;
    }
    /*@=branchstate@*/
    return retstr;
}

#if !defined(USE_ALT_DNS) || !USE_ALT_DNS 
static int mygethostbyname(const char * host,
		/*@out@*/ struct in_addr * address)
	/*@modifies *address @*/
{
    struct hostent * hostinfo;

    /*@-unrecog -multithreaded @*/
    /*@-globs@*/ /* FIX: h_errno access */
    hostinfo = gethostbyname(host);
    /*@=globs@*/
    /*@=unrecog =multithreaded @*/
    if (!hostinfo) return 1;

    /*@-nullderef@*/
    memcpy(address, hostinfo->h_addr_list[0], sizeof(*address));
    /*@=nullderef@*/
    return 0;
}
#endif

/*@-compdef@*/	/* FIX: address->s_addr undefined. */
static int getHostAddress(const char * host, /*@out@*/ struct in_addr * address)
	/*@globals errno @*/
	/*@modifies *address, errno @*/
{
    if (xisdigit(host[0])) {
	/*@-unrecog -moduncon @*/
	if (!inet_aton(host, address))
	    return FTPERR_BAD_HOST_ADDR;
	/*@=unrecog =moduncon @*/
    } else {
	/*@-globs@*/ /* FIX: h_errno access */
	if (mygethostbyname(host, address)) {
	    errno = /*@-unrecog@*/ h_errno /*@=unrecog@*/;
	    return FTPERR_BAD_HOSTNAME;
	}
	/*@=globs@*/
    }
    
    return 0;
}
/*@=compdef@*/

static int tcpConnect(FD_t ctrl, const char * host, int port)
	/*@globals fileSystem @*/
	/*@modifies ctrl, fileSystem @*/
{
    struct sockaddr_in sin;
    int fdno = -1;
    int rc;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = INADDR_ANY;
    
  do {
    if ((rc = getHostAddress(host, &sin.sin_addr)) < 0)
	break;

    if ((fdno = socket(sin.sin_family, SOCK_STREAM, IPPROTO_IP)) < 0) {
	rc = FTPERR_FAILED_CONNECT;
	break;
    }

    /*@-internalglobs@*/
    if (connect(fdno, (struct sockaddr *) &sin, sizeof(sin))) {
	rc = FTPERR_FAILED_CONNECT;
	break;
    }
    /*@=internalglobs@*/
  } while (0);

    if (rc < 0)
	goto errxit;

if (_ftp_debug)
fprintf(stderr,"++ connect %s:%d on fdno %d\n",
/*@-unrecog -moduncon -evalorderuncon @*/
inet_ntoa(sin.sin_addr)
/*@=unrecog =moduncon =evalorderuncon @*/ ,
(int)ntohs(sin.sin_port), fdno);

    fdSetFdno(ctrl, (fdno >= 0 ? fdno : -1));
    return 0;

errxit:
    /*@-observertrans@*/
    fdSetSyserrno(ctrl, errno, ftpStrerror(rc));
    /*@=observertrans@*/
    if (fdno >= 0)
	(void) close(fdno);
    return rc;
}

static int checkResponse(void * uu, FD_t ctrl,
		/*@out@*/ int *ecp, /*@out@*/ char ** str)
	/*@globals fileSystem @*/
	/*@modifies ctrl, *ecp, *str, fileSystem @*/
{
    urlinfo u = uu;
    char *buf;
    size_t bufAlloced;
    int bufLength = 0; 
    const char *s;
    char *se;
    int ec = 0;
    int moretodo = 1;
    char errorCode[4];
 
    URLSANE(u);
    if (u->bufAlloced == 0 || u->buf == NULL) {
	u->bufAlloced = _url_iobuf_size;
	u->buf = xcalloc(u->bufAlloced, sizeof(u->buf[0]));
    }
    buf = u->buf;
    bufAlloced = u->bufAlloced;
    *buf = '\0';

    errorCode[0] = '\0';
    
    do {
	int rc;

	/*
	 * Read next line from server.
	 */
	se = buf + bufLength;
	*se = '\0';
	rc = fdFgets(ctrl, se, (bufAlloced - bufLength));
	if (rc < 0) {
	    ec = FTPERR_BAD_SERVER_RESPONSE;
	    continue;
	} else if (rc == 0 || fdWritable(ctrl, 0) < 1)
	    moretodo = 0;

	/*
	 * Process next line from server.
	 */
	for (s = se; *s != '\0'; s = se) {
		const char *e;

		while (*se && *se != '\n') se++;

		if (se > s && se[-1] == '\r')
		   se[-1] = '\0';
		if (*se == '\0')
		    /*@innerbreak@*/ break;

if (_ftp_debug)
fprintf(stderr, "<- %s\n", s);

		/* HTTP: header termination on empty line */
		if (*s == '\0') {
		    moretodo = 0;
		    /*@innerbreak@*/ break;
		}
		*se++ = '\0';

		/* HTTP: look for "HTTP/1.1 123 ..." */
		if (!strncmp(s, "HTTP", sizeof("HTTP")-1)) {
		    ctrl->contentLength = -1;
		    if ((e = strchr(s, '.')) != NULL) {
			e++;
			u->httpVersion = *e - '0';
			if (u->httpVersion < 1 || u->httpVersion > 2)
			    ctrl->persist = u->httpVersion = 0;
			else
			    ctrl->persist = 1;
		    }
		    if ((e = strchr(s, ' ')) != NULL) {
			e++;
			if (strchr("0123456789", *e))
			    strncpy(errorCode, e, 3);
			errorCode[3] = '\0';
		    }
		    /*@innercontinue@*/ continue;
		}

		/* HTTP: look for "token: ..." */
		for (e = s; *e && !(*e == ' ' || *e == ':'); e++)
		    {};
		if (e > s && *e++ == ':') {
		    size_t ne = (e - s);
		    while (*e && *e == ' ') e++;
#if 0
		    if (!strncmp(s, "Date:", ne)) {
		    } else
		    if (!strncmp(s, "Server:", ne)) {
		    } else
		    if (!strncmp(s, "Last-Modified:", ne)) {
		    } else
		    if (!strncmp(s, "ETag:", ne)) {
		    } else
#endif
		    if (!strncmp(s, "Accept-Ranges:", ne)) {
			if (!strcmp(e, "bytes"))
			    u->httpHasRange = 1;
			if (!strcmp(e, "none"))
			    u->httpHasRange = 0;
		    } else
		    if (!strncmp(s, "Content-Length:", ne)) {
			if (strchr("0123456789", *e))
			    ctrl->contentLength = atoi(e);
		    } else
		    if (!strncmp(s, "Connection:", ne)) {
			if (!strcmp(e, "close"))
			    ctrl->persist = 0;
		    }
#if 0
		    else
		    if (!strncmp(s, "Content-Type:", ne)) {
		    } else
		    if (!strncmp(s, "Transfer-Encoding:", ne)) {
			if (!strcmp(e, "chunked"))
			    ctrl->wr_chunked = 1;
			else
			    ctrl->wr_chunked = 0;
		    } else
		    if (!strncmp(s, "Allow:", ne)) {
		    }
#endif
		    /*@innercontinue@*/ continue;
		}

		/* HTTP: look for "<TITLE>501 ... </TITLE>" */
		if (!strncmp(s, "<TITLE>", sizeof("<TITLE>")-1))
		    s += sizeof("<TITLE>") - 1;

		/* FTP: look for "123-" and/or "123 " */
		if (strchr("0123456789", *s)) {
		    if (errorCode[0] != '\0') {
			if (!strncmp(s, errorCode, sizeof("123")-1) && s[3] == ' ')
			    moretodo = 0;
		    } else {
			strncpy(errorCode, s, sizeof("123")-1);
			errorCode[3] = '\0';
			if (s[3] != '-')
			    moretodo = 0;
		    }
		}
	}

	if (moretodo && se > s) {
	    bufLength = se - s - 1;
	    if (s != buf)
		memmove(buf, s, bufLength);
	} else {
	    bufLength = 0;
	}
    } while (moretodo && ec == 0);

    if (str)	*str = buf;
    if (ecp)	*ecp = atoi(errorCode);

    return ec;
}

static int ftpCheckResponse(urlinfo u, /*@out@*/ char ** str)
	/*@globals fileSystem @*/
	/*@modifies u, *str, fileSystem @*/
{
    int ec = 0;
    int rc;

    URLSANE(u);
    rc = checkResponse(u, u->ctrl, &ec, str);

    switch (ec) {
    case 550:
	return FTPERR_FILE_NOT_FOUND;
	/*@notreached@*/ break;
    case 552:
	return FTPERR_NIC_ABORT_IN_PROGRESS;
	/*@notreached@*/ break;
    default:
	if (ec >= 400 && ec <= 599) {
	    return FTPERR_BAD_SERVER_RESPONSE;
	}
	break;
    }
    return rc;
}

static int ftpCommand(urlinfo u, char ** str, ...)
	/*@globals fileSystem @*/
	/*@modifies u, *str, fileSystem @*/
{
    va_list ap;
    int len = 0;
    const char * s, * t;
    char * te;
    int rc;

    URLSANE(u);
    va_start(ap, str);
    while ((s = va_arg(ap, const char *)) != NULL) {
	if (len) len++;
	len += strlen(s);
    }
    len += sizeof("\r\n")-1;
    va_end(ap);

    t = te = alloca(len + 1);

    va_start(ap, str);
    while ((s = va_arg(ap, const char *)) != NULL) {
	if (te > t) *te++ = ' ';
	te = stpcpy(te, s);
    }
    te = stpcpy(te, "\r\n");
    va_end(ap);

if (_ftp_debug)
fprintf(stderr, "-> %s", t);
    if (fdWrite(u->ctrl, t, (te-t)) != (te-t))
	return FTPERR_SERVER_IO_ERROR;

    rc = ftpCheckResponse(u, str);
    return rc;
}

static int ftpLogin(urlinfo u)
	/*@globals fileSystem @*/
	/*@modifies u, fileSystem @*/
{
    const char * host;
    const char * user;
    const char * password;
    int port;
    int rc;

    URLSANE(u);
    u->ctrl = fdLink(u->ctrl, "open ctrl");

    if (((host = (u->proxyh ? u->proxyh : u->host)) == NULL)) {
	rc = FTPERR_BAD_HOSTNAME;
	goto errxit;
    }

    if ((port = (u->proxyp > 0 ? u->proxyp : u->port)) < 0) port = IPPORT_FTP;

    /*@-branchstate@*/
    if ((user = (u->proxyu ? u->proxyu : u->user)) == NULL)
	user = "anonymous";
    /*@=branchstate@*/

    /*@-branchstate@*/
    if ((password = u->password) == NULL) {
 	uid_t uid = getuid();
	struct passwd * pw;
	if (uid && (pw = getpwuid(uid)) != NULL) {
	    char *myp = alloca(strlen(pw->pw_name) + sizeof("@"));
	    strcpy(myp, pw->pw_name);
	    strcat(myp, "@");
	    password = myp;
	} else {
	    password = "root@";
	}
    }
    /*@=branchstate@*/

    /*@-branchstate@*/
    if (fdFileno(u->ctrl) >= 0 && fdWritable(u->ctrl, 0) < 1)
	/*@-refcounttrans@*/ (void) fdClose(u->ctrl); /*@=refcounttrans@*/
    /*@=branchstate@*/

/*@-usereleased@*/
    if (fdFileno(u->ctrl) < 0) {
	rc = tcpConnect(u->ctrl, host, port);
	if (rc < 0)
	    goto errxit2;
    }

    if ((rc = ftpCheckResponse(u, NULL)))
	goto errxit;

    if ((rc = ftpCommand(u, NULL, "USER", user, NULL)))
	goto errxit;

    if ((rc = ftpCommand(u, NULL, "PASS", password, NULL)))
	goto errxit;

    if ((rc = ftpCommand(u, NULL, "TYPE", "I", NULL)))
	goto errxit;

    /*@-compdef@*/
    return 0;
    /*@=compdef@*/

errxit:
    /*@-observertrans@*/
    fdSetSyserrno(u->ctrl, errno, ftpStrerror(rc));
    /*@=observertrans@*/
errxit2:
    /*@-branchstate@*/
    if (fdFileno(u->ctrl) >= 0)
	/*@-refcounttrans@*/ (void) fdClose(u->ctrl); /*@=refcounttrans@*/
    /*@=branchstate@*/
    /*@-compdef@*/
    return rc;
    /*@=compdef@*/
/*@=usereleased@*/
}

int ftpReq(FD_t data, const char * ftpCmd, const char * ftpArg)
{
    urlinfo u = data->url;
    struct sockaddr_in dataAddress;
    char * cmd;
    int cmdlen;
    char * passReply;
    char * chptr;
    int rc;

    URLSANE(u);
    if (ftpCmd == NULL)
	return FTPERR_UNKNOWN;	/* XXX W2DO? */

    cmdlen = strlen(ftpCmd) + (ftpArg ? 1+strlen(ftpArg) : 0) + sizeof("\r\n");
    chptr = cmd = alloca(cmdlen);
    chptr = stpcpy(chptr, ftpCmd);
    if (ftpArg) {
	*chptr++ = ' ';
	chptr = stpcpy(chptr, ftpArg);
    }
    chptr = stpcpy(chptr, "\r\n");
    cmdlen = chptr - cmd;

/*
 * Get the ftp version of the Content-Length.
 */
    if (!strncmp(cmd, "RETR", 4)) {
	unsigned cl;

	passReply = NULL;
	rc = ftpCommand(u, &passReply, "SIZE", ftpArg, NULL);
	if (rc)
	    goto errxit;
	if (sscanf(passReply, "%d %u", &rc, &cl) != 2) {
	    rc = FTPERR_BAD_SERVER_RESPONSE;
	    goto errxit;
	}
	rc = 0;
	data->contentLength = cl;
    }

    passReply = NULL;
    rc = ftpCommand(u, &passReply, "PASV", NULL);
    if (rc) {
	rc = FTPERR_PASSIVE_ERROR;
	goto errxit;
    }

    chptr = passReply;
    while (*chptr && *chptr != '(') chptr++;
    if (*chptr != '(') return FTPERR_PASSIVE_ERROR; 
    chptr++;
    passReply = chptr;
    while (*chptr && *chptr != ')') chptr++;
    if (*chptr != ')') return FTPERR_PASSIVE_ERROR;
    *chptr-- = '\0';

    while (*chptr && *chptr != ',') chptr--;
    if (*chptr != ',') return FTPERR_PASSIVE_ERROR;
    chptr--;
    while (*chptr && *chptr != ',') chptr--;
    if (*chptr != ',') return FTPERR_PASSIVE_ERROR;
    *chptr++ = '\0';
    
    /* now passReply points to the IP portion, and chptr points to the
       port number portion */

    {	int i, j;
	memset(&dataAddress, 0, sizeof(dataAddress));
	dataAddress.sin_family = AF_INET;
	if (sscanf(chptr, "%d,%d", &i, &j) != 2) {
	    rc = FTPERR_PASSIVE_ERROR;
	    goto errxit;
	}
	dataAddress.sin_port = htons((((unsigned)i) << 8) + j);
    }

    chptr = passReply;
    while (*chptr++ != '\0') {
	if (*chptr == ',') *chptr = '.';
    }

    /*@-moduncon@*/
    if (!inet_aton(passReply, &dataAddress.sin_addr)) {
	rc = FTPERR_PASSIVE_ERROR;
	goto errxit;
    }
    /*@=moduncon@*/

    rc = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    fdSetFdno(data, (rc >= 0 ? rc : -1));
    if (rc < 0) {
	rc = FTPERR_FAILED_CONNECT;
	goto errxit;
    }
    data = fdLink(data, "open data (ftpReq)");

    /* XXX setsockopt SO_LINGER */
    /* XXX setsockopt SO_KEEPALIVE */
    /* XXX setsockopt SO_TOS IPTOS_THROUGHPUT */

    /*@-internalglobs@*/
    while (connect(fdFileno(data), (struct sockaddr *) &dataAddress, 
	        sizeof(dataAddress)) < 0)
    {
	if (errno == EINTR)
	    continue;
	rc = FTPERR_FAILED_DATA_CONNECT;
	goto errxit;
    }
    /*@=internalglobs@*/

if (_ftp_debug)
fprintf(stderr, "-> %s", cmd);
    if (fdWrite(u->ctrl, cmd, cmdlen) != cmdlen) {
	rc = FTPERR_SERVER_IO_ERROR;
	goto errxit;
    }

    if ((rc = ftpCheckResponse(u, NULL))) {
	goto errxit;
    }

    data->ftpFileDoneNeeded = 1;
    u->ctrl = fdLink(u->ctrl, "grab data (ftpReq)");
    u->ctrl = fdLink(u->ctrl, "open data (ftpReq)");
    return 0;

errxit:
    /*@-observertrans@*/
    fdSetSyserrno(u->ctrl, errno, ftpStrerror(rc));
    /*@=observertrans@*/
    /*@-branchstate@*/
    if (fdFileno(data) >= 0)
	/*@-refcounttrans@*/ (void) fdClose(data); /*@=refcounttrans@*/
    /*@=branchstate@*/
    return rc;
}

/*@unchecked@*/ /*@null@*/
static rpmCallbackFunction	urlNotify = NULL;

/*@unchecked@*/ /*@null@*/
static void *			urlNotifyData = NULL;

/*@unchecked@*/
static int			urlNotifyCount = -1;

void urlSetCallback(rpmCallbackFunction notify, void *notifyData, int notifyCount) {
    urlNotify = notify;
    urlNotifyData = notifyData;
    urlNotifyCount = (notifyCount >= 0) ? notifyCount : 4096;
}

int ufdCopy(FD_t sfd, FD_t tfd)
{
    char buf[BUFSIZ];
    unsigned int itemsRead;
    unsigned int itemsCopied = 0;
    int rc = 0;
    int notifier = -1;

    if (urlNotify) {
	/*@-noeffectuncon @*/ /* FIX: check rc */
	(void)(*urlNotify) (NULL, RPMCALLBACK_INST_OPEN_FILE,
		0, 0, NULL, urlNotifyData);
	/*@=noeffectuncon @*/
    }
    
    while (1) {
	rc = Fread(buf, sizeof(buf[0]), sizeof(buf), sfd);
	if (rc < 0)
	    break;
	else if (rc == 0) {
	    rc = itemsCopied;
	    break;
	}
	itemsRead = rc;
	rc = Fwrite(buf, sizeof(buf[0]), itemsRead, tfd);
	if (rc < 0)
	    break;
 	if (rc != itemsRead) {
	    rc = FTPERR_FILE_IO_ERROR;
	    break;
	}

	itemsCopied += itemsRead;
	if (urlNotify && urlNotifyCount > 0) {
	    int n = itemsCopied/urlNotifyCount;
	    if (n != notifier) {
		/*@-noeffectuncon @*/ /* FIX: check rc */
		(void)(*urlNotify) (NULL, RPMCALLBACK_INST_PROGRESS,
			itemsCopied, 0, NULL, urlNotifyData);
		/*@=noeffectuncon @*/
		notifier = n;
	    }
	}
    }

/*@-modfilesys@*/
    DBGIO(sfd, (stderr, "++ copied %d bytes: %s\n", itemsCopied,
	ftpStrerror(rc)));
/*@=modfilesys@*/

    if (urlNotify) {
	/*@-noeffectuncon @*/ /* FIX: check rc */
	(void)(*urlNotify) (NULL, RPMCALLBACK_INST_OPEN_FILE,
		itemsCopied, itemsCopied, NULL, urlNotifyData);
	/*@=noeffectuncon @*/
    }
    
    return rc;
}

static int urlConnect(const char * url, /*@out@*/ urlinfo * uret)
	/*@globals fileSystem @*/
	/*@modifies *uret, fileSystem @*/
{
    urlinfo u;
    int rc = 0;

    if (urlSplit(url, &u) < 0)
	return -1;

    if (u->urltype == URL_IS_FTP) {
	FD_t fd;

	if ((fd = u->ctrl) == NULL) {
	    fd = u->ctrl = fdNew("persist ctrl (urlConnect FTP)");
	    fdSetIo(u->ctrl, ufdio);
	}
	
	fd->rd_timeoutsecs = ftpTimeoutSecs;
	fd->contentLength = fd->bytesRemain = -1;
	fd->url = NULL;		/* XXX FTP ctrl has not */
	fd->ftpFileDoneNeeded = 0;
	fd = fdLink(fd, "grab ctrl (urlConnect FTP)");

	if (fdFileno(u->ctrl) < 0) {
	    rpmMessage(RPMMESS_DEBUG, _("logging into %s as %s, pw %s\n"),
			u->host ? u->host : "???",
			u->user ? u->user : "ftp",
			u->password ? u->password : "(username)");

	    if ((rc = ftpLogin(u)) < 0) {	/* XXX save ftpLogin error */
		u->ctrl = fdFree(fd, "grab ctrl (urlConnect FTP)");
		u->openError = rc;
	    }
	}
    }

    if (uret != NULL)
	*uret = urlLink(u, "urlConnect");
    u = urlFree(u, "urlSplit (urlConnect)");	

    return rc;
}

int ufdGetFile(FD_t sfd, FD_t tfd)
{
    int rc;

    FDSANE(sfd);
    FDSANE(tfd);
    rc = ufdCopy(sfd, tfd);
    (void) Fclose(sfd);
    if (rc > 0)		/* XXX ufdCopy now returns no. bytes copied */
	rc = 0;
    return rc;
}

int ftpCmd(const char * cmd, const char * url, const char * arg2)
{
    urlinfo u;
    int rc;
    const char * path;

    if (urlConnect(url, &u) < 0)
	return -1;

    (void) urlPath(url, &path);

    rc = ftpCommand(u, NULL, cmd, path, arg2, NULL);
    u->ctrl = fdFree(u->ctrl, "grab ctrl (ftpCmd)");
    return rc;
}

/* XXX these aren't worth the pain of including correctly */
#if !defined(IAC)
#define	IAC	255		/* interpret as command: */
#endif
#if !defined(IP)
#define	IP	244		/* interrupt process--permanently */
#endif
#if !defined(DM)
#define	DM	242		/* data mark--for connect. cleaning */
#endif
#if !defined(SHUT_RDWR)
#define	SHUT_RDWR	1+1
#endif

static int ftpAbort(urlinfo u, FD_t data)
	/*@globals fileSystem @*/
	/*@modifies u, data, fileSystem @*/
{
    static unsigned char ipbuf[3] = { IAC, IP, IAC };
    FD_t ctrl;
    int rc;
    int tosecs;

    URLSANE(u);

    if (data != NULL) {
	data->ftpFileDoneNeeded = 0;
	if (fdFileno(data) >= 0)
	    u->ctrl = fdFree(u->ctrl, "open data (ftpAbort)");
	u->ctrl = fdFree(u->ctrl, "grab data (ftpAbort)");
    }
    ctrl = u->ctrl;

/*@-modfilesys@*/
    DBGIO(0, (stderr, "-> ABOR\n"));
/*@=modfilesys@*/

/*@-usereleased -compdef@*/
    if (send(fdFileno(ctrl), ipbuf, sizeof(ipbuf), MSG_OOB) != sizeof(ipbuf)) {
	/*@-refcounttrans@*/ (void) fdClose(ctrl); /*@=refcounttrans@*/
	return FTPERR_SERVER_IO_ERROR;
    }

    sprintf(u->buf, "%cABOR\r\n",(char) DM);
    if (fdWrite(ctrl, u->buf, 7) != 7) {
	/*@-refcounttrans@*/ (void) fdClose(ctrl); /*@=refcounttrans@*/
	return FTPERR_SERVER_IO_ERROR;
    }

    if (data && fdFileno(data) >= 0) {
	/* XXX shorten data drain time wait */
	tosecs = data->rd_timeoutsecs;
	data->rd_timeoutsecs = 10;
	if (fdReadable(data, data->rd_timeoutsecs) > 0) {
	    while (timedRead(data, u->buf, u->bufAlloced) > 0)
		u->buf[0] = '\0';
	}
	data->rd_timeoutsecs = tosecs;
	/* XXX ftp abort needs to close the data channel to receive status */
	(void) shutdown(fdFileno(data), SHUT_RDWR);
	(void) close(fdFileno(data));
	data->fps[0].fdno = -1;	/* XXX WRONG but expedient */
    }

    /* XXX shorten ctrl drain time wait */
    tosecs = u->ctrl->rd_timeoutsecs;
    u->ctrl->rd_timeoutsecs = 10;
    if ((rc = ftpCheckResponse(u, NULL)) == FTPERR_NIC_ABORT_IN_PROGRESS) {
	rc = ftpCheckResponse(u, NULL);
    }
    rc = ftpCheckResponse(u, NULL);
    u->ctrl->rd_timeoutsecs = tosecs;

    return rc;
/*@=usereleased =compdef@*/
}

static int ftpFileDone(urlinfo u, FD_t data)
	/*@globals fileSystem @*/
	/*@modifies u, data, fileSystem @*/
{
    int rc = 0;

    URLSANE(u);
    assert(data->ftpFileDoneNeeded);

    if (data->ftpFileDoneNeeded) {
	data->ftpFileDoneNeeded = 0;
	u->ctrl = fdFree(u->ctrl, "open data (ftpFileDone)");
	u->ctrl = fdFree(u->ctrl, "grab data (ftpFileDone)");
	rc = ftpCheckResponse(u, NULL);
    }
    return rc;
}

static int httpResp(urlinfo u, FD_t ctrl, /*@out@*/ char ** str)
	/*@globals fileSystem @*/
	/*@modifies ctrl, *str, fileSystem @*/
{
    int ec = 0;
    int rc;

    URLSANE(u);
    rc = checkResponse(u, ctrl, &ec, str);

if (_ftp_debug && !(rc == 0 && ec == 200))
fprintf(stderr, "*** httpResp: rc %d ec %d\n", rc, ec);

    switch (ec) {
    case 200:
	break;
    default:
	rc = FTPERR_FILE_NOT_FOUND;
	break;
    }

    return rc;
}

static int httpReq(FD_t ctrl, const char * httpCmd, const char * httpArg)
	/*@globals fileSystem @*/
	/*@modifies ctrl, fileSystem @*/
{
    urlinfo u = ctrl->url;
    const char * host;
    const char * path;
    int port;
    int rc;
    char * req;
    size_t len;
    int retrying = 0;

    URLSANE(u);
    assert(ctrl != NULL);

    if (((host = (u->proxyh ? u->proxyh : u->host)) == NULL))
	return FTPERR_BAD_HOSTNAME;

    if ((port = (u->proxyp > 0 ? u->proxyp : u->port)) < 0) port = 80;
    path = (u->proxyh || u->proxyp > 0) ? u->url : httpArg;
    /*@-branchstate@*/
    if (path == NULL) path = "";
    /*@=branchstate@*/

reopen:
    /*@-branchstate@*/
    if (fdFileno(ctrl) >= 0 && (rc = fdWritable(ctrl, 0)) < 1) {
	/*@-refcounttrans@*/ (void) fdClose(ctrl); /*@=refcounttrans@*/
    }
    /*@=branchstate@*/

/*@-usereleased@*/
    if (fdFileno(ctrl) < 0) {
	rc = tcpConnect(ctrl, host, port);
	if (rc < 0)
	    goto errxit2;
	ctrl = fdLink(ctrl, "open ctrl (httpReq)");
    }

    len = sizeof("\
req x HTTP/1.0\r\n\
User-Agent: rpm/3.0.4\r\n\
Host: y:z\r\n\
Accept: text/plain\r\n\
Transfer-Encoding: chunked\r\n\
\r\n\
") + strlen(httpCmd) + strlen(path) + sizeof(VERSION) + strlen(host) + 20;

    req = alloca(len);
    *req = '\0';

  if (!strcmp(httpCmd, "PUT")) {
    sprintf(req, "\
%s %s HTTP/1.%d\r\n\
User-Agent: rpm/%s\r\n\
Host: %s:%d\r\n\
Accept: text/plain\r\n\
Transfer-Encoding: chunked\r\n\
\r\n\
",	httpCmd, path, (u->httpVersion ? 1 : 0), VERSION, host, port);
} else {
    sprintf(req, "\
%s %s HTTP/1.%d\r\n\
User-Agent: rpm/%s\r\n\
Host: %s:%d\r\n\
Accept: text/plain\r\n\
\r\n\
",	httpCmd, path, (u->httpVersion ? 1 : 0), VERSION, host, port);
}

if (_ftp_debug)
fprintf(stderr, "-> %s", req);

    len = strlen(req);
    if (fdWrite(ctrl, req, len) != len) {
	rc = FTPERR_SERVER_IO_ERROR;
	goto errxit;
    }

    /*@-branchstate@*/
    if (!strcmp(httpCmd, "PUT")) {
	ctrl->wr_chunked = 1;
    } else {

	rc = httpResp(u, ctrl, NULL);

	if (rc) {
	    if (!retrying) {	/* not HTTP_OK */
		retrying = 1;
		/*@-refcounttrans@*/ (void) fdClose(ctrl); /*@=refcounttrans@*/
		goto reopen;
	    }
	    goto errxit;
	}
    }
    /*@=branchstate@*/

    ctrl = fdLink(ctrl, "open data (httpReq)");
    return 0;

errxit:
    /*@-observertrans@*/
    fdSetSyserrno(ctrl, errno, ftpStrerror(rc));
    /*@=observertrans@*/
errxit2:
    /*@-branchstate@*/
    if (fdFileno(ctrl) >= 0)
	/*@-refcounttrans@*/ (void) fdClose(ctrl); /*@=refcounttrans@*/
    /*@=branchstate@*/
    return rc;
/*@=usereleased@*/
}

/* XXX DYING: unused */
void * ufdGetUrlinfo(FD_t fd)
{
    FDSANE(fd);
    if (fd->url == NULL)
	return NULL;
    return urlLink(fd->url, "ufdGetUrlinfo");
}

/* =============================================================== */
static ssize_t ufdRead(void * cookie, /*@out@*/ char * buf, size_t count)
	/*@globals fileSystem, internalState @*/
	/*@modifies *buf, fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    int bytesRead;
    int total;

    *buf = '\0';	/* LCL: insistent bugger. */
    /* XXX preserve timedRead() behavior */
    if (fdGetIo(fd) == fdio) {
	struct stat sb;
	int fdno = fdFileno(fd);
	(void) fstat(fdno, &sb);
	if (S_ISREG(sb.st_mode))
	    return fdRead(fd, buf, count);
    }

    UFDONLY(fd);
    assert(fd->rd_timeoutsecs >= 0);

    for (total = 0; total < count; total += bytesRead) {

	int rc;

	bytesRead = 0;

	/* Is there data to read? */
	if (fd->bytesRemain == 0) return total;	/* XXX simulate EOF */
	rc = fdReadable(fd, fd->rd_timeoutsecs);

	switch (rc) {
	case -1:	/* error */
	case  0:	/* timeout */
	    return total;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	default:	/* data to read */
	    /*@switchbreak@*/ break;
	}

	rc = fdRead(fd, buf + total, count - total);

	if (rc < 0) {
	    switch (errno) {
	    case EWOULDBLOCK:
		continue;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    default:
		/*@switchbreak@*/ break;
	    }
if (_rpmio_debug)
fprintf(stderr, "*** read: rc %d errno %d %s \"%s\"\n", rc, errno, strerror(errno), buf);
	    return rc;
	    /*@notreached@*/ break;
	} else if (rc == 0) {
	    return total;
	    /*@notreached@*/ break;
	}
	bytesRead = rc;
    }

    return count;
}

static ssize_t ufdWrite(void * cookie, const char * buf, size_t count)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    int bytesWritten;
    int total = 0;

#ifdef	NOTYET
    if (fdGetIo(fd) == fdio) {
	struct stat sb;
	(void) fstat(fdGetFdno(fd), &sb);
	if (S_ISREG(sb.st_mode))
	    return fdWrite(fd, buf, count);
    }
#endif

    UFDONLY(fd);

    for (total = 0; total < count; total += bytesWritten) {

	int rc;

	bytesWritten = 0;

	/* Is there room to write data? */
	if (fd->bytesRemain == 0) {
fprintf(stderr, "*** ufdWrite fd %p WRITE PAST END OF CONTENT\n", fd);
	    return total;	/* XXX simulate EOF */
	}
	rc = fdWritable(fd, 2);		/* XXX configurable? */

	switch (rc) {
	case -1:	/* error */
	case  0:	/* timeout */
	    return total;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	default:	/* data to write */
	    /*@switchbreak@*/ break;
	}

	rc = fdWrite(fd, buf + total, count - total);

	if (rc < 0) {
	    switch (errno) {
	    case EWOULDBLOCK:
		continue;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    default:
		/*@switchbreak@*/ break;
	    }
if (_rpmio_debug)
fprintf(stderr, "*** write: rc %d errno %d %s \"%s\"\n", rc, errno, strerror(errno), buf);
	    return rc;
	    /*@notreached@*/ break;
	} else if (rc == 0) {
	    return total;
	    /*@notreached@*/ break;
	}
	bytesWritten = rc;
    }

    return count;
}

static inline int ufdSeek(void * cookie, _libio_pos_t pos, int whence)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);

    switch (fd->urlType) {
    case URL_IS_UNKNOWN:
    case URL_IS_PATH:
	break;
    case URL_IS_DASH:
    case URL_IS_FTP:
    case URL_IS_HTTP:
    default:
        return -2;
	/*@notreached@*/ break;
    }
    return fdSeek(cookie, pos, whence);
}

/*@-branchstate@*/
/*@-usereleased@*/	/* LCL: fd handling is tricky here. */
int ufdClose( /*@only@*/ void * cookie)
{
    FD_t fd = c2f(cookie);

    UFDONLY(fd);

    /*@-branchstate@*/
    if (fd->url) {
	urlinfo u = fd->url;

	if (fd == u->data)
		fd = u->data = fdFree(fd, "grab data (ufdClose persist)");
	else
		fd = fdFree(fd, "grab data (ufdClose)");
	(void) urlFree(fd->url, "url (ufdClose)");
	fd->url = NULL;
	u->ctrl = fdFree(u->ctrl, "grab ctrl (ufdClose)");

	if (u->urltype == URL_IS_FTP) {

	    /* XXX if not using libio, lose the fp from fpio */
	    {   FILE * fp;
		/*@+voidabstract -nullpass@*/
		fp = fdGetFILE(fd);
		if (noLibio && fp)
		    fdSetFp(fd, NULL);
		/*@=voidabstract =nullpass@*/
	    }

	    /*
	     * Normal FTP has 4 refs on the data fd:
	     *	"persist data (ufdOpen FTP)"		rpmio.c:888
	     *	"grab data (ufdOpen FTP)"		rpmio.c:892
	     *	"open data (ftpReq)"			ftp.c:633
	     *	"fopencookie"				rpmio.c:1507
	     *
	     * Normal FTP has 5 refs on the ctrl fd:
	     *	"persist ctrl"				url.c:176
	     *	"grab ctrl (urlConnect FTP)"		rpmio.c:404
	     *	"open ctrl"				ftp.c:504
	     *	"grab data (ftpReq)"			ftp.c:661
	     *	"open data (ftpReq)"			ftp.c:662
	     */
	    if (fd->bytesRemain > 0) {
		if (fd->ftpFileDoneNeeded) {
		    if (fdReadable(u->ctrl, 0) > 0)
			(void) ftpFileDone(u, fd);
		    else
			(void) ftpAbort(u, fd);
		}
	    } else {
		int rc;
		/* XXX STOR et al require close before ftpFileDone */
		/*@-refcounttrans@*/
		rc = fdClose(fd);
		/*@=refcounttrans@*/
#if 0	/* XXX error exit from ufdOpen does not have this set */
		assert(fd->ftpFileDoneNeeded != 0);
#endif
		/*@-compdef@*/ /* FIX: u->data undefined */
		if (fd->ftpFileDoneNeeded)
		    (void) ftpFileDone(u, fd);
		/*@=compdef@*/
		return rc;
	    }
	}

	/* XXX Why not (u->urltype == URL_IS_HTTP) ??? */
	if (u->service != NULL && !strcmp(u->service, "http")) {
	    if (fd->wr_chunked) {
		int rc;
	    /* XXX HTTP PUT requires terminating 0 length chunk. */
		(void) fdWrite(fd, NULL, 0);
		fd->wr_chunked = 0;
	    /* XXX HTTP PUT requires terminating entity-header. */
if (_ftp_debug)
fprintf(stderr, "-> \r\n");
		(void) fdWrite(fd, "\r\n", sizeof("\r\n")-1);
		rc = httpResp(u, fd, NULL);
	    }

	    if (fd == u->ctrl)
		fd = u->ctrl = fdFree(fd, "open data (ufdClose HTTP persist ctrl)");
	    else if (fd == u->data)
		fd = u->data = fdFree(fd, "open data (ufdClose HTTP persist data)");
	    else
		fd = fdFree(fd, "open data (ufdClose HTTP)");

	    /*
	     * HTTP has 4 (or 5 if persistent malloc) refs on the fd:
	     *	"persist ctrl"				url.c:177
	     *	"grab ctrl (ufdOpen HTTP)"		rpmio.c:924
	     *	"grab data (ufdOpen HTTP)"		rpmio.c:928
	     *	"open ctrl (httpReq)"			ftp.c:382
	     *	"open data (httpReq)"			ftp.c:435
	     */

	    /* XXX if not using libio, lose the fp from fpio */
	    {   FILE * fp;
		/*@+voidabstract -nullpass@*/
		fp = fdGetFILE(fd);
		if (noLibio && fp)
		    fdSetFp(fd, NULL);
		/*@=voidabstract =nullpass@*/
	    }

	    if (fd->persist && u->httpVersion &&
		(fd == u->ctrl || fd == u->data) && fd->bytesRemain == 0) {
		fd->contentLength = fd->bytesRemain = -1;
		return 0;
	    } else {
		fd->contentLength = fd->bytesRemain = -1;
	    }
	}
    }
    return fdClose(fd);
}
/*@=usereleased@*/
/*@=branchstate@*/

/*@-nullstate@*/	/* FIX: u->{ctrl,data}->url undef after XurlLink. */
/*@null@*/ FD_t ftpOpen(const char *url, /*@unused@*/ int flags,
		/*@unused@*/ mode_t mode, /*@out@*/ urlinfo *uret)
	/*@modifies *uret @*/
{
    urlinfo u = NULL;
    FD_t fd = NULL;

#if 0	/* XXX makeTempFile() heartburn */
    assert(!(flags & O_RDWR));
#endif
    if (urlConnect(url, &u) < 0)
	goto exit;

    if (u->data == NULL)
	u->data = fdNew("persist data (ftpOpen)");

    if (u->data->url == NULL)
	fd = fdLink(u->data, "grab data (ftpOpen persist data)");
    else
	fd = fdNew("grab data (ftpOpen)");

    if (fd) {
	fdSetIo(fd, ufdio);
	fd->ftpFileDoneNeeded = 0;
	fd->rd_timeoutsecs = ftpTimeoutSecs;
	fd->contentLength = fd->bytesRemain = -1;
	fd->url = urlLink(u, "url (ufdOpen FTP)");
	fd->urlType = URL_IS_FTP;
    }

exit:
    if (uret)
	*uret = u;
    /*@-refcounttrans@*/
    return fd;
    /*@=refcounttrans@*/
}
/*@=nullstate@*/

/*@-nullstate@*/	/* FIX: u->{ctrl,data}->url undef after XurlLink. */
static /*@null@*/ FD_t httpOpen(const char * url, /*@unused@*/ int flags,
		/*@unused@*/ mode_t mode, /*@out@*/ urlinfo * uret)
	/*@modifies *uret @*/
{
    urlinfo u = NULL;
    FD_t fd = NULL;

#if 0	/* XXX makeTempFile() heartburn */
    assert(!(flags & O_RDWR));
#endif
    if (urlSplit(url, &u))
	goto exit;

    if (u->ctrl == NULL)
	u->ctrl = fdNew("persist ctrl (httpOpen)");
    if (u->ctrl->nrefs > 2 && u->data == NULL)
	u->data = fdNew("persist data (httpOpen)");

    if (u->ctrl->url == NULL)
	fd = fdLink(u->ctrl, "grab ctrl (httpOpen persist ctrl)");
    else if (u->data->url == NULL)
	fd = fdLink(u->data, "grab ctrl (httpOpen persist data)");
    else
	fd = fdNew("grab ctrl (httpOpen)");

    if (fd) {
	fdSetIo(fd, ufdio);
	fd->ftpFileDoneNeeded = 0;
	fd->rd_timeoutsecs = httpTimeoutSecs;
	fd->contentLength = fd->bytesRemain = -1;
	fd->url = urlLink(u, "url (httpOpen)");
	fd = fdLink(fd, "grab data (httpOpen)");
	fd->urlType = URL_IS_HTTP;
    }

exit:
    if (uret)
	*uret = u;
    /*@-refcounttrans@*/
    return fd;
    /*@=refcounttrans@*/
}
/*@=nullstate@*/

static /*@null@*/ FD_t ufdOpen(const char * url, int flags, mode_t mode)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    FD_t fd = NULL;
    const char * cmd;
    urlinfo u;
    const char * path;
    urltype urlType = urlPath(url, &path);

if (_rpmio_debug)
fprintf(stderr, "*** ufdOpen(%s,0x%x,0%o)\n", url, (unsigned)flags, (unsigned)mode);

    /*@-branchstate@*/
    switch (urlType) {
    case URL_IS_FTP:
	fd = ftpOpen(url, flags, mode, &u);
	if (fd == NULL || u == NULL)
	    break;

	/* XXX W2DO? use STOU rather than STOR to prevent clobbering */
	cmd = ((flags & O_WRONLY) 
		?  ((flags & O_APPEND) ? "APPE" :
		   ((flags & O_CREAT) ? "STOR" : "STOR"))
		:  ((flags & O_CREAT) ? "STOR" : "RETR"));
	u->openError = ftpReq(fd, cmd, path);
	if (u->openError < 0) {
	    /* XXX make sure that we can exit through ufdClose */
	    fd = fdLink(fd, "error data (ufdOpen FTP)");
	} else {
	    fd->bytesRemain = ((!strcmp(cmd, "RETR"))
		?  fd->contentLength : -1);
	    fd->wr_chunked = 0;
	}
	break;
    case URL_IS_HTTP:
	fd = httpOpen(url, flags, mode, &u);
	if (fd == NULL || u == NULL)
	    break;

	cmd = ((flags & O_WRONLY)
		?  ((flags & O_APPEND) ? "PUT" :
		   ((flags & O_CREAT) ? "PUT" : "PUT"))
		: "GET");
	u->openError = httpReq(fd, cmd, path);
	if (u->openError < 0) {
	    /* XXX make sure that we can exit through ufdClose */
	    fd = fdLink(fd, "error ctrl (ufdOpen HTTP)");
	    fd = fdLink(fd, "error data (ufdOpen HTTP)");
	} else {
	    fd->bytesRemain = ((!strcmp(cmd, "GET"))
		?  fd->contentLength : -1);
	    fd->wr_chunked = ((!strcmp(cmd, "PUT"))
		?  fd->wr_chunked : 0);
	}
	break;
    case URL_IS_DASH:
	assert(!(flags & O_RDWR));
	fd = fdDup( ((flags & O_WRONLY) ? STDOUT_FILENO : STDIN_FILENO) );
	if (fd) {
	    fdSetIo(fd, ufdio);
	    fd->rd_timeoutsecs = 600;	/* XXX W2DO? 10 mins? */
	    fd->contentLength = fd->bytesRemain = -1;
	}
	break;
    case URL_IS_PATH:
    case URL_IS_UNKNOWN:
    default:
	fd = fdOpen(path, flags, mode);
	if (fd) {
	    fdSetIo(fd, ufdio);
	    fd->rd_timeoutsecs = 1;
	    fd->contentLength = fd->bytesRemain = -1;
	}
	break;
    }
    /*@=branchstate@*/

    if (fd == NULL) return NULL;
    fd->urlType = urlType;
    if (Fileno(fd) < 0) {
	(void) ufdClose(fd);
	return NULL;
    }
/*@-modfilesys@*/
DBGIO(fd, (stderr, "==>\tufdOpen(\"%s\",%x,0%o) %s\n", url, (unsigned)flags, (unsigned)mode, fdbg(fd)));
/*@=modfilesys@*/
    return fd;
}

/* Return number of threads ought to be used for compression based
   on a parsed value threads (e.g. from w7T0.xzdio or w7T16.xzdio).
   Value -1 means automatic detection. */

static int
get_compression_threads(int threads)
{
    if (threads == -1)
	threads = rpmExpandNumeric("%{getncpus}");

    return threads;
}

static struct FDIO_s ufdio_s = {
  ufdRead, ufdWrite, ufdSeek, ufdClose, XfdLink, XfdFree, XfdNew, fdFileno,
  ufdOpen, NULL, fdGetFp, NULL,	Mkdir, Chdir, Rmdir, Rename, Unlink
};
FDIO_t ufdio = /*@-compmempass@*/ &ufdio_s /*@=compmempass@*/ ;

/* =============================================================== */
/* Support for GZIP library.
 */
#ifdef	HAVE_ZLIB_H
/*@-moduncon@*/

/*@-noparams@*/
#include <zlib.h>
/*@=noparams@*/

static inline /*@dependent@*/ /*@null@*/ void * gzdFileno(FD_t fd)
	/*@*/
{
    void * rc = NULL;
    int i;

    FDSANE(fd);
    for (i = fd->nfps; i >= 0; i--) {
	FDSTACK_t * fps = &fd->fps[i];
	if (fps->io != gzdio)
	    continue;
	rc = fps->fp;
	break;
    }
    
    return rc;
}

#include <stdint.h>
#include <stdbool.h>

struct cpio_state {
	uint32_t n;			/* byte progress in cpio header */
	uint32_t mode;			/* file attributes */
	uint32_t nlnk;
	uint32_t size;
};

#define RSYNC_WIN 4096

struct rsync_state {
	uint32_t n;			/* number of elements in the window */
	uint32_t sum;			/* current sum */
	unsigned char win[RSYNC_WIN];	/* window elements */
};

typedef struct rpmGZFILE_s {
	gzFile gz;			/* gzFile is a pointer */
	struct rsync_state rs;
	struct cpio_state cs;
	uint32_t nb;			/* bytes pending for sync */
} rpmGZFILE;				/* like FILE, to use with star */

static /*@null@*/ FD_t gzdOpen(const char * path, const char * fmode)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    FD_t fd;
    rpmGZFILE *rpmgz;
    rpmgz = calloc(1, sizeof(*rpmgz));
    if (!rpmgz)
	return NULL;
    rpmgz->gz = gzopen(path, fmode);
    if (!rpmgz->gz) {
	free(rpmgz);
	return NULL;
    }
    fd = fdNew("open (gzdOpen)");
    fdPop(fd); fdPush(fd, gzdio, rpmgz, -1);
    
/*@-modfilesys@*/
DBGIO(fd, (stderr, "==>\tgzdOpen(\"%s\", \"%s\") fd %p %s\n", path, fmode, (fd ? fd : NULL), fdbg(fd)));
/*@=modfilesys@*/
    return fdLink(fd, "gzdOpen");
}

/*@-globuse@*/
static /*@null@*/ FD_t gzdFdopen(void * cookie, const char *fmode)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    int fdno;
    rpmGZFILE *rpmgz;

    if (fmode == NULL) return NULL;
    fdno = fdFileno(fd);
    fdSetFdno(fd, -1);		/* XXX skip the fdio close */
    if (fdno < 0) return NULL;
    rpmgz = calloc(1, sizeof(*rpmgz));
    if (!rpmgz)
	return NULL;
    rpmgz->gz = gzdopen(fdno, fmode);
    if (!rpmgz->gz) {
	free(rpmgz);
	return NULL;
    }

    fdPush(fd, gzdio, rpmgz, fdno);		/* Push gzdio onto stack */

    return fdLink(fd, "gzdFdopen");
}
/*@=globuse@*/

/*@-globuse@*/
static int gzdFlush(FD_t fd)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    rpmGZFILE *rpmgz;
    rpmgz = gzdFileno(fd);
    if (rpmgz == NULL) return -2;
    return gzflush(rpmgz->gz, Z_SYNC_FLUSH);	/* XXX W2DO? */
}
/*@=globuse@*/

/* =============================================================== */
/*@-mustmod@*/		/* LCL: *buf is modified */
static ssize_t gzdRead(void * cookie, /*@out@*/ char * buf, size_t count)
	/*@globals fileSystem, internalState @*/
	/*@modifies *buf, fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    rpmGZFILE *rpmgz;
    ssize_t rc;

    if (fd == NULL || fd->bytesRemain == 0) return 0;	/* XXX simulate EOF */

    rpmgz = gzdFileno(fd);
    if (rpmgz == NULL) return -2;	/* XXX can't happen */

    fdstat_enter(fd, FDSTAT_READ);
    /*@-compdef@*/ /* LCL: *buf is undefined */
    rc = gzread(rpmgz->gz, buf, count);
/*@-modfilesys@*/
DBGIO(fd, (stderr, "==>\tgzdRead(%p,%p,%u) rc %lx %s\n", cookie, buf, (unsigned)count, (unsigned long)rc, fdbg(fd)));
/*@=modfilesys@*/
    /*@=compdef@*/
    if (rc < 0) {
	int zerror = 0;
	fd->errcookie = gzerror(rpmgz->gz, &zerror);
	if (zerror == Z_ERRNO) {
	    fd->syserrno = errno;
	    fd->errcookie = strerror(fd->syserrno);
	}
    } else if (rc >= 0) {
	fdstat_exit(fd, FDSTAT_READ, rc);
	/*@-compdef@*/
	if (fd->ndigests && rc > 0)
	    fdUpdateDigests(fd, (const unsigned char *)buf, rc);
	/*@=compdef@*/
    }
    return rc;
}
/*@=mustmod@*/

/* from ../lib/cpio.h */
#define CPIO_NEWC_MAGIC "070701"
#define PHYS_HDR_SIZE 110

#define OFFSET_MODE (sizeof(CPIO_NEWC_MAGIC)-1 + 1*8)
#define OFFSET_NLNK (sizeof(CPIO_NEWC_MAGIC)-1 + 4*8)
#define OFFSET_SIZE (sizeof(CPIO_NEWC_MAGIC)-1 + 6*8)

static inline
int hex(unsigned char c)
{
    if (c >= '0' && c <= '9')
	return c - '0';
    else if (c >= 'a' && c <= 'f')
	return c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
	return c - 'A' + 10;
    return -1;
}

static inline
bool cpio_next(struct cpio_state *s, unsigned char c)
{
    if (s->n >= sizeof(CPIO_NEWC_MAGIC)-1) {
	int d = hex(c);
	if (d < 0) {
	    s->n = 0;
	    return false;
	}
	if (0); /* indent */
	else if (s->n >= OFFSET_MODE && s->n < OFFSET_MODE+8) {
	    if (s->n == OFFSET_MODE)
		s->mode = 0;
	    else
		s->mode <<= 4;
	    s->mode |= d;
	}
	else if (s->n >= OFFSET_NLNK && s->n < OFFSET_NLNK+8) {
	    if (s->n == OFFSET_NLNK)
		s->nlnk = 0;
	    else
		s->nlnk <<= 4;
	    s->nlnk |= d;
	}
	else if (s->n >= OFFSET_SIZE && s->n < OFFSET_SIZE+8) {
	    if (s->n == OFFSET_SIZE)
		s->size = 0;
	    else
		s->size <<= 4;
	    s->size |= d;
	}
	s->n++;
	if (s->n >= PHYS_HDR_SIZE) {
	    s->n = 0;
	    if (!S_ISREG(s->mode) || s->nlnk != 1)
		/* no file data */
		s->size = 0;
	    return true;
	}
    }
    else if (CPIO_NEWC_MAGIC[s->n] == c) {
	s->n++;
    }
    else {
	s->n = 0;
    }
    return false;
}

static inline
bool rsync_next(struct rsync_state *s, unsigned char c)
{
	if (s->n < RSYNC_WIN) {		/* not enough elements */
		s->sum += c;		/* update the sum */
		s->win[s->n++] = c;	/* remember the element */
		return false;		/* no match */
	}
	int i = s->n++ % RSYNC_WIN;	/* wrap up */
	s->sum -= s->win[i];		/* move the window on */
	s->sum += c;
	s->win[i] = c;
	if (s->sum % RSYNC_WIN == 0) {	/* match */
		s->n = 0;		/* reset */
		s->sum = 0;
		return true;
	}
	return false;
}

static inline
bool sync_hint(rpmGZFILE *rpmgz, unsigned char c)
{
    rpmgz->nb++;
    bool cpio_hint = cpio_next(&rpmgz->cs, c);
    if (cpio_hint) {
	/* cpio header/data boundary */
	rpmgz->rs.n = rpmgz->rs.sum = 0;
#define CHUNK 4096
	if (rpmgz->nb >= 2*CHUNK)
	    /* better sync here */
	    goto cpio_sync;
	if (rpmgz->cs.size < CHUNK)
	    /* file is too small */
	    return false;
	if (rpmgz->nb < CHUNK/2)
	    /* not enough pending bytes */
	    return false;
    cpio_sync:
	rpmgz->nb = 0;
	return true;
    }
    bool rsync_hint = rsync_next(&rpmgz->rs, c);
    if (rsync_hint) {
	/* rolling checksum match */
	assert(rpmgz->nb >= RSYNC_WIN);
	rpmgz->nb = 0;
	return true;
    }
    return false;
}

static ssize_t
rsyncable_gzwrite(rpmGZFILE *rpmgz, const unsigned char *const buf, const size_t len)
{
    ssize_t rc;
    ssize_t n_written = 0;
    const unsigned char *begin = buf;
    size_t i;

    for (i = 0; i < len; i++) {
	if (!sync_hint(rpmgz, buf[i]))
	    continue;
	size_t n = i + 1 - (begin - buf);
	rc = gzwrite(rpmgz->gz, begin, n);
	if (rc < 0)
	    return n_written ? n_written : rc;
	n_written += rc;
	if (rc < n)
	    return n_written;
	begin += n;
	rc = gzflush(rpmgz->gz, Z_SYNC_FLUSH);
	if (rc < 0)
	    return n_written ? n_written : rc;
    }
    if (begin < buf + len) {
	size_t n = len - (begin - buf);
	rc = gzwrite(rpmgz->gz, begin, n);
	if (rc < 0)
	    return n_written ? n_written : rc;
	n_written += rc;
    }
    return n_written;
}

static ssize_t gzdWrite(void * cookie, const char * buf, size_t count)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    rpmGZFILE *rpmgz;
    ssize_t rc;

    if (fd == NULL || fd->bytesRemain == 0) return 0;	/* XXX simulate EOF */

    if (fd->ndigests && count > 0)
	fdUpdateDigests(fd, (const unsigned char *)buf, count);

    rpmgz = gzdFileno(fd);
    if (rpmgz == NULL) return -2;	/* XXX can't happen */

    fdstat_enter(fd, FDSTAT_WRITE);
    rc = rsyncable_gzwrite(rpmgz, (void *)buf, count);
/*@-modfilesys@*/
DBGIO(fd, (stderr, "==>\tgzdWrite(%p,%p,%u) rc %lx %s\n", cookie, buf, (unsigned)count, (unsigned long)rc, fdbg(fd)));
/*@=modfilesys@*/
    if (rc < count) {
	int zerror = 0;
	fd->errcookie = gzerror(rpmgz->gz, &zerror);
	if (zerror == Z_ERRNO) {
	    fd->syserrno = errno;
	    fd->errcookie = strerror(fd->syserrno);
	}
    }
    if (rc > 0)
	fdstat_exit(fd, FDSTAT_WRITE, rc);
    return rc;
}

/* XXX zlib-1.0.4 has not */
static inline int gzdSeek(void * cookie, _libio_pos_t pos, int whence)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
#ifdef USE_COOKIE_SEEK_POINTER
    off64_t p = *pos;
#else
    off_t p = pos;
#endif
    int rc;
#if HAVE_GZSEEK
    FD_t fd = c2f(cookie);
    rpmGZFILE *rpmgz;

    if (fd == NULL) return -2;
    assert(fd->bytesRemain == -1);	/* XXX FIXME */

    rpmgz = gzdFileno(fd);
    if (rpmgz == NULL) return -2;	/* XXX can't happen */

    fdstat_enter(fd, FDSTAT_SEEK);
    rc = gzseek(rpmgz->gz, p, whence);
/*@-modfilesys@*/
DBGIO(fd, (stderr, "==>\tgzdSeek(%p,%ld,%d) rc %lx %s\n", cookie, (long)p, whence, (unsigned long)rc, fdbg(fd)));
/*@=modfilesys@*/
    if (rc < 0) {
	int zerror = 0;
	fd->errcookie = gzerror(rpmgz->gz, &zerror);
	if (zerror == Z_ERRNO) {
	    fd->syserrno = errno;
	    fd->errcookie = strerror(fd->syserrno);
	}
    } else if (rc >= 0) {
	fdstat_exit(fd, FDSTAT_SEEK, rc);
    }
#else
    rc = -2;
#endif
    return rc;
}

static int gzdClose( /*@only@*/ void * cookie)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    rpmGZFILE *rpmgz;
    int rc;

    rpmgz = gzdFileno(fd);
    if (rpmgz == NULL) return -2;	/* XXX can't happen */

    fdstat_enter(fd, FDSTAT_CLOSE);
    /*@-dependenttrans@*/
    rc = gzclose(rpmgz->gz);
    free(rpmgz);
    /*@=dependenttrans@*/

    /* XXX TODO: preserve fd if errors */

    if (fd) {
/*@-modfilesys@*/
DBGIO(fd, (stderr, "==>\tgzdClose(%p) zerror %d %s\n", cookie, rc, fdbg(fd)));
/*@=modfilesys@*/
	if (rc < 0) {
	    fd->errcookie = "gzclose error";
	    if (rc == Z_ERRNO) {
		fd->syserrno = errno;
		fd->errcookie = strerror(fd->syserrno);
	    }
	} else if (rc >= 0) {
	    fdstat_exit(fd, FDSTAT_CLOSE, rc);
	}
    }

/*@-modfilesys@*/
DBGIO(fd, (stderr, "==>\tgzdClose(%p) rc %lx %s\n", cookie, (unsigned long)rc, fdbg(fd)));
/*@=modfilesys@*/

    if (_rpmio_debug || rpmIsDebug()) fdstat_print(fd, "GZDIO", stderr);
    /*@-branchstate@*/
    if (rc == 0)
	fd = fdFree(fd, "open (gzdClose)");
    /*@=branchstate@*/
    return rc;
}

static struct FDIO_s gzdio_s = {
  gzdRead, gzdWrite, gzdSeek, gzdClose, XfdLink, XfdFree, XfdNew, fdFileno,
  NULL, gzdOpen, gzdFileno, gzdFlush,	NULL, NULL, NULL, NULL, NULL
};
FDIO_t gzdio = /*@-compmempass@*/ &gzdio_s /*@=compmempass@*/ ;

/*@=moduncon@*/
#endif	/* HAVE_ZLIB_H */

/* =============================================================== */
/* Support for BZIP2 library.
 */
#ifdef HAVE_BZLIB_H
/*@-moduncon@*/

#include <bzlib.h>

#ifdef HAVE_BZ2_1_0
# define bzopen  BZ2_bzopen
# define bzclose BZ2_bzclose
# define bzdopen BZ2_bzdopen
# define bzerror BZ2_bzerror
# define bzflush BZ2_bzflush
# define bzread  BZ2_bzread
# define bzwrite BZ2_bzwrite
#endif /* HAVE_BZ2_1_0 */

static inline /*@dependent@*/ void * bzdFileno(FD_t fd)
	/*@*/
{
    void * rc = NULL;
    int i;

    FDSANE(fd);
    for (i = fd->nfps; i >= 0; i--) {
	FDSTACK_t * fps = &fd->fps[i];
	if (fps->io != bzdio)
	    continue;
	rc = fps->fp;
	break;
    }
    
    return rc;
}

/*@-globuse@*/
static /*@null@*/ FD_t bzdOpen(const char * path, const char * mode)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    FD_t fd;
    BZFILE *bzfile;;
    if ((bzfile = bzopen(path, mode)) == NULL)
	return NULL;
    fd = fdNew("open (bzdOpen)");
    fdPop(fd); fdPush(fd, bzdio, bzfile, -1);
    return fdLink(fd, "bzdOpen");
}
/*@=globuse@*/

/*@-globuse@*/
static /*@null@*/ FD_t bzdFdopen(void * cookie, const char * fmode)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    int fdno;
    BZFILE *bzfile;

    if (fmode == NULL) return NULL;
    fdno = fdFileno(fd);
    fdSetFdno(fd, -1);		/* XXX skip the fdio close */
    if (fdno < 0) return NULL;
    bzfile = bzdopen(fdno, fmode);
    if (bzfile == NULL) return NULL;

    fdPush(fd, bzdio, bzfile, fdno);		/* Push bzdio onto stack */

    return fdLink(fd, "bzdFdopen");
}
/*@=globuse@*/

/*@-globuse@*/
static int bzdFlush(FD_t fd)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    return bzflush(bzdFileno(fd));
}
/*@=globuse@*/

/* =============================================================== */
/*@-globuse@*/
/*@-mustmod@*/		/* LCL: *buf is modified */
static ssize_t bzdRead(void * cookie, /*@out@*/ char * buf, size_t count)
	/*@globals fileSystem, internalState @*/
	/*@modifies *buf, fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    BZFILE *bzfile;
    ssize_t rc = 0;

    if (fd->bytesRemain == 0) return 0;	/* XXX simulate EOF */
    bzfile = bzdFileno(fd);
    fdstat_enter(fd, FDSTAT_READ);
    if (bzfile)
	/*@-compdef@*/
	rc = bzread(bzfile, buf, count);
	/*@=compdef@*/
    if (rc == -1) {
	int zerror = 0;
	if (bzfile)
	    fd->errcookie = bzerror(bzfile, &zerror);
    } else if (rc >= 0) {
	fdstat_exit(fd, FDSTAT_READ, rc);
	/*@-compdef@*/
	if (fd->ndigests && rc > 0)
	    fdUpdateDigests(fd, (const unsigned char *)buf, rc);
	/*@=compdef@*/
    }
    return rc;
}
/*@=mustmod@*/
/*@=globuse@*/

/*@-globuse@*/
static ssize_t bzdWrite(void * cookie, const char * buf, size_t count)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    BZFILE *bzfile;
    ssize_t rc;

    if (fd->bytesRemain == 0) return 0;	/* XXX simulate EOF */

    if (fd->ndigests && count > 0)
	fdUpdateDigests(fd, (const unsigned char *)buf, count);

    bzfile = bzdFileno(fd);
    fdstat_enter(fd, FDSTAT_WRITE);
    rc = bzwrite(bzfile, (void *)buf, count);
    if (rc == -1) {
	int zerror = 0;
	fd->errcookie = bzerror(bzfile, &zerror);
    } else if (rc > 0) {
	fdstat_exit(fd, FDSTAT_WRITE, rc);
    }
    return rc;
}
/*@=globuse@*/

static inline int bzdSeek(void * cookie, /*@unused@*/ _libio_pos_t pos,
			/*@unused@*/ int whence)
	/*@*/
{
    FD_t fd = c2f(cookie);

    BZDONLY(fd);
    return -2;
}

static int bzdClose( /*@only@*/ void * cookie)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    BZFILE *bzfile;
    int rc;

    bzfile = bzdFileno(fd);

    if (bzfile == NULL) return -2;
    fdstat_enter(fd, FDSTAT_CLOSE);
    /*@-noeffectuncon@*/ /* FIX: check rc */
    bzclose(bzfile);
    /*@=noeffectuncon@*/
    rc = 0;	/* XXX FIXME */

    /* XXX TODO: preserve fd if errors */

    if (fd) {
	if (rc == -1) {
	    int zerror = 0;
	    fd->errcookie = bzerror(bzfile, &zerror);
	} else if (rc >= 0) {
	    fdstat_exit(fd, FDSTAT_CLOSE, rc);
	}
    }

/*@-modfilesys@*/
DBGIO(fd, (stderr, "==>\tbzdClose(%p) rc %lx %s\n", cookie, (unsigned long)rc, fdbg(fd)));
/*@=modfilesys@*/

    if (_rpmio_debug || rpmIsDebug()) fdstat_print(fd, "BZDIO", stderr);
    /*@-branchstate@*/
    if (rc == 0)
	fd = fdFree(fd, "open (bzdClose)");
    /*@=branchstate@*/
    return rc;
}

static struct FDIO_s bzdio_s = {
  bzdRead, bzdWrite, bzdSeek, bzdClose, XfdLink, XfdFree, XfdNew, fdFileno,
  NULL, bzdOpen, bzdFileno, bzdFlush,	NULL, NULL, NULL, NULL, NULL
};
FDIO_t bzdio = /*@-compmempass@*/ &bzdio_s /*@=compmempass@*/ ;

/*@=moduncon@*/
#endif	/* HAVE_BZLIB_H */

#include <sys/types.h>
#include <inttypes.h>
#include <lzma.h>
/* Multithreading support in stable API since xz 5.2.0 */
#if LZMA_VERSION >= 50020002
#define HAVE_LZMA_MT
#endif

#define kBufferSize (1 << 15)

typedef struct lzfile {
  /* IO buffer */
    uint8_t buf[kBufferSize];

    lzma_stream strm;
    lzma_options_lzma options;
    lzma_filter filters[2];

    FILE *file;

    int encoding;
    int eof;

} LZFILE;

static LZFILE *lzopen_internal(const char *path, const char *mode, int fd, int xz)
{
    int level = LZMA_PRESET_DEFAULT;
    int encoding = 0;
    FILE *fp;
    LZFILE *lzfile;
    lzma_ret ret;
    uint64_t mem_limit = rpmExpandNumeric("%{_xz_memlimit}");
#ifdef HAVE_LZMA_MT
    int threads = 0;
#endif
    const char * const _mode = mode;

    for (; *mode; mode++) {
	if (*mode == 'w')
	    encoding = 1;
	else if (*mode == 'r')
	    encoding = 0;
	else if (*mode >= '0' && *mode <= '9')
	    level = *mode - '0';
	else if (*mode == 'T') {
	    if (isdigit(*(mode+1))) {
#ifdef HAVE_LZMA_MT
		threads = atoi(++mode);
		/* T0 means automatic detection */
		if (threads == 0)
		    threads = -1;
#endif
		/* skip past rest of digits in string that atoi()
		 * should've processed
		 * */
		while(isdigit(*++mode));
		--mode;
	    }
#ifdef HAVE_LZMA_MT
	    else
		threads = -1;
#endif
	}
    }
    if (fd != -1)
	fp = fdopen(fd, encoding ? "w" : "r");
    else
	fp = fopen(path, encoding ? "w" : "r");
    if (!fp)
	return NULL;
    lzfile = xcalloc(1, sizeof(*lzfile));
    lzfile->file = fp;
    lzfile->encoding = encoding;
    lzfile->eof = 0;
    lzfile->strm = (lzma_stream)LZMA_STREAM_INIT;
    if (encoding) {
	lzma_lzma_preset(&lzfile->options, level);
#if 1	/* tweak options for better compression */
	if (level == 5)
	    /* This level is still used by default in girar-builder, as of
	     * early 2018, due to a historical accident: sometime around 10
	     * years ago it was using a smaller dictionary and was deemed more
	     * appropriate for the minimum system requirements at the time.
	     * Since xz v4.999.9beta-161-gb4b1cbc, level 5 and level 6,
	     * the default one, both use 8M dictionary; the only difference
	     * is that level 5 should compress slightly faster. */
	    lzfile->options.nice_len = 64; /* default 32 */
	else if (level > 5)
	    /* The constant 112 was found to be a local optimum on some cpio
	     * inputs.  Both size and "saved bytes per second" metrics were
	     * taken into account. */
	    lzfile->options.nice_len = 112; /* default 64 */
#endif
	if (xz) {
	    lzfile->filters[0].id = LZMA_FILTER_LZMA2;
	    lzfile->filters[0].options = &lzfile->options;
	    lzfile->filters[1].id = LZMA_VLI_UNKNOWN;
	    /* xz(1) uses CRC64 by default */
#ifdef HAVE_LZMA_MT
	    if (!threads) {
#endif
		ret = lzma_stream_encoder(&lzfile->strm, lzfile->filters, LZMA_CHECK_CRC64);
#ifdef HAVE_LZMA_MT
	    } else {
		threads = get_compression_threads(threads);
		lzma_mt mt_options = {
		    .flags = 0,
		    .threads = threads,
		    .block_size = 0,
		    .timeout = 0,
		    .preset = level,
		    .filters = lzfile->filters,
		    .check = LZMA_CHECK_CRC64 };

#if __SIZEOF_LONG__ == 4
		/* Reduce number of threads for 32-bit systems to limit memory usage. */
		if (threads > 1) {
		    uint64_t memory_usage;
		    uint32_t memlimit = (SIZE_MAX >> 1) + (SIZE_MAX >> 2); /* 3 GiB */

		    if ((personality(0xffffffff) & PER_MASK) == PER_LINUX32)
			memlimit = SIZE_MAX; /* 4 GiB */

		    memlimit -= SIZE_MAX >> 4; /* 256 MiB */

		    void *testmem = NULL; /* While liblzma cannot handle memory allocation errors
					   * robustly, will try to predict failures with guard
					   * malloc test. Calling malloc(3) should be enough, but
					   * will use realloc(3) just to be extra paranoid. */

		    /* keep reducing the number of threads until memory usage gets below limit */
		    while ((memory_usage = lzma_stream_encoder_mt_memusage(&mt_options)) > memlimit ||
			!(testmem = realloc(testmem, memory_usage + (SIZE_MAX >> 4)))) {
			/* number of threads shouldn't be able to hit zero with
			 * compression settings available to set through rpm... */
			if (--mt_options.threads == 0) {
			    mt_options.threads = 1;
			    break;
			}
		    }
		    free(testmem);
		}
#endif
		rpmioThreads = mt_options.threads;
		assert(!lzinfo);
		lzinfo = xasprintf("%s: threads %d memusage %ju",
		    _mode,
		    mt_options.threads,
		    lzma_stream_encoder_mt_memusage(&mt_options));
		ret = lzma_stream_encoder_mt(&lzfile->strm, &mt_options);
	    }
#endif
	} else {
	    ret = lzma_alone_encoder(&lzfile->strm, &lzfile->options);
	}
    } else {
	/* We set the memlimit for decompression to 100MiB which should be
	 * more than enough to be sufficient for level 9 which requires 65 MiB.
	 */
	ret = lzma_auto_decoder(&lzfile->strm, mem_limit ? mem_limit : 100<<20, 0);
    }
    if (ret != LZMA_OK) {
	switch (ret) {
	    case LZMA_MEM_ERROR:
		rpmlog(RPMLOG_ERR, "liblzma: Memory allocation failed (mode %s)\n",
		    lzinfo? lzinfo : _mode);
		break;

	    case LZMA_DATA_ERROR:
		rpmlog(RPMLOG_ERR, "liblzma: File size limits exceeded\n");
		break;

	    default:
		rpmlog(RPMLOG_ERR, "liblzma: <Unknown error (%d), possibly a bug\n", ret);
		break;
	}
	fclose(fp);
	free(lzfile);
	return NULL;
    }
    return lzfile;
}

static LZFILE *xzopen(const char *path, const char *mode)
{
    return lzopen_internal(path, mode, -1, 1);
}

static LZFILE *lzopen(const char *path, const char *mode)
{
    return lzopen_internal(path, mode, -1, 0);
}

static LZFILE *xzdopen(int fd, const char *mode)
{
    if (fd < 0)
	return 0;
    return lzopen_internal(0, mode, fd, 1);
}

static LZFILE *lzdopen(int fd, const char *mode)
{
    if (fd < 0)
	return 0;
    return lzopen_internal(0, mode, fd, 0);
}

static int lzflush(LZFILE *lzfile)
{
    return fflush(lzfile->file);
}

static int lzclose(LZFILE *lzfile)
{
    lzma_ret ret;
    size_t n;
    int rc;

    if (lzinfo) {
	free(lzinfo);
	lzinfo = NULL;
    }
    if (!lzfile)
	return -1;
    if (lzfile->encoding) {
	for (;;) {
	    lzfile->strm.avail_out = kBufferSize;
	    lzfile->strm.next_out = lzfile->buf;
	    ret = lzma_code(&lzfile->strm, LZMA_FINISH);
	    if (ret != LZMA_OK && ret != LZMA_STREAM_END) {
		rpmlog(RPMLOG_ERR, "lzclose: lzma error %d\n", ret);
		return -1;
	    }
	    n = kBufferSize - lzfile->strm.avail_out;
	    if (n && fwrite(lzfile->buf, 1, n, lzfile->file) != n)
		return -1;
	    if (ret == LZMA_STREAM_END)
		break;
	}
    }
    lzma_end(&lzfile->strm);
    rc = fclose(lzfile->file);
    free(lzfile);
    return rc;
}

static ssize_t lzread(LZFILE *lzfile, void *buf, size_t len)
{
    lzma_ret ret;
    int eof = 0;

    if (!lzfile || lzfile->encoding)
      return -1;
    if (lzfile->eof)
      return 0;
    lzfile->strm.next_out = buf;
    lzfile->strm.avail_out = len;
    for (;;) {
	if (!lzfile->strm.avail_in) {
	    lzfile->strm.next_in = lzfile->buf;
	    lzfile->strm.avail_in = fread(lzfile->buf, 1, kBufferSize, lzfile->file);
	    if (!lzfile->strm.avail_in)
		eof = 1;
	}
	ret = lzma_code(&lzfile->strm, LZMA_RUN);
	if (ret == LZMA_STREAM_END) {
	    lzfile->eof = 1;
	    return len - lzfile->strm.avail_out;
	}
	if (ret != LZMA_OK) {
	    rpmlog(RPMLOG_ERR, "lzread: lzma error %d\n", ret);
	    return -1;
	}
	if (!lzfile->strm.avail_out)
	    return len;
	if (eof)
	    return -1;
      }
}

static ssize_t lzwrite(LZFILE *lzfile, void *buf, size_t len)
{
    lzma_ret ret;
    size_t n;
    if (!lzfile || !lzfile->encoding)
	return -1;
    if (!len)
	return 0;
    lzfile->strm.next_in = buf;
    lzfile->strm.avail_in = len;
    for (;;) {
	lzfile->strm.next_out = lzfile->buf;
	lzfile->strm.avail_out = kBufferSize;
	ret = lzma_code(&lzfile->strm, LZMA_RUN);
	if (ret != LZMA_OK) {
	    rpmlog(RPMLOG_ERR, "lzwrite: lzma error %d (%s)\n",
		ret, lzinfo? lzinfo : "");
	    return -1;
	}
	n = kBufferSize - lzfile->strm.avail_out;
	if (n && fwrite(lzfile->buf, 1, n, lzfile->file) != n)
	    return -1;
	if (!lzfile->strm.avail_in)
	    return len;
    }
}

/* =============================================================== */

static inline /*@dependent@*/ void * lzdFileno(FD_t fd)
	/*@*/
{
    void * rc = NULL;
    int i;

    FDSANE(fd);
    for (i = fd->nfps; i >= 0; i--) {
/*@-boundsread@*/
	    FDSTACK_t * fps = &fd->fps[i];
/*@=boundsread@*/
	    if (fps->io != xzdio && fps->io != lzdio)
		continue;
	    rc = fps->fp;
	break;
    }
    
    return rc;
}

/*@-globuse@*/
static /*@null@*/ FD_t xzdOpen(const char * path, const char * mode)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    FD_t fd;
    LZFILE *lzfile;
    if ((lzfile = xzopen(path, mode)) == NULL)
	return NULL;
    fd = fdNew("open (xzdOpen)");
    fdPop(fd); fdPush(fd, xzdio, lzfile, -1);
    return fdLink(fd, "xzdOpen");
}
/*@=globuse@*/

/*@-globuse@*/
static /*@null@*/ FD_t lzdOpen(const char * path, const char * mode)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    FD_t fd;
    LZFILE *lzfile;
    if ((lzfile = lzopen(path, mode)) == NULL)
	return NULL;
    fd = fdNew("open (lzdOpen)");
    fdPop(fd); fdPush(fd, lzdio, lzfile, -1);
    return fdLink(fd, "lzdOpen");
}
/*@=globuse@*/

/*@-globuse@*/
static /*@null@*/ FD_t xzdFdopen(void * cookie, const char * fmode)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    int fdno;
    LZFILE *lzfile;

    if (fmode == NULL) return NULL;
    fdno = fdFileno(fd);
    fdSetFdno(fd, -1);          /* XXX skip the fdio close */
    if (fdno < 0) return NULL;
    lzfile = xzdopen(fdno, fmode);
    if (lzfile == NULL) return NULL;
    fdPush(fd, xzdio, lzfile, fdno);
    return fdLink(fd, "xzdFdopen");
}
/*@=globuse@*/

/*@-globuse@*/
static /*@null@*/ FD_t lzdFdopen(void * cookie, const char * fmode)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    int fdno;
    LZFILE *lzfile;

    if (fmode == NULL) return NULL;
    fdno = fdFileno(fd);
    fdSetFdno(fd, -1);          /* XXX skip the fdio close */
    if (fdno < 0) return NULL;
    lzfile = lzdopen(fdno, fmode);
    if (lzfile == NULL) return NULL;
    fdPush(fd, lzdio, lzfile, fdno);
    return fdLink(fd, "lzdFdopen");
}
/*@=globuse@*/

/*@-globuse@*/
static int lzdFlush(FD_t fd)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    return lzflush(lzdFileno(fd));
}
/*@=globuse@*/

/* =============================================================== */
/*@-globuse@*/
/*@-mustmod@*/          /* LCL: *buf is modified */
static ssize_t lzdRead(void * cookie, /*@out@*/ char * buf, size_t count)
	/*@globals fileSystem, internalState @*/
	/*@modifies *buf, fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    LZFILE *lzfile;
    ssize_t rc = 0;

    if (fd->bytesRemain == 0) return 0; /* XXX simulate EOF */
    lzfile = lzdFileno(fd);
    fdstat_enter(fd, FDSTAT_READ);
    if (lzfile)
	/*@-compdef@*/
	rc = lzread(lzfile, buf, count);
	/*@=compdef@*/
    if (rc == -1) {
	fd->errcookie = "Lzma: decoding error";
    } else if (rc >= 0) {
	fdstat_exit(fd, FDSTAT_READ, rc);
	/*@-compdef@*/
	if (fd->ndigests && rc > 0)
	    fdUpdateDigests(fd, (const unsigned char *)buf, rc);
	/*@=compdef@*/
    }
    return rc;
}
/*@=mustmod@*/
/*@=globuse@*/

/*@-globuse@*/
static ssize_t lzdWrite(void * cookie, const char * buf, size_t count)
/*@globals fileSystem, internalState @*/
/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    LZFILE *lzfile;
    ssize_t rc = 0;

    if (fd == NULL || fd->bytesRemain == 0) return 0;   /* XXX simulate EOF */

    if (fd->ndigests && count > 0)
	fdUpdateDigests(fd, (const unsigned char *)buf, count);

    lzfile = lzdFileno(fd);

    fdstat_enter(fd, FDSTAT_WRITE);
    rc = lzwrite(lzfile, (void *)buf, count);
    if (rc == -1) {
	fd->errcookie = "Lzma: encoding error";
    } else if (rc > 0) {
	fdstat_exit(fd, FDSTAT_WRITE, rc);
    }
    return rc;
}

static inline int lzdSeek(void * cookie, /*@unused@*/ _libio_pos_t pos,
			/*@unused@*/ int whence)
	/*@*/
{
    FD_t fd = c2f(cookie);

    LZDONLY(fd);
    return -2;
}

static int lzdClose( /*@only@*/ void * cookie)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    LZFILE *lzfile;
    int rc;

    lzfile = lzdFileno(fd);

    if (lzfile == NULL) return -2;
    fdstat_enter(fd, FDSTAT_CLOSE);
    /*@-dependenttrans@*/
    rc = lzclose(lzfile);
    /*@=dependenttrans@*/

    /* XXX TODO: preserve fd if errors */

    if (fd) {
	if (rc == -1) {
	    fd->errcookie = strerror(ferror(lzfile->file));
	} else if (rc >= 0) {
	    fdstat_exit(fd, FDSTAT_CLOSE, rc);
	}
    }

DBGIO(fd, (stderr, "==>\tlzdClose(%p) rc %lx %s\n", cookie, (unsigned long)rc, fdbg(fd)));

    if (_rpmio_debug || rpmIsDebug()) fdstat_print(fd, "LZDIO", stderr);
    /*@-branchstate@*/
    if (rc == 0)
	fd = fdFree(fd, "open (lzdClose)");
    /*@=branchstate@*/
    return rc;
}

/*@-type@*/ /* LCL: function typedefs */
static struct FDIO_s lzdio_s = {
  lzdRead, lzdWrite, lzdSeek, lzdClose, XfdLink, XfdFree, XfdNew, fdFileno,
  NULL, lzdOpen, lzdFileno, lzdFlush,	NULL, NULL, NULL, NULL, NULL
};
/*@=type@*/
FDIO_t lzdio = /*@-compmempass@*/ &lzdio_s /*@=compmempass@*/ ;

/*@-type@*/ /* LCL: function typedefs */
static struct FDIO_s xzdio_s = {
  lzdRead, lzdWrite, lzdSeek, lzdClose, XfdLink, XfdFree, XfdNew, fdFileno,
  NULL, xzdOpen, lzdFileno, lzdFlush,	NULL, NULL, NULL, NULL, NULL
};
/*@=type@*/
FDIO_t xzdio = /*@-compmempass@*/ &xzdio_s /*@=compmempass@*/ ;

/* =============================================================== */
/*@observer@*/
static const char * getFdErrstr (FD_t fd)
	/*@*/
{
    const char *errstr = NULL;

#ifdef	HAVE_ZLIB_H
    if (fdGetIo(fd) == gzdio) {
	errstr = fd->errcookie;
    } else
#endif	/* HAVE_ZLIB_H */

#ifdef	HAVE_BZLIB_H
    if (fdGetIo(fd) == bzdio) {
	errstr = fd->errcookie;
    } else
#endif	/* HAVE_BZLIB_H */
    if (fdGetIo(fd) == xzdio || fdGetIo(fd) == lzdio) {
	errstr = fd->errcookie;
    } else
    {
	errstr = (fd->syserrno ? strerror(fd->syserrno) : "");
    }

    return errstr;
}

/* =============================================================== */

const char *Fstrerror(FD_t fd)
{
    if (fd == NULL)
	return (errno ? strerror(errno) : "");
    FDSANE(fd);
    return getFdErrstr(fd);
}

#define	FDIOVEC(_fd, _vec)	\
  ((fdGetIo(_fd) && fdGetIo(_fd)->_vec) ? fdGetIo(_fd)->_vec : NULL)

size_t Fread(void *buf, size_t size, size_t nmemb, FD_t fd) {
    fdio_read_function_t _read;
    int rc;

    FDSANE(fd);
#ifdef __LCLINT__
    *(char *)buf = '\0';
#endif
/*@-modfilesys@*/
DBGIO(fd, (stderr, "==> Fread(%p,%u,%u,%p) %s\n", buf, (unsigned)size, (unsigned)nmemb, (fd ? fd : NULL), fdbg(fd)));
/*@=modfilesys@*/

    if (fdGetIo(fd) == fpio) {
	/*@+voidabstract -nullpass@*/
	rc = fread(buf, size, nmemb, fdGetFILE(fd));
	/*@=voidabstract =nullpass@*/
	return rc;
    }

    /*@-nullderef@*/
    _read = FDIOVEC(fd, read);
    /*@=nullderef@*/

    rc = (_read ? (*_read) (fd, buf, size * nmemb) : -2);
    return rc;
}

size_t Fwrite(const void *buf, size_t size, size_t nmemb, FD_t fd)
{
    FDSANE(fd);
/*@-modfilesys@*/
DBGIO(fd, (stderr, "==> Fwrite(%p,%u,%u,%p) %s\n", buf, (unsigned)size, (unsigned)nmemb, (fd ? fd : NULL), fdbg(fd)));
/*@=modfilesys@*/

    if (fdGetIo(fd) == fpio) {
	/*@+voidabstract -nullpass@*/
	size_t ret = fwrite(buf, size, nmemb, fdGetFILE(fd));
	/*@=voidabstract =nullpass@*/
	return ret * size;
    }

    /*@-nullderef@*/
    fdio_write_function_t _write = FDIOVEC(fd, write);
    /*@=nullderef@*/
    assert(_write);

    // XXX check for overflow instead of assuming that size=1.
    size_t n = size * nmemb;

    // XXX sloppy mixing of size_t and ssize_t is going on here.
    ssize_t ret = _write(fd, buf, n);
    if (ret == -1)
	return 0;
    if (ret == n)
	return n;
    if (ret < 0)
	return 0;
    return ret;
}

int Fseek(FD_t fd, _libio_off_t offset, int whence) {
    fdio_seek_function_t _seek;
#ifdef USE_COOKIE_SEEK_POINTER
    off64_t o64 = offset;
    _libio_pos_t pos = &o64;
#else
    _libio_pos_t pos = offset;
#endif

    long int rc;

    FDSANE(fd);
/*@-modfilesys@*/
DBGIO(fd, (stderr, "==> Fseek(%p,%ld,%d) %s\n", fd, (long)offset, whence, fdbg(fd)));
/*@=modfilesys@*/

    if (fdGetIo(fd) == fpio) {
	FILE *fp;

	/*@+voidabstract -nullpass@*/
	fp = fdGetFILE(fd);
	rc = fseek(fp, offset, whence);
	/*@=voidabstract =nullpass@*/
	return rc;
    }

    /*@-nullderef@*/
    _seek = FDIOVEC(fd, seek);
    /*@=nullderef@*/

    rc = (_seek ? _seek(fd, pos, whence) : -2);
    return rc;
}

int Fclose(FD_t fd)
{
    int rc = 0, ec = 0;

    FDSANE(fd);
/*@-modfilesys@*/
DBGIO(fd, (stderr, "==> Fclose(%p) %s\n", (fd ? fd : NULL), fdbg(fd)));
/*@=modfilesys@*/

    fd = fdLink(fd, "Fclose");
    /*@-branchstate@*/
    while (fd->nfps >= 0) {
	FDSTACK_t * fps = &fd->fps[fd->nfps];
	
	if (fps->io == fpio) {
	    FILE *fp;
	    int fpno;

	    /*@+voidabstract -nullpass@*/
	    fp = fdGetFILE(fd);
	    fpno = fileno(fp);
	    /*@=voidabstract =nullpass@*/
	/* XXX persistent HTTP/1.1 returns the previously opened fp */
	    if (fd->nfps > 0 && fpno == -1 &&
		fd->fps[fd->nfps-1].io == ufdio &&
		fd->fps[fd->nfps-1].fp == fp &&
		fd->fps[fd->nfps-1].fdno >= 0)
	    {
		if (fp)
		    rc = fflush(fp);
		fd->nfps--;
		/*@-refcounttrans@*/
		rc = ufdClose(fd);
		/*@=refcounttrans@*/
/*@-usereleased@*/
		if (fdGetFdno(fd) >= 0)
		    break;
		fdSetFp(fd, NULL);
		fd->nfps++;
		if (fp)
		    rc = fclose(fp);
		fdPop(fd);
		if (noLibio)
		    fdSetFp(fd, NULL);
	    } else {
		if (fp)
		    rc = fclose(fp);
		if (fpno == -1) {
		    fd = fdFree(fd, "fopencookie (Fclose)");
		    fdPop(fd);
		}
	    }
	} else {
	    /*@-nullderef@*/
	    fdio_close_function_t _close = FDIOVEC(fd, close);
	    /*@=nullderef@*/
	    rc = _close(fd);
	}
	if (fd->nfps == 0)
	    break;
	if (ec == 0 && rc)
	    ec = rc;
	fdPop(fd);
    }
    /*@=branchstate@*/
    fd = fdFree(fd, "Fclose");
    return ec;
/*@=usereleased@*/
}

/**
 * Convert stdio fmode to open(2) mode, filtering out zlib/bzlib flags.
 *	returns stdio[0] = '\0' on error.
 *
 * - gzopen:	[0-9] is compession level
 * - gzopen:	'f' is filtered (Z_FILTERED)
 * - gzopen:	'h' is Huffman encoding (Z_HUFFMAN_ONLY)
 * - bzopen:	[1-9] is block size (modulo 100K)
 * - bzopen:	's' is smallmode
 * - HACK:	'.' terminates, rest is type of I/O
 */
static inline void cvtfmode (const char *m,
				/*@out@*/ char *stdio, size_t nstdio,
				/*@out@*/ char *other, size_t nother,
				/*@out@*/ const char **end, /*@out@*/ int * f)
	/*@modifies *stdio, *other, *end, *f @*/
{
    int flags = 0;
    char c;

    switch (*m) {
    case 'a':
	flags |= O_WRONLY | O_CREAT | O_APPEND;
	if (--nstdio > 0) *stdio++ = *m;
	break;
    case 'w':
	flags |= O_WRONLY | O_CREAT | O_TRUNC;
	if (--nstdio > 0) *stdio++ = *m;
	break;
    case 'r':
	flags |= O_RDONLY;
	if (--nstdio > 0) *stdio++ = *m;
	break;
    default:
	*stdio = '\0';
	return;
	/*@notreached@*/ break;
    }
    m++;

    while ((c = *m++) != '\0') {
	switch (c) {
	case '.':
	    /*@switchbreak@*/ break;
	case '+':
	    flags &= ~(O_RDONLY|O_WRONLY);
	    flags |= O_RDWR;
	    if (--nstdio > 0) *stdio++ = c;
	    continue;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	case 'b':
	    if (--nstdio > 0) *stdio++ = c;
	    continue;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	case 'x':
	    flags |= O_EXCL;
	    if (--nstdio > 0) *stdio++ = c;
	    continue;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	default:
	    if (--nother > 0) *other++ = c;
	    continue;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	}
	break;
    }

    *stdio = *other = '\0';
    if (end != NULL)
	*end = (*m != '\0' ? m : NULL);
    if (f != NULL)
	*f = flags;
}

FD_t Fdopen(FD_t ofd, const char *fmode)
{
    char stdio[20], other[20], zstdio[40];
    const char *end = NULL;
    FDIO_t iof = NULL;
    FD_t fd = ofd;

if (_rpmio_debug)
fprintf(stderr, "*** Fdopen(%p,%s) %s\n", fd, fmode, fdbg(fd));
    FDSANE(fd);

    if (fmode == NULL)
	return NULL;

    cvtfmode(fmode, stdio, sizeof(stdio), other, sizeof(other), &end, NULL);
    if (stdio[0] == '\0')
	return NULL;
    zstdio[0] = '\0';
    strncat(zstdio, stdio, sizeof(zstdio) - 1 - strlen(zstdio));
    strncat(zstdio, other, sizeof(zstdio) - 1 - strlen(zstdio));

    if (end == NULL && other[0] == '\0')
	/*@-refcounttrans -retalias@*/ return fd; /*@=refcounttrans =retalias@*/

    /*@-branchstate@*/
    if (end && *end) {
	if (!strcmp(end, "fdio")) {
	    iof = fdio;
	} else if (!strcmp(end, "gzdio")) {
	    iof = gzdio;
	    /*@-internalglobs@*/
	    fd = gzdFdopen(fd, zstdio);
	    /*@=internalglobs@*/
#ifdef HAVE_BZLIB_H
	} else if (!strcmp(end, "bzdio")) {
	    iof = bzdio;
	    /*@-internalglobs@*/
	    fd = bzdFdopen(fd, zstdio);
	    /*@=internalglobs@*/
#endif
	} else if (!strcmp(end, "lzdio")) {
	    iof = lzdio;
	    fd = lzdFdopen(fd, zstdio);
	} else if (!strcmp(end, "xzdio")) {
	    iof = xzdio;
	    fd = xzdFdopen(fd, zstdio);
	} else if (!strcmp(end, "ufdio")) {
	    iof = ufdio;
	} else if (!strcmp(end, "fadio")) {
	    iof = fadio;
	} else if (!strcmp(end, "fpio")) {
	    iof = fpio;
	    if (noLibio) {
		int fdno = Fileno(fd);
		FILE * fp = fdopen(fdno, stdio);
/*@+voidabstract -nullpass@*/
if (_rpmio_debug)
fprintf(stderr, "*** Fdopen fpio fp %p\n", (void *)fp);
/*@=voidabstract =nullpass@*/
		if (fp == NULL)
		    return NULL;
		/* XXX gzdio/bzdio use fp for private data */
		/*@+voidabstract@*/
		if (fdGetFp(fd) == NULL)
		    fdSetFp(fd, fp);
		fdPush(fd, fpio, fp, fdno);	/* Push fpio onto stack */
		/*@=voidabstract@*/
	    }
	}
    } else if (other[0] != '\0') {
	for (end = other; *end && strchr("0123456789fh", *end); end++)
	    {};
	if (*end == '\0') {
	    iof = gzdio;
	    /*@-internalglobs@*/
	    fd = gzdFdopen(fd, zstdio);
	    /*@=internalglobs@*/
	}
    }
    /*@=branchstate@*/
    if (iof == NULL)
	/*@-refcounttrans -retalias@*/ return fd; /*@=refcounttrans =retalias@*/

    if (!noLibio) {
	FILE * fp = NULL;

#ifdef HAVE_COOKIE_IO_FUNCTIONS_T
	{   cookie_io_functions_t ciof;
	    ciof.read = iof->read;
	    ciof.write = iof->write;
	    ciof.seek = iof->seek;
	    ciof.close = iof->close;
	    fp = fopencookie(fd, stdio, ciof);
/*@-modfilesys@*/
DBGIO(fd, (stderr, "==> fopencookie(%p,\"%s\",*%p) returns fp %p\n", fd, stdio, iof, fp));
/*@=modfilesys@*/
	}
#endif

	/*@-branchstate@*/
	if (fp) {
	    /* XXX gzdio/bzdio use fp for private data */
	    /*@+voidabstract -nullpass@*/
	    if (fdGetFp(fd) == NULL)
		fdSetFp(fd, fp);
	    fdPush(fd, fpio, fp, fileno(fp));	/* Push fpio onto stack */
	    /*@=voidabstract =nullpass@*/
	    fd = fdLink(fd, "fopencookie");
	}
	/*@=branchstate@*/
    }

/*@-modfilesys@*/
DBGIO(fd, (stderr, "==> Fdopen(%p,\"%s\") returns fd %p %s\n", ofd, fmode, (fd ? fd : NULL), fdbg(fd)));
/*@=modfilesys@*/
    /*@-refcounttrans -retalias@*/ return fd; /*@=refcounttrans =retalias@*/
}

FD_t Fopen(const char *path, const char *fmode)
{
    char stdio[20], other[20];
    const char *end = NULL;
    mode_t perms = 0666;
    int flags;
    FD_t fd;

    if (path == NULL || fmode == NULL)
	return NULL;

    cvtfmode(fmode, stdio, sizeof(stdio), other, sizeof(other), &end, &flags);
    if (stdio[0] == '\0')
	return NULL;

    /*@-branchstate@*/
    if (end == NULL || !strcmp(end, "fdio")) {
if (_rpmio_debug)
fprintf(stderr, "*** Fopen fdio path %s fmode %s\n", path, fmode);
	fd = fdOpen(path, flags, perms);
	if (fdFileno(fd) < 0) {
	    if (fd) (void) fdClose(fd);
	    return NULL;
	}
    } else if (!strcmp(end, "fadio")) {
if (_rpmio_debug)
fprintf(stderr, "*** Fopen fadio path %s fmode %s\n", path, fmode);
	fd = fadio->_open(path, flags, perms);
	if (fdFileno(fd) < 0) {
	    /*@-refcounttrans@*/ (void) fdClose(fd); /*@=refcounttrans@*/
	    return NULL;
	}
    } else {
	FILE *fp;
	int fdno;
	int isHTTP = 0;

	/* XXX gzdio and bzdio here too */

	switch (urlIsURL(path)) {
	case URL_IS_HTTP:
	    isHTTP = 1;
	    /*@fallthrough@*/
	case URL_IS_PATH:
	case URL_IS_DASH:
	case URL_IS_FTP:
	case URL_IS_UNKNOWN:
if (_rpmio_debug)
fprintf(stderr, "*** Fopen ufdio path %s fmode %s\n", path, fmode);
	    fd = ufdOpen(path, flags, perms);
	    if (fd == NULL || fdFileno(fd) < 0)
		return fd;
	    break;
	default:
if (_rpmio_debug)
fprintf(stderr, "*** Fopen WTFO path %s fmode %s\n", path, fmode);
	    return NULL;
	    /*@notreached@*/ break;
	}

	/* XXX persistent HTTP/1.1 returns the previously opened fp */
	if (isHTTP && ((fp = fdGetFp(fd)) != NULL) && ((fdno = fdGetFdno(fd)) >= 0)) {
	    /*@+voidabstract@*/
	    fdPush(fd, fpio, fp, fileno(fp));	/* Push fpio onto stack */
	    /*@=voidabstract@*/
	    return fd;
	}
    }
    /*@=branchstate@*/

    /*@-branchstate@*/
    if (fd)
	fd = Fdopen(fd, fmode);
    /*@=branchstate@*/
    return fd;
}

int Fflush(FD_t fd)
{
    void * vh;
    if (fd == NULL) return -1;
    if (fdGetIo(fd) == fpio)
	/*@+voidabstract -nullpass@*/
	return fflush(fdGetFILE(fd));
	/*@=voidabstract =nullpass@*/

    vh = fdGetFp(fd);
    if (vh && fdGetIo(fd) == gzdio)
	return gzdFlush(vh);
#ifdef HAVE_BZLIB_H
    if (vh && fdGetIo(fd) == bzdio)
	return bzdFlush(vh);
#endif
    if (vh && (fdGetIo(fd) == xzdio || fdGetIo(fd) == lzdio))
	return lzdFlush(vh);
/* FIXME: If we get here, something went wrong above */
    return 0;
}

int Ferror(FD_t fd)
{
    int i, rc = 0;

    if (fd == NULL) return -1;
    for (i = fd->nfps; rc == 0 && i >= 0; i--) {
	FDSTACK_t * fps = &fd->fps[i];
	int ec;
	
	if (fps->io == fpio) {
	    /*@+voidabstract -nullpass@*/
	    ec = ferror(fdGetFILE(fd));
	    /*@=voidabstract =nullpass@*/
	} else if (fps->io == gzdio) {
	    ec = (fd->syserrno || fd->errcookie != NULL) ? -1 : 0;
	    i--;	/* XXX fdio under gzdio always has fdno == -1 */
#ifdef HAVE_BZLIB_H
	} else if (fps->io == bzdio) {
	    ec = (fd->syserrno  || fd->errcookie != NULL) ? -1 : 0;
	    i--;	/* XXX fdio under bzdio always has fdno == -1 */
#endif
	} else if (fps->io == xzdio || fps->io == lzdio) {
	    ec = (fd->syserrno  || fd->errcookie != NULL) ? -1 : 0;
	    i--;	/* XXX fdio under xzdio/lzdio always has fdno == -1 */
	} else {
	/* XXX need to check ufdio/gzdio/bzdio/fdio errors correctly. */
	    ec = (fdFileno(fd) < 0 ? -1 : 0);
	}

	if (rc == 0 && ec)
	    rc = ec;
    }
/*@-modfilesys@*/
DBGIO(fd, (stderr, "==> Ferror(%p) rc %d %s\n", fd, rc, fdbg(fd)));
/*@=modfilesys@*/
    return rc;
}

int Fileno(FD_t fd)
{
    int i, rc = -1;

    for (i = fd->nfps ; rc == -1 && i >= 0; i--) {
	rc = fd->fps[i].fdno;
    }
/*@-modfilesys@*/
DBGIO(fd, (stderr, "==> Fileno(%p) rc %d %s\n", (fd ? fd : NULL), rc, fdbg(fd)));
/*@=modfilesys@*/
    return rc;
}

/* XXX this is naive */
int Fcntl(FD_t fd, int op, void *lip)
{
    return fcntl(Fileno(fd), op, lip);
}

/* =============================================================== */
/* Helper routines that may be generally useful.
 */

int rpmioSlurp(const char * fn, const byte ** bp, ssize_t * blenp)
{
    static ssize_t blenmax = (8 * BUFSIZ);
    ssize_t blen = 0;
    byte * b = NULL;
    ssize_t size;
    FD_t fd;
    int rc = 0;

    fd = Fopen(fn, "r.ufdio");
    if (fd == NULL || Ferror(fd)) {
	rc = 2;
	goto exit;
    }

    size = fdSize(fd);
    blen = (size >= 0 ? size : blenmax);
    /*@-branchstate@*/
    if (blen) {
	int nb;
	b = xmalloc(blen+1);
	b[0] = '\0';
	nb = Fread(b, sizeof(*b), blen, fd);
	if (Ferror(fd) || (size > 0 && nb != blen)) {
	    rc = 1;
	    goto exit;
	}
	if (blen == blenmax && nb < blen) {
	    blen = nb;
	    b = xrealloc(b, blen+1);
	}
	b[blen] = '\0';
    }
    /*@=branchstate@*/

exit:
    if (fd) (void) Fclose(fd);
	
    if (rc) {
	if (b) free(b);
	b = NULL;
	blen = 0;
    }

    if (bp) *bp = b;
    else if (b) free(b);

    if (blenp) *blenp = blen;

    return rc;
}

static struct FDIO_s fpio_s = {
  ufdRead, ufdWrite, fdSeek, ufdClose, XfdLink, XfdFree, XfdNew, fdFileno,
  ufdOpen, NULL, fdGetFp, NULL,	Mkdir, Chdir, Rmdir, Rename, Unlink
};
FDIO_t fpio = /*@-compmempass@*/ &fpio_s /*@=compmempass@*/ ;
/*@=type@*/
