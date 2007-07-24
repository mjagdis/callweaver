/* $Id: event.c,v 1.17 2004/05/29 14:32:32 mjt Exp $
 * Timer and I/O Event core
 * Author: Michael Tokarev, <mjt@corpit.ru>
 * License: LGPL.
 */

#if defined(HAVE_CONFIG_H)
#include <confdefs.h>
#endif

#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/resource.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <signal.h>

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif
#if defined(HAVE_EPOLL)
#include <sys/epoll.h>
#if defined(HAVE_POLL)
#undef HAVE_POLL
#endif
#define HAVE_POLL 1
#endif
#if defined(HAVE_DEVPOLL)
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/devpoll.h>
#include <sys/ioctl.h>
#endif
#if defined(HAVE_KQUEUE)
#include <sys/event.h>
#endif
#if defined(HAVE_POLL)
#include <sys/poll.h>
#endif

#include "callweaver/eventlib.h"

#define ARR_ROUND(x) (((x) < 64)  ?  (((x) & ~3) + 4)  :  (((x) & ~31) + 32))

struct ev_fd
{
    /* Information about a file descriptor */
    short int events;   /* bitmask: events of interest */
    short int revents;  /* bitmask: events ready */
#if defined(HAVE_POLL)
    int pfdi;           /* index in ct.pfd[] array */
#endif
    ev_io_cbck_f *cbck; /* application callback routine */
    void *user_data;    /* application data */
    struct ev_fd *next; /* next in ready list */
};

struct ev_method_s;

#if 0
struct ev_sig
{
    struct ev_ct *ct;
    ev_sig_cbck_f *cbck;
    void *user_data;
    int raised;
    struct ev_sig *next;
};
#endif

struct ev_ct
{
    /* The event context */
    struct ev_tm *tmhead;   /*! List of timers - seen forwards */
    struct ev_tm *tmtail;   /*! List of timers - seen backwards */
    int tmcnt;              /*! Current number of timers */
    ev_time_t tmsum;        /*! Sum of `when' values of all timers */

    volatile int loop;      /*! Re-enterancy protection */

    /* Common fields */
    struct ev_fd *efd;      /*! Array of FD structures (dynalloc), indexed by fd */
    int aefd;               /*! Number of entries allocated in efd */

    int maxfd;              /*! Max FD number so far */
    int nfd;                /*! Total number of FDs monitored */

    const struct ev_method_s *method; /*! Current I/O method */

    /* Method-specific data */
    int qfd;                /*! fd for epoll, kqueue, devpoll */
#if 0
    int sigpipe[2];
    int sigcnt;
#endif
#if defined(HAVE_POLL)
    struct pollfd *pfd;     /*! Array of pollfd structures (dynalloc) */
    int apfd;               /*! Number of entries in pfd (allocated so far) */
#endif
#if defined(HAVE_SELECT)
    fd_set rfdset;
    fd_set wfdset;
    fd_set xfdset;
#endif
};

static struct ev_ct *ev_default_ct;

ev_time_t ev_now;
time_t ev_time;

static __inline__ struct timeval ev_to_timeval(ev_time_t when)
{
    struct timeval tv;

    tv.tv_sec = when/1000;
    tv.tv_usec = (when%1000)*1000;
    return tv;
}

static __inline__ ev_time_t ev_from_timeval(struct timeval *tv)
{
    return (ev_time_t) tv->tv_sec*1000LL + tv->tv_usec/1000;
}

static __inline__ struct timespec ev_to_timespec(ev_time_t when)
{
    struct timespec ts;

    ts.tv_sec = when/1000;
    ts.tv_nsec = (when%1000)*1000000;
    return ts;
}

static __inline__ ev_time_t ev_from_timespec(struct timespec *ts)
{
    return (ev_time_t) ts->tv_sec*1000LL + ts->tv_nsec/1000000;
}

#if defined(HAVE_EPOLL)

static int evio_epoll_init(struct ev_ct *ct, int maxfd)
{
    int epfd = epoll_create(maxfd);
    
    assert(EPOLLIN == EV_IN  &&  EPOLLOUT == EV_OUT  &&  EPOLLPRI == EV_PRI);
    if (epfd < 0)
        return -1;
    ct->qfd = epfd;
    return 0;
}

static int evio_epoll_ctl(struct ev_ct *ct, int func, int fd, int events)
{
    struct epoll_event ev;
  
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl(ct->qfd, func, fd, &ev);
}

static int evio_epoll_add(struct ev_ct *ct, int fd, int events)
{
    return evio_epoll_ctl(ct, EPOLL_CTL_ADD, fd, events);
}

