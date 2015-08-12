#include <signals-lib/signals.hxx>


namespace signalslib
{

std::mutex PosixHandler::mtx;
std::vector<PosixHandler *> PosixHandler::handlers (get_sigmax ());
std::vector<struct sigaction> PosixHandler::old_sigactions (get_sigmax ());

} // namespace signalslib
