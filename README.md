# signals-lib

This library provides means to "convert" asynchronous POSIX signals to
synchronous callbacks to given `std::function` callback in a worker thread.

~~~~{.cpp}
#include <signals-lib/signals.hxx>
#include <iostream>

int
main ()
{
    std::cout << "signals test\n";
    auto func = [](signalslib::signal_info const & si) {
        std::cout << "got signal " << si.signo << "\n";
    };
    {
        sigset_t sig_set;
        sigemptyset (&sig_set);
        sigaddset (&sig_set, SIGUSR1);

        signalslib::PosixHandler s (sig_set, func);

        signalslib::scoped_signals_blocker blocker (
            signalslib::get_reasonable_blocking_sigset_t ());
        std::thread signaler ([] ()
            {
                for (int i = 0; i != 10; ++i)
                {
                    std::cout << "raising signal\n";
                    kill (getpid(), SIGUSR1);
                    std::this_thread::sleep_for (
                        std::chrono::milliseconds (500));
                }
            });
        signaler.join ();
        std::cout << "signaler thread joined\n";
    }

    std::cout << "exiting main()\n";
}
~~~~

## `signalslib::PosixHandler`

The `signalslib::PosixHandler` is portable to all POSIX systems. It uses the
"self pipe trick." It is necessary to link your application with
`src/signals.cxx`.

## `signalslib::SignalFDHandler`

On Linux, it is possible to use `signalslib::SignalFDHandler` which uses
`signalfd()` internally to get notification of signals. This handler is
entirely contained in the `signals-lib/signals.hxx` header.
