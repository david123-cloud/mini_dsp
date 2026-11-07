#ifndef MINI_DSP_ERROR_H
#define MINI_DSP_ERROR_H

/*
 * Shared status codes.
 *
 * Every fallible function in mini_dsp returns DSP_OK (0) on success and a
 * negative code otherwise.  A failure is never reported as "0 samples
 * processed", so a silent pass-through can never hide a broken stage.
 */
#define DSP_OK 0
#define DSP_ERR_INVALID_ARG (-1) /* NULL pointer, bad size, bad configuration */
#define DSP_ERR_UNSUPPORTED (-2) /* legal request, not implemented yet */
#define DSP_ERR_NO_MEMORY (-3)   /* allocation failed */
#define DSP_ERR_IO (-4)          /* open/read/write/close failed */
#define DSP_ERR_FORMAT (-5)      /* unsupported or malformed file layout */
#define DSP_ERR_STATE (-6)       /* object not initialised / already owned */
#define DSP_ERR_RANGE (-7)       /* value outside the supported range */

/* Human readable name for a status code; never returns NULL. */
const char *dsp_strerror(int status);

#endif /* MINI_DSP_ERROR_H */
