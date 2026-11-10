#include <Zamin/Core/Log.h>
#include <Zamin/Utils/FileSystem.h>
#include <unistd.h>

// A separate process allows a root-run CI to exercise actual permission failures safely.
int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    Zamin::Log::SetMinLevel(Zamin::LogLevel::Error);
    if (::geteuid() == 0 && (::setgid(65534) != 0 || ::setuid(65534) != 0)) return 3;
    return Zamin::FileSystem::WriteTextFileAtomic(argv[1], "replacement") ? 1 : 0;
}
