#ifndef ADP_SETUP_SAMPLE_H
#define ADP_SETUP_SAMPLE_H

// 8-bit unsigned mono PCM (0x80 = silence) for the SETUP sound test, converted
// from src-dos-setup/test-sample.wav to 11025 Hz. It lives in its own far
// segment (16-bit large model) so the ~30 KB stays out of DGROUP. The DOS mixer
// resamples SETUP_SAMPLE_RATE to the Sound Blaster output rate at play time.
#define SETUP_SAMPLE_RATE 11025

extern const unsigned char __far setupSample[];
extern const unsigned       setupSampleSize;

#endif
