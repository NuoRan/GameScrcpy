#include "Server.h"

int wmain(int argc, wchar_t** argv)
{
    return gs::scripthost::serve(argc, argv);
}
