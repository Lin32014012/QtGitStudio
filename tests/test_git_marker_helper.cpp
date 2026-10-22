#include <iostream>
#include <string>

int main(int argc, char **argv) {
    std::string executable = argc > 0 ? argv[0] : std::string();
    const std::size_t forwardSlash = executable.find_last_of('/');
    const std::size_t backSlash = executable.find_last_of(static_cast<char>(92));
    const std::size_t slash = forwardSlash == std::string::npos
        ? backSlash
        : (backSlash == std::string::npos
               ? forwardSlash
               : (forwardSlash > backSlash ? forwardSlash : backSlash));
    if (slash != std::string::npos) {
        executable = executable.substr(slash + 1);
    }
    std::cout << "git version 9.9.9 marker=" << executable << std::endl;
    return 0;
}
