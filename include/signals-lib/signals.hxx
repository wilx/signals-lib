#if ! defined (SIGNALS_LIB_SIGNALS_HXX)
#define SIGNALS_LIB_SIGNALS_HXX

#include <versions-lib/versions.hxx>

#if VERSIONS_LIB_LINUX_PREREQ (2, 6, 22)         \
    && VERSIONS_LIB_GLIBC_PREREQ (2, 8)
#define SIGNALS_LIB_HAVE_SIGNALFD
#include <sys/signalfd.h>
#endif

#if VERSIONS_LIB_LINUX_PREREQ (2, 6, 27)         \
    && VERSIONS_LIB_GLIBC_PREREQ (2, 9)
#define SIGNALS_LIB_HAVE_PIPE2
#endif

#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>

#include <thread>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>
#include <chrono>
#include <type_traits>
#include <utility>
#include <cstring>


namespace signalslib
{


struct scoped_signals_blocker
{
    sigset_t old;

    scoped_signals_blocker (sigset_t blocked_signals)
    {
        pthread_sigmask (SIG_BLOCK, &blocked_signals, &old);
    }

    ~scoped_signals_blocker ()
    {
        pthread_sigmask (SIG_SETMASK, &old, nullptr);
    }
};


inline
size_t
get_sigmax ()
{
#if defined (SIGRTMAX)
    return SIGRTMAX;
#else
    return 64;
#endif
}


template <typename Sigset, typename Functor>
typename std::enable_if<
    std::is_same<
        typename std::remove_reference<Sigset>::type,
        sigset_t>::value
    >::type
for_each_signal (Sigset && sigset, Functor func)
{
    int const max = get_sigmax ();
    for (int i = 0; i != max; ++i)
    {
        int const ret = sigismember (&sigset, i);
        if (ret == -1)
            continue;

        func (i, !! ret);
    }
}


inline
sigset_t
get_reasonable_blocking_sigset_t ()
{
    sigset_t blocked_signals;
    sigfillset (&blocked_signals);

    // From pthread_sigmask() page in IEEE Std 1003.1, 2013 Edition:
    //
    // If any of the SIGFPE, SIGILL, SIGSEGV, or SIGBUS signals are generated
    // while they are blocked, the result is undefined, unless the signal was
    // generated by the action of another process, or by one of the functions
    // kill(), pthread_kill(), raise(), or sigqueue().
    //
    // So we unblock these explicitly here.

    sigdelset (&blocked_signals, SIGFPE);
    sigdelset (&blocked_signals, SIGILL);
    sigdelset (&blocked_signals, SIGSEGV);
    sigdelset (&blocked_signals, SIGBUS);

    return blocked_signals;
}






inline
bool
try_set_close_on_exec (int fd)
{
    int ret = -1;
#if defined (FD_CLOEXEC)
    ret = fcntl (fd, F_SETFD, FD_CLOEXEC);
#endif
    return ret != -1;
}


//
//
//

//! This structure gets written into pipe from signal handler to convey all the
//! necessary information to the handling thread.
struct signal_info
{
    int signo;
    siginfo_t siginfo;
    ucontext_t context;
};


//
//
//

enum FDs
{
    SIGNALS_FD = 0,
    SHUTDOWN_FD = 1,
};

enum PipeEnds
{
    READ_END = 0,
    WRITE_END = 1
};


struct Handler
{
    virtual FDs poll_fds () = 0;
    virtual void signal_shutdown () = 0;
    virtual void wait_shutdown () = 0;