static int evio_epoll_mod(struct ev_ct *ct, int fd, int events)
{
    return evio_epoll_ctl(ct, EPOLL_CTL_MOD, fd, events);
}

static int evio_epoll_del(struct ev_ct *ct, int fd)
{
    return evio_epoll_ctl(ct, EPOLL_CTL_DEL, fd, 0);
}

static int evio_epoll_wait(struct ev_ct *ct, int timeout, struct ev_fd **efdp)
{
#define EPOLL_CHUNK 200
    struct epoll_event epev[EPOLL_CHUNK];
    struct epoll_event *ep;
    struct epoll_event *epe;
    int ready;

    /* Wait for events */
    ready = epoll_wait(ct->qfd, epev, EPOLL_CHUNK, (timeout < 0)  ?  -1  :  timeout);
    if (ready > 0)
    {
        /* Initialize list of ready fds */
        ep = epev;
        epe = epev + ready;
        do
        {
            struct ev_fd *efd;
            
            efd = ct->efd + ep->data.fd;
            assert(ep->data.fd >= 0  &&  ep->data.fd <= ct->maxfd);
            assert(efd->cbck != NULL);
            assert(efd->next == NULL);
            assert(efd->revents == 0);
            efd->revents = ep->events;
            *efdp = efd;
            efdp = &efd->next;
        }
        while (++ep < epe);
    }
    *efdp = NULL;
    return ready;
}

#endif

#if defined(HAVE_DEVPOLL)

static int evio_devpoll_init(struct ev_ct *ct, int maxfd)
{
    int dpfd;
    
    if ((dpfd = open("/dev/poll", O_RDWR)) < 0)
        return -1;
    ct->qfd = dpfd;
    maxfd = maxfd;
    return 0;
}

static int evio_devpoll_mod(struct ev_ct *ct, int fd, int events)
{
    struct pollfd pfd;
    
    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;
    return (write(ct->qfd, &pfd, sizeof(pfd)) < 0)  ?  -1  :  0;
}

static int evio_devpoll_add(struct ev_ct *ct, int fd, int events)
{
    assert(POLLIN == EV_IN  &&  POLLOUT == EV_OUT  &&  POLLPRI == EV_PRI);
    return evio_devpoll_mod(ct, fd, events);
}

static int evio_devpoll_del(struct ev_ct *ct, int fd)
{
    return evio_devpoll_mod(ct, fd, POLLREMOVE);
}

static int evio_devpoll_wait(struct ev_ct *ct, int timeout, struct ev_fd **efdp)
{
#define DEVPOLL_CHUNK 100
    struct pollfd pfda[DEVPOLL_CHUNK];
    struct pollfd *pfd;
    struct pollfd *pfde;
    dvpoll_t dvp;
    int ready;

    /* Wait for events */
    dvp.dp_timeout = timeout;
    dvp.dp_nfds = DEVPOLL_CHUNK;
    dvp.dp_fds = pfd = pfda;
    ready = ioctl(ct->qfd, DP_POLL, &dvp);
    if (ready > 0)
    {
        /* initialize list of ready fds */
        for (pfde = pfd + ready;  pfd < pfde;  ++pfd)
        {
            struct ev_fd *efd;

            efd = ct->efd + pfd->fd;
            assert(pfd->fd >= 0  &&  pfd->fd <= ct->maxfd);
            assert(efd->cbck != NULL);
            assert(efd->revents == 0);
            efd->revents = pfd->revents;
            *efdp = efd;
            efdp = &efd->next;
        }
    }
    *efdp = NULL;
    return ready;
}

#endif

#if defined(HAVE_KQUEUE)

static int evio_kqueue_init(struct ev_ct *ct, int maxfd)
{
    int kqfd = kqueue();
  
    if (kqfd < 0)
        return -1;
    ct->qfd = kqfd;
    maxfd = maxfd;
    return 0;
}

