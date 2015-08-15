#include <signals-lib/signals.hxx>
#include <iostream>


int
main ()
{
    std::cout << "signals test\n";
    auto func = [](int signo, siginfo_t const & si, ucontext_t const &) {
        std::cout << "got signal " << signo << "\n";
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
