#include <signals-lib/signals.hxx>


namespace signalslib
{

std::vector<std::atomic<PosixHandler *> > PosixHandler::handlers (
    get_sigmax ());

} // namespace signalslib
