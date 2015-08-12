#include <signals-lib/signals.hxx>
#include <iostream>


int
main ()
{
    std::cout << "signals test\n";
    sigset_t sig_set;
    sigemptyset (&sig_set);
    sigaddset (&sig_set, SIGINT);
    auto func = [](int signo, siginfo_t const & si, ucontext_t const &) -> void {
        std::cout << "got signal " << signo << "\n";
    };
    signalslib::PosixHandler s (sig_set, func);
}
