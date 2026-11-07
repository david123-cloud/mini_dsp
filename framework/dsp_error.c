#include "dsp_error.h"

const char *dsp_strerror(int status)
{
    switch (status) {
    case DSP_OK:
        return "ok";
    case DSP_ERR_INVALID_ARG:
        return "invalid argument";
    case DSP_ERR_UNSUPPORTED:
        return "unsupported operation";
    case DSP_ERR_NO_MEMORY:
        return "out of memory";
    case DSP_ERR_IO:
        return "io error";
    case DSP_ERR_FORMAT:
        return "unsupported or malformed file format";
    case DSP_ERR_STATE:
        return "object state error";
    case DSP_ERR_RANGE:
        return "value out of range";
    default:
        return "unknown error";
    }
}
