// Rewrites the file named on its command line with one of two contents, over and over, until it is
// killed: the "killed at any moment" case of AtomicFileTests.cpp.

#include <gs/platform/AtomicFile.h>

#include <cstdint>
#include <filesystem>
#include <string>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) {
        return 2;
    }
    const std::filesystem::path target = argv[1];
    const std::string first(256 * 1024, 'a');
    const std::string second(192 * 1024, 'b');
    for (std::uint64_t i = 0;; ++i) {
        if (gs::platform::writeFileAtomically(target, i % 2 == 0 ? first : second)) {
            return 1;
        }
    }
}
