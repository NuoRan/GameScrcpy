#include <gs/exthost/Server.h>

int wmain(int argc, wchar_t** argv)
{
    return gs::exthost::serve(argc, argv);
}