    virtual ~Handler () = 0;
};


inline
Handler::~Handler ()
{ }


//
//
//


inline
int
xwrite (int fd, void const * buf, std::size_t size)
{
    int ret;
    do
    {
        ret = write (fd, buf, size);
    }
    while (ret == -1 && errno == EINTR);
    return ret;
}


inline
int
debug_print(char const * str)
{
    return xwrite (1, str, std::strlen (str));
}


inline
int
xread (int fd, void * buf_ptr, std::size_t buf_size)
{
    long read_bytes = 0;
    char * buf = static_cast<char *>(buf_ptr);
    do
    {
        long const res = read (fd, buf + read_bytes, buf_size - read_bytes);
        if (res == -1 && errno == EINTR)
            continue;
        else if (res == -1)
            return res;

        read_bytes += res;
    }
    while (static_cast<std::size_t>(read_bytes) < buf_size);

    return read_bytes;
}


using signal_handler_callback_type =
    std::function<void (int, siginfo_t const &, ucontext_t const &)>;


inline
int
create_pipe (std::array<int, 2> & fds)
{
    int ret;

#if defined (SIGNALS_LIB_HAVE_PIPE2)
    ret = pipe2 (&fds[0], O_CLOEXEC);

#else
    ret = pipe (&fds[0]);
    try_set_close_on_exec (fds[0]);
    try_set_close_on_exec (fds[1]);

#endif

    return ret;
}


//
//
//

class HandlerBase
    : public virtual Handler
{
public:
    HandlerBase (sigset_t const & s, signal_handler_callback_type cb)
        : signals (s)
        , callback (std::move (cb))
    {
        int ret = create_pipe (shutdown_pipe_fds);
        if (ret == -1)
            throw std::runtime_error ("create_pipe");
    }

    HandlerBase () = delete;
    HandlerBase (HandlerBase const &) = delete;
    HandlerBase (HandlerBase &&) = delete;
    HandlerBase & operator = (HandlerBase const &) = delete;
    HandlerBase & operator = (HandlerBase &&) = delete;

    virtual
    ~HandlerBase ()
    {
        close (get_shutdown_fd_write_end ());
        close (get_shutdown_fd_read_end ());
    }

    virtual
    void
    signal_shutdown ()
    {
        char const ch = 'S';
        int ret = xwrite (get_shutdown_fd_write_end (), &ch, 1);
        // TODO: Error handling.
        if (ret == -1)
            std::abort ();
    }


    virtual
    void
    wait_shutdown ()
    {
        if (handler_thread)
            handler_thread->join ();
    }


    int
    get_shutdown_fd_read_end () const
    {
        return shutdown_pipe_fds[READ_END];
    }

    int
    get_shutdown_fd_write_end () const
    {
        return shutdown_pipe_fds[WRITE_END];
    }


protected:
    sigset_t signals;
    std::array<int, 2> shutdown_pipe_fds;
    std::unique_ptr<std::thread> handler_thread;
    signal_handler_callback_type callback;
};


//
//
//

using signal_handler_function_type = void (*) (int, siginfo_t *, void *);


inline
std::tuple<int, struct sigaction>
install_sig_handler(signal_handler_function_type func, int sig)
{
    struct sigaction act{};
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = func;
    act.sa_mask = get_reasonable_blocking_sigset_t ();

    struct sigaction old;
    int ret = sigaction (sig, &act, &old);

    return std::make_tuple (ret, old);
}


inline
int
restore_sig_handler (struct sigaction const & old_act, int sig)
{
    return sigaction (sig, &old_act, nullptr);
}


} // namespace signalslib


extern "C"
inline
void
signalslib_signal_handler_func  (int sig, siginfo_t * siginfo, void * context);