static int evio_kqueue_mod(struct ev_ct *ct, int fd, int events)
{
    struct kevent kev;
    struct ev_fd *efd;
    static struct timespec zero_ts;
  
    efd = ct->efd + fd;
    if ((efd->events & EV_IN)  &&  !(events & EV_IN))
    {
        EV_SET(&kev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        if (kevent(ct->qfd, &kev, 1, 0, 0, &zero_ts))
            return -1;
        efd->events &= ~EV_IN;
    }
    else if (!(efd->events & EV_IN)  &&  (events & EV_IN))
    {
        EV_SET(&kev, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
        if (kevent(ct->qfd, &kev, 1, 0, 0, &zero_ts) < 0)
            return -1;
        efd->events |= EV_IN;
    }
    if ((efd->events & EV_OUT)  &&  !(events & EV_OUT))
    {
        EV_SET(&kev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        if (kevent(ct->qfd, &kev, 1, 0, 0, &zero_ts))
            return -1;
        efd->events &= ~EV_OUT;
    }
    else if (!(efd->events & EV_OUT)  &&  (events & EV_OUT))
    {
        EV_SET(&kev, fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
        if (kevent(ct->qfd, &kev, 1, 0, 0, &zero_ts) < 0)
            return -1;
        efd->events |= EV_OUT;
    }
    return 0;
}

static int evio_kqueue_add(struct ev_ct *ct, int fd, int events)
{
    return evio_kqueue_mod(ct, fd, events);
}

static int evio_kqueue_del(struct ev_ct *ct, int fd)
{
    return evio_kqueue_mod(ct, fd, 0);
}

static int evio_kqueue_wait(struct ev_ct *ct, int timeout, struct ev_fd **efdp)
{
#define KQUEUE_CHUNK 200
    struct kevent keva[KQUEUE_CHUNK];
    struct kevent *kev;
    struct kevent *keve;
    int ready;
    struct timespec ts;
    struct timespec *tsp;

    /* Wait for events */
    if (timeout < 0)
    {
        tsp = NULL;
    }
    else
    {
        ts = ev_to_timespec(timeout);
        tsp = &ts;
    }
    kev = keva;
    if ((ready = kevent(ct->qfd, 0, 0, kev, KQUEUE_CHUNK, tsp)) > 0)
    {
        /* Initialize list of ready fds */
        for (keve = kev + ready;  kev < keve;  ++kev)
        {
            short int ev;
            struct ev_fd *efd;

            efd = ct->efd + kev->ident;
            assert(kev->ident <= (unsigned int) ct->maxfd);
            assert(efd->cbck != NULL);
            if (kev->filter == EVFILT_READ)
                ev = EV_IN;
            else if (kev->filter == EVFILT_WRITE)
                ev = EV_OUT;
            else
                continue;
            if (!efd->revents)
            {
                *efdp = efd;
                efdp = &efd->next;
            }
            efd->revents |= ev;
        }
    }
    *efdp = NULL;
    return ready;
}

#endif

#if defined(HAVE_POLL)

static int evio_poll_init(struct ev_ct *ct, int maxfd)
{
    struct pollfd *pfd;

    assert(POLLIN == EV_IN  &&  POLLOUT == EV_OUT  &&  POLLPRI == EV_PRI);
    maxfd = ARR_ROUND(maxfd);
    if ((pfd = (struct pollfd *) malloc(maxfd*sizeof(struct pollfd))) == NULL)
    {
        errno = ENOMEM;
        return -1;
    }
    ct->pfd = pfd;
    ct->apfd = maxfd;
    return 0;
}

static int evio_poll_add(struct ev_ct *ct, int fd, int events)
{
    struct pollfd *pfd;
    int pfdi;
    int apfd;
    struct pollfd *pfdp;
    struct pollfd *pfde;

    pfdi = ct->nfd;
    if (pfdi >= ct->apfd)
    {
        apfd = ARR_ROUND(pfdi + 1);
        if ((pfd = (struct pollfd *) realloc(ct->pfd, sizeof(struct pollfd)*apfd)) == NULL)
        {
            errno = ENOMEM;
            return -1;
        }
        for (pfdp = pfd + pfdi, pfde = pfd + apfd;  pfdp < pfde;  ++pfdp)
            pfdp->fd = -1;
        ct->pfd = pfd;
        ct->apfd = apfd;
    }
    pfd = ct->pfd + pfdi;
    assert(pfd->events == 0);
    pfd->fd = fd;
    pfd->events = events;
    pfd->revents = 0;
    assert(ct->efd[fd].pfdi < 0);
    ct->efd[fd].pfdi = pfdi;
    return 0;
}

static int evio_poll_mod(struct ev_ct *ct, int fd, int events)
{
    struct ev_fd *efd;
    int pfdi;

    efd = ct->efd + fd;
    pfdi = efd->pfdi;
    assert(pfdi >= 0  &&  pfdi <= ct->nfd);
    assert(ct->pfd[pfdi].fd == fd);
    ct->pfd[pfdi].events = events;
    return 0;
}

static int evio_poll_del(struct ev_ct *ct, int fd)
{
    struct ev_fd *efd;
    int pfdi;
    int lastfd;

    efd = ct->efd + fd;
    pfdi = efd->pfdi;
    assert(pfdi >= 0  &&  pfdi <= ct->nfd);
    assert(ct->pfd[pfdi].fd == fd);
    ct->pfd[pfdi].fd = -1;
    efd->pfdi = -1;
    lastfd = ct->nfd - 1;
    if (lastfd != pfdi)
    {
        /* move last pfd to pfdi'th position */
        ct->pfd[pfdi] = ct->pfd[lastfd];
        ct->efd[ct->pfd[pfdi].fd].pfdi = pfdi;
    }
    ct->pfd[lastfd].fd = -1;
    efd->pfdi = -1;
    return 0;
}

static int evio_poll_wait(struct ev_ct *ct, int timeout, struct ev_fd **efdp)
{
    struct pollfd *pfd;
    struct pollfd *pfde;
    struct ev_fd *efd;
    int ready;
    int cnt;

    /* Wait for events */
    pfd = ct->pfd;
    if ((ready = poll(pfd, ct->nfd, timeout)) > 0)
    {
        /* Initialize list of ready fds */
        for (pfde = pfd + ct->nfd, cnt = ready;  pfd < pfde;  ++pfd)
        {
            assert(pfd->fd >= 0  &&  pfd->fd <= ct->maxfd);
            efd = ct->efd + pfd->fd;
            assert(efd->pfdi == pfd - ct->pfd);
            assert(efd->cbck != NULL);
            if (pfd->revents)
            {
                efd->revents = (pfd->revents & POLLERR)  ?  (EV_IN | EV_OUT | EV_PRI)  :  pfd->revents;
                *efdp = efd;
                efdp = &efd->next;
                if (--cnt)
                    break;
            }
        }
    }

    *efdp = NULL;
    return ready;
}

#endif

#if defined(HAVE_SELECT)

static int evio_select_init(struct ev_ct *ct, int maxfd)
{
    FD_ZERO(&ct->rfdset);
    FD_ZERO(&ct->wfdset);
    FD_ZERO(&ct->xfdset);
    maxfd = maxfd;
    return 0;
}

static int evio_select_add(struct ev_ct *ct, int fd, int events)
{
    assert(fd < FD_SETSIZE);
    if (events & EV_IN)
        FD_SET(fd, &ct->rfdset);
    if (events & EV_OUT)
        FD_SET(fd, &ct->wfdset);
    if (events & EV_PRI)
        FD_SET(fd, &ct->xfdset);
    return 0;
}

static int evio_select_mod(struct ev_ct *ct, int fd, int events)
{
    if (events & EV_IN)
        FD_SET(fd, &ct->rfdset);
    else
        FD_CLR(fd, &ct->rfdset);
    if (events & EV_OUT)
        FD_SET(fd, &ct->wfdset);
    else
        FD_CLR(fd, &ct->wfdset);
    if (events & EV_PRI)
        FD_SET(fd, &ct->xfdset);
    else
        FD_CLR(fd, &ct->xfdset);
    return 0;
}

static int evio_select_del(struct ev_ct *ct, int fd)
{
    FD_CLR(fd, &ct->rfdset);
    FD_CLR(fd, &ct->wfdset);
    FD_CLR(fd, &ct->xfdset);
    return 0;
}

static int evio_select_wait(struct ev_ct *ct, int timeout, struct ev_fd **efdp)
{
    int ready;
    int cur;
    int fd;
    fd_set fdr;
    fd_set fdw;
    fd_set fdx;
    struct timeval tv;
    struct timeval *tvp;
    int revents;
    struct ev_fd *efd;

    fdr = ct->rfdset;
    fdw = ct->wfdset;
    fdx = ct->xfdset;
    if (timeout < 0)
    {
        tvp = NULL;
    }
    else
    {
        tv = ev_to_timeval(timeout);
        tvp = &tv;
    }

    /* Wait for events */
    ready = select(ct->maxfd + 1, &fdr, &fdw, &fdx, tvp);
    if (ready > 0)
    {
        /* Initialize the list of ready fds */
        for (fd = 0, cur = ready;  fd <= ct->maxfd;  ++fd)
        {
            revents = 0;
            efd = ct->efd + fd;
            if (efd->cbck == NULL)
                continue;
            if (FD_ISSET(fd, &fdr))
                revents |= EV_IN;
            if (FD_ISSET(fd, &fdw))
                revents |= EV_OUT;
            if (FD_ISSET(fd, &fdx))
                revents |= EV_PRI;
            if (!revents)
                continue;
            assert(!efd->next);
            assert(!efd->revents);
            efd->revents = revents;
            *efdp = efd;
            efdp = &efd->next;
            if (--cur == 0)
                break;
        }
    }

    *efdp = NULL;
    return ready;
}

#endif

ev_time_t ev_gettime(void)
{
#ifdef HAVE_CLOCK_GETTIME
	struct timespec	ts;

#ifdef HAVE_CLOCK_MONOTONIC      
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
#else
	if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
#endif
		return -1;
    ev_now = ev_from_timespec(&ts);
    return ev_now;
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);
    ev_time = tv.tv_sec;
    ev_now = ev_from_timeval(&tv);
    return ev_now;
#endif
}

int ev_fdlimit(void)
{
    struct rlimit rlim;

    getrlimit(RLIMIT_NOFILE, &rlim);
    return rlim.rlim_cur;
}

struct ev_method_s
{
    const char *name;
    int type;
    int (*init)(struct ev_ct *ct, int maxfd);
    int (*wait)(struct ev_ct *ct, int tmo, struct ev_fd **pefd);
    int (*add)(struct ev_ct *ct, int fd, int events);
    int (*mod)(struct ev_ct *ct, int fd, int events);
    int (*del)(struct ev_ct *ct, int fd);
    int maxfd;
};

static const struct ev_method_s methods[] =
{
#define EVIO_METHOD(name,type,maxfd) \
    { #name, type, evio_##name##_init, evio_##name##_wait, \
      evio_##name##_add, evio_##name##_mod, evio_##name##_del, maxfd }

#if defined(HAVE_EPOLL)
    EVIO_METHOD(epoll, EV_EPOLL, 0),
#endif
#if defined(HAVE_DEVPOLL)
    EVIO_METHOD(devpoll, EV_DEVPOLL, 0),
#endif
#if defined(HAVE_KQUEUE)
    EVIO_METHOD(kqueue, EV_KQUEUE, 0),
#endif
#if defined(HAVE_POLL)
    EVIO_METHOD(poll, EV_POLL, 0),
#endif
#if defined(HAVE_SELECT)
    EVIO_METHOD(select, EV_SELECT, FD_SETSIZE - 1)
#endif
};

struct ev_ct *ev_ct_new(int maxfdhint, int type)
{
    unsigned int i;
    struct ev_ct *ct;
    const struct ev_method_s *em;
    int maxfd;
    const char *method;

    ev_gettime();
    if ((ct = (struct ev_ct *) malloc(sizeof(struct ev_ct))) == NULL)
    {
        errno = ENOMEM;
        return (struct ev_ct *) NULL;
    }
    ct->qfd = -1;
    if (maxfdhint <= 0)
        maxfdhint = ev_fdlimit();
    maxfdhint = ARR_ROUND(maxfdhint);
    method = getenv("EV_METHOD");
    if (method  ||  !type)
        type = 0xFFFF;
#if defined(ENOTSUP)
    errno = ENOTSUP;
#else
    errno = ENOENT;
#endif
    i = 0;
    for (;;)
    {
        em = &methods[i];
        if ((em->type & type)  &&  (!method || strcmp(method, em->name) == 0))
        {
            maxfd = (em->maxfd  &&  em->maxfd < maxfdhint)  ?  em->maxfd  :  maxfdhint;
            if (em->init(ct, maxfd) == 0)
                break;
        }
        if (++i >= sizeof(methods)/sizeof(methods[0]))
        {
            free(ct);
            return NULL;
        }
    }
    if ((ct->efd = (struct ev_fd *) malloc(maxfd*sizeof(struct ev_fd))) == NULL)
    {
        if (ct->qfd >= 0)
            close(ct->qfd);
#if defined(HAVE_POLL)
        if (ct->pfd)
            free(ct->pfd);
#endif
        free(ct);
        errno = ENOMEM;
        return NULL;
    }
#if defined(HAVE_POLL)
    {
        struct ev_fd *efd;
        struct ev_fd *efde;

        for (efd = ct->efd, efde = efd + maxfd;  efd < efde;  ++efd)
            efd->pfdi = -1;
    }
#endif
    ct->aefd = maxfd;
    ct->method = em;
    ct->maxfd = -1;
    return ct;
}

void ev_ct_free(struct ev_ct *ct)
{
    if (ct == NULL)
        ct = ev_default_ct;
    if (ct->qfd >= 0)
        close(ct->qfd);
#if defined(HAVE_POLL)
    if (ct->pfd)
        free(ct->pfd);
#endif
    free(ct->efd);
    free(ct);
    if (ct == ev_default_ct)
        ev_default_ct = NULL;
}

const char *ev_method_name(const struct ev_ct *ct)
{
    if (ct == NULL)
        ct = ev_default_ct;
    return ct->method->name;
}

int ev_method(const struct ev_ct *ct)
{
    if (ct == NULL)
        ct = ev_default_ct;
    return ct->method->type;
}

int ev_init(int maxfdhint, int type)
{
    if (ev_default_ct == NULL  &&  (ev_default_ct = ev_ct_new(maxfdhint, type)) == NULL)
        return -1;
    return 0;
}

void ev_free(void)
{
    if (ev_default_ct)
        ev_ct_free(ev_default_ct);
}

/* Register an FD */
int ev_io_add(struct ev_ct *ct, int fd, int events, ev_io_cbck_f *cb, void *user_data)
{
    int r;
    struct ev_fd *efd;

    if (ct == NULL)
        ct = ev_default_ct;
    if (fd < 0)
    {
        errno = EINVAL;
        return -1;
    }
    if (cb == NULL)
    {
        errno = EFAULT;
        return -1;
    }
    if (ct->method->maxfd  &&  fd > ct->method->maxfd)
    {
        errno = EMFILE;
        return -1;
    }
    if (fd >= ct->aefd)
    {
        r = ARR_ROUND(fd);
        if ((efd = (struct ev_fd *) realloc(ct->efd, sizeof(struct ev_fd)*r)) == NULL)
        {
            errno = ENOMEM;
            return -1;
        }
        memset(efd, 0, sizeof(struct ev_fd)*(r - ct->aefd));
#if defined(HAVE_POLL)
        {
            struct ev_fd *efdp;
            struct ev_fd *efde;

            for (efdp = efd + ct->aefd, efde = efd + r;  efdp < efde;  ++efdp)
                efdp->pfdi = -1;
        }
#endif
        ct->efd = efd;
        ct->aefd = r;
    }
    efd = ct->efd + fd;
    if (efd->cbck)
    {
        errno = EEXIST;
        return -1;
    }
    if ((r = ct->method->add(ct, fd, events)) != 0)
        return r;
    efd->cbck = cb;
    efd->user_data = user_data;
    if (ct->maxfd < fd)
        ct->maxfd = fd;
    ++ct->nfd;
    return 0;
}

/* Modify parameters for an existing FD */
int ev_io_mod(struct ev_ct *ct, int fd, int events, ev_io_cbck_f *cb, void *user_data)
{
    int r;
    struct ev_fd *efd;
    
    if (ct == NULL)
        ct = ev_default_ct;
    if (fd < 0)
    {
        errno = EINVAL;
        return -1;
    }
    if (fd > ct->maxfd)
    {
        errno = ENOENT;
        return -1;
    }
    efd = ct->efd + fd;
    if (efd->cbck == NULL)
    {
        errno = ENOENT;
        return -1;
    }
    if (cb == NULL)
    {
        errno = EFAULT;
        return -1;
    }
    if ((r = ct->method->mod(ct, fd, events)))
        return r;
    efd->cbck = cb;
    efd->user_data = user_data;
    efd->events = events;
    efd->revents = 0;
    return 0;
}

/* Deregister an FD */
int ev_io_del(struct ev_ct *ct, int fd)
{
    struct ev_fd *efd;
    
    if (ct == NULL)
        ct = ev_default_ct;
    if (fd < 0)
    {
        errno = EINVAL;
        return -1;
    }
    if (fd > ct->maxfd)
    {
        errno = ENOENT;
        return -1;
    }
    efd = ct->efd + fd;
    if (efd->cbck == NULL)
    {
        errno = ENOENT;
        return -1;
    }
    ct->method->del(ct, fd);
    efd->cbck = NULL;
    efd->user_data = NULL;
    efd->events =
    efd->revents = 0;
  
    if (ct->maxfd == fd)
    {
        do
            --fd;
        while (fd >= 0  &&  ct->efd[fd].cbck == NULL);
        ct->maxfd = fd;
    }
    --ct->nfd;
    return 0;
}

int ev_io_count(const struct ev_ct *ct)
{
    if (ct == NULL)
        ct = ev_default_ct;
    return ct->nfd;
}

/* create new timer to be fired at the time specified by `when',
 * and insert it into the list appropriately.
 */
struct ev_tm *ev_timer_add(struct ev_ct *ct,
                           struct ev_tm *tmr,
                           ev_time_t when,
                           ev_tm_cbck_f *cbck,
                           void *user_data)
{
    if (cbck == NULL)
    {
        errno = EFAULT;
        return (struct ev_tm *) NULL;
    }
    if (ct == NULL)
        ct = ev_default_ct;
    if (tmr)
    {
        /* There is a timer to reuse */
        assert(!tmr->evtm_prev  &&  !tmr->evtm_next  &&  !tmr->evtm_when);
    }
    else
    {
        /* We need a new timer */
        if ((tmr = (struct ev_tm *) malloc(sizeof(struct ev_tm))) == NULL)
        {
            errno = ENOMEM;
            return (struct ev_tm *) NULL;
        }
    }
    tmr->evtm_when = when;
    tmr->evtm_cbck = cbck;
    tmr->evtm_data = user_data;

    if (ct->tmhead == NULL)
    {
        /* No other timers are registered. Just create empty list */
        assert(!ct->tmtail  &&  !ct->tmcnt  &&  !ct->tmsum);
        ct->tmhead =
        ct->tmtail = tmr;
        tmr->evtm_next =
        tmr->evtm_prev = NULL;
    }
    else if (when >= ct->tmtail->evtm_when)
    {
        /* Add after the tail */
        ct->tmtail->evtm_next = tmr;
        tmr->evtm_prev = ct->tmtail;
        ct->tmtail = tmr;
        tmr->evtm_next = NULL;
    }
    else if (when < ct->tmhead->evtm_when)
    {
        /* Add before the head */
        ct->tmhead->evtm_prev = tmr;
        tmr->evtm_next = ct->tmhead;
        ct->tmhead = tmr;
        tmr->evtm_prev = NULL;
    }
    else if (ct->tmsum <= when*ct->tmcnt)
    {
        /* Add in the middle, in order, scanning from the tail */
        struct ev_tm *prev = ct->tmtail;
        
        while (prev->evtm_when > when)
        {
            assert(prev->evtm_prev  &&  prev->evtm_when >= prev->evtm_prev->evtm_when);
            prev = prev->evtm_prev;
        }
        tmr->evtm_prev = prev;
        tmr->evtm_next = prev->evtm_next;
        prev->evtm_next->evtm_prev = tmr;
        prev->evtm_next = tmr;
    }
    else
    {
        /* Add in the middle, in order, scanning from the head */
        struct ev_tm *next = ct->tmhead;
        
        while (next->evtm_when <= when)
        {
            assert(next->evtm_next  &&  next->evtm_when <= next->evtm_next->evtm_when);
            next = next->evtm_next;
        }
        tmr->evtm_next = next;
        tmr->evtm_prev = next->evtm_prev;
        next->evtm_prev->evtm_next = tmr;
        next->evtm_prev = tmr;
    }
    ct->tmcnt += 1;
    ct->tmsum += when;
    return tmr;
}

struct ev_tm *ev_ms_timer_add(struct ev_ct *ct,
                              struct ev_tm *tmr,
                              int ms_timeout,
                              ev_tm_cbck_f *cbck,
                              void *user_data)
{
    if (ms_timeout < 0)
    {
        errno = EINVAL;
        return (struct ev_tm *) NULL;
    }
    return ev_timer_add(ct, tmr, ev_now + ms_timeout, cbck, user_data);
}

struct ev_tm *ev_sec_timer_add(struct ev_ct *ct,
                               struct ev_tm *tmr,
                               int s_timeout,
                               ev_tm_cbck_f *cbck,
                               void *user_data)
{
    if (s_timeout < 0)
    {
        errno = EINVAL;
        return (struct ev_tm *) NULL;
    }
    return ev_timer_add(ct, tmr, ((ev_now + 500) / 1000 + s_timeout)*1000, cbck, user_data);
}

int ev_timer_del(struct ev_ct *ct, struct ev_tm *tmr)
{
    int ms_timeout;

    if (ct == NULL)
        ct = ev_default_ct;
    if (tmr->evtm_when == 0)
    {
        assert(tmr->evtm_prev == NULL  &&  tmr->evtm_next == NULL);
        errno = ENOENT;
        return -1;
    }
    assert(tmr->evtm_prev != NULL  ||  ct->tmhead == tmr);
    assert(tmr->evtm_next != NULL  ||  ct->tmtail == tmr);
    if (tmr->evtm_prev)
        tmr->evtm_prev->evtm_next = tmr->evtm_next;
    else
        ct->tmhead = tmr->evtm_next;
    if (tmr->evtm_next)
        tmr->evtm_next->evtm_prev = tmr->evtm_prev;
    else
        ct->tmtail = tmr->evtm_prev;
    ct->tmcnt--;
    ct->tmsum -= tmr->evtm_when;
    ms_timeout = (tmr->evtm_when < ev_now)  ?  0  :  tmr->evtm_when - ev_now;
    tmr->evtm_prev =
    tmr->evtm_next = NULL;
    tmr->evtm_when = 0;
    return ms_timeout;
}

/* return the time when first timer will be fired */
ev_time_t ev_timer_first(const struct ev_ct *ct)
{
    if (ct == NULL)
        ct = ev_default_ct;
    return (ct->tmhead)  ?  ct->tmhead->evtm_when  :  0;
}

/* Return the time from now to the first timer to be fired
 * or -1 if no timer is set */
int ev_timer_timeout(const struct ev_ct *ct)
{
    ev_time_t timeout;
    
    if (ct == NULL)
        ct = ev_default_ct;
    if (ct->tmhead == NULL)
        return -1;
    timeout = ct->tmhead->evtm_when - ev_now;
    if (timeout < 0)
        return 0;
    return timeout;
}

int ev_timer_count(const struct ev_ct *ct)
{
    if (ct == NULL)
        ct = ev_default_ct;
    return ct->tmcnt;
}

/* Wait and dispatch any events, single */
int ev_wait(struct ev_ct *ct, int timeout)
{
    struct ev_tm *tmr;
    struct ev_fd *efdl;
    struct ev_fd *efd;
    int revents;
    int r;
    int saved_errno;
    
    if (ct == NULL)
        ct = ev_default_ct;
    /* Lock the wait operation */
    if (++ct->loop != 1)
    {
        ct->loop--;
        errno = EAGAIN;
        return -1;
    }
    if (timeout  &&  ct->tmhead)
    {
        r = ct->tmhead->evtm_when - ev_now;
        if (r < 0)
            timeout = 0;
        else if (timeout < 0  ||  r < timeout)
            timeout = r;
    }
    saved_errno = 0;
    if ((r = ct->method->wait(ct, timeout, &efdl)) < 0)
        saved_errno = errno;
    ev_gettime();
    while ((tmr = ct->tmhead)  &&  tmr->evtm_when <= ev_now)
    {
        if ((ct->tmhead = tmr->evtm_next))
            tmr->evtm_next->evtm_prev = NULL;
        else
            ct->tmtail = NULL;
        ct->tmcnt--;
        ct->tmsum -= tmr->evtm_when;
        tmr->evtm_prev =
        tmr->evtm_next = NULL;
        tmr->evtm_when = 0;
        tmr->evtm_cbck(ct, tmr->evtm_data, tmr);
    }
    while (efdl)
    {
        efd = efdl;
        efdl = efd->next;
        revents = efd->revents;
        efd->revents = 0;
        efd->next = NULL;
        if (revents)
            efd->cbck(ct, efd->user_data, revents, efd - ct->efd);
    }
    /* Release the lock */
    ct->loop = 0;
    if (r < 0)
        errno = saved_errno;
    return r;
}

#if TEST

#include <stdio.h>

int delay = 3000;

static void pc(struct ev_ct *ct, void *user_data, struct ev_tm *tmr)
{
    char *t = (char *) user_data;

    write(1, t, 1);
    if (*t >= 'a' && *t <= 'z')
        *t = *t - 'a' + 'A';
    else if (*t >= 'A' && *t <= 'Z')
        *t = *t - 'A' + 'a';
    ev_ms_timer_add(ct, tmr, delay, pc, user_data);
}

static void rt(struct ev_ct *ct, void *user_data, int revents, int fd)
{
    char buf[10];
    int l;
    
    if ((l = read(fd, buf, sizeof(buf))) <= 0)
    {
        ev_ct_free(ct);
        exit((l < 0)  ?  1  :  0);
    }
    write(1, "input: ", 7);
    write(1, buf, l);
    ev_io_del(NULL, 0);
    ev_io_add(NULL, 0, EV_IN, rt, 0);
}

int main(int argc, char *argv[])
{
    static char text[] = "\rdingDONG";
    char *p;

    if (ev_init(1, 0))
    {
        perror("ev_init");
        return 1;
    }
    printf("evio method: %s\n", ev_method_name(0));

    delay = 3000;
    for (p = text;  *p;  p++)
    {
        ev_ms_timer_add(NULL, NULL, delay, pc, p);
        delay += 1000;
    }
    delay = 12000;
    ev_io_add(0, 0, EV_IN, rt, 0);
    for (;;)
        ev_wait(0, -1);
    return 1;
}

#endif /* TEST */
