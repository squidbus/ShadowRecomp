#include <os/media.h>

bool os::media::IsSupported()
{
    // This functionality is not supported in macOS.
    return false;
}

bool os::media::IsExternalMediaPlaying()
{
    // This functionality is not supported in macOS.
    return false;
}