namespace signalslib
{


class PosixHandler
    : public HandlerBase
{
public:
    PosixHandler (sigset_t const & s, signal_handler_callback_type cb)
        : HandlerBase (s, std::move (cb))
    {
        int ret = create_pipe (signals_pipe_fds);
        if (ret == -1)
            throw std::runtime_error ("create_pipe");

        // Block (almost) all signals.

        sigset_t blocked_signals = get_reasonable_blocking_sigset_t ();
        scoped_signals_blocker sig_blocker (blocked_signals);

        // Install signal handlers for signals that interest us.

        for_each_signal (signals, [this](int sig, bool is_set)
            {
                if (! is_set)
                    return;

                set_handler_ptr (sig, this);

                struct sigaction old_act;
                int ret;
                std::tie (ret, old_act)
                    = install_sig_handler (signalslib_signal_handler_func, sig);
                // TODO:: Error handling.
                set_old_sigaction (sig, old_act);
            });

        std::atomic_signal_fence (std::memory_order_acq_rel);
        handler_thread.reset (
            new std::thread (&PosixHandler::thread_function, this));
    }


    void
    handle_one_signal ()
    {
        signal_info si;
        int ret = xread (get_signals_fd_read_end (), &si, sizeof (si));
        if (ret == -1)
            std::abort ();

        callback (si.signo, si.siginfo, si.context);
    }


    void
    thread_function ()
    {
        debug_print ("thread_function\n");

        // Unblock signals that interest us. They can be blocked in all other
        // threads.

        int ret = pthread_sigmask (SIG_UNBLOCK, &signals, nullptr);
        if (ret != 0)
            std::abort ();

        // Poll signals and shutdown file descriptors.

        for (;;)
        {
            // Poll handles here.
            debug_print ("entering poll_fds\n");
            FDs signaled_handle = poll_fds ();
            debug_print ("exited poll_fds\n");
            switch (signaled_handle)
            {
            case SIGNALS_FD:
                debug_print ("got signals FD signaled\n");
                handle_one_signal ();
                break;

            case SHUTDOWN_FD:
                debug_print ("got shutdown FD signaled\n");
                return;

            default:
                throw std::logic_error ("unknown handle kind signaled");
            }
        }
    }


    void
    restore_signal_handlers ()
    {
        std::unique_lock<std::mutex> lock (mtx);

        size_t const max = get_sigmax ();
        for (int i = 0; i != max; ++i)
        {
            int ret = sigismember (&signals, i);
            // TODO: Check ret == -1 here.

            if (! ret)
                continue;

            struct sigaction old_act = get_old_sigaction (i);
            ret = restore_sig_handler (old_act, i);
            set_handler_ptr (i, nullptr);
            // TODO: Error handling.
        }
    }


    virtual
    ~PosixHandler ()
    {
        restore_signal_handlers ();
        signal_shutdown ();
        wait_shutdown ();
        for_each_signal (signals, [](int sig, bool is_set)
            {
                if (is_set)
                    set_handler_ptr (sig,  nullptr);
            });
        std::atomic_signal_fence (std::memory_order_acq_rel);
        close (get_signals_fd_read_end ());
        close (get_signals_fd_write_end ());
    }


    int
    get_signals_fd_read_end () const
    {
        return signals_pipe_fds[READ_END];
    }


    int
    get_signals_fd_write_end () const
    {
        return signals_pipe_fds[WRITE_END];
    }


    virtual
    FDs
    poll_fds ()
    {
        std::array<struct pollfd, 2> pollfds;

        struct pollfd & signals_pollfd = pollfds[SIGNALS_FD];
        signals_pollfd.fd = get_signals_fd_read_end ();
        signals_pollfd.events = POLLIN;
        signals_pollfd.revents = 0;

        struct pollfd & shutdown_pollfd = pollfds[SHUTDOWN_FD];
        shutdown_pollfd.fd = get_shutdown_fd_read_end ();
        shutdown_pollfd.events = POLLIN;
        shutdown_pollfd.revents = 0;

        int ret;
        while (((ret = poll (&pollfds[0], 2, -1)) == -1
                && errno == EINTR)
            || ret == 0)
        {
            debug_print ("looping poll\n");
        }
        if (ret == -1)
            // TODO: Error handling.
            std::abort ();

        if ((shutdown_pollfd.revents & POLLIN) == POLLIN)
            return SHUTDOWN_FD;
        else if ((signals_pollfd.revents & POLLIN) == POLLIN)
            return SIGNALS_FD;

        throw std::logic_error ("unknown handle signaled");
    }



protected:
    friend void ::signalslib_signal_handler_func  (int sig, siginfo_t * siginfo,
        void * context);


    std::array<int, 2> signals_pipe_fds;

    static std::mutex mtx;
    static std::vector<PosixHandler *> handlers;
    static std::vector<struct sigaction> old_sigactions;


    static
    void
    set_handler_ptr (std::size_t slot, PosixHandler * handler)
    {
        if (slot >= PosixHandler::handlers.size ())
            throw std::out_of_range ("");

        handlers[slot] = handler;
    }


    static
    PosixHandler *
    get_handler_ptr (std::size_t slot)
    {
        if (slot >= PosixHandler::handlers.size ())
            throw std::out_of_range ("");

        return handlers[slot];
    }


    static
    struct sigaction
    get_old_sigaction (std::size_t slot)
    {
        if (slot >= PosixHandler::old_sigactions.size ())
            throw std::out_of_range ("");

        return old_sigactions[slot];
    }


    static
    void
    set_old_sigaction (std::size_t slot, struct sigaction const & old_act)
    {
        if (slot >= PosixHandler::old_sigactions.size ())
            throw std::out_of_range ("");

        old_sigactions[slot] = old_act;
    }
};


} // namespace signalslib


extern "C"
inline
void
signalslib_signal_handler_func  (int sig, siginfo_t * siginfo, void * context)
{
    using namespace signalslib;

    debug_print ("signal handler called\n");

    PosixHandler * const handler = PosixHandler::get_handler_ptr (sig);
    int const signals_fd = handler->get_signals_fd_write_end ();
    signal_info const si {sig, siginfo ? *siginfo : siginfo_t (),
            context ? *reinterpret_cast<ucontext_t *>(context) : ucontext_t ()};
    int ret = xwrite (signals_fd, &si, sizeof (si));
    if (ret == -1)
        std::abort ();
    else if (ret < sizeof (si))
        std::abort ();
    // TODO: Error handling. Also check for incomplete write.

    debug_print ("signal handler exiting\n");
}



#endif // SIGNALS_LIB_SIGNALS_HXX
