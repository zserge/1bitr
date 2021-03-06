#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SAMPLE_RATE 44100

/* Pluggable audio backend, capable of playing unsigned 8-bit mono audio */
struct backend {
  int (*start)();
  void (*stop)();
  void (*write)(unsigned char);
};

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
/* TODO */
#elif __linux__
/* A very minimal ALSA backend for Linux, does not require ALSA dev headers */
int snd_pcm_open(void **, const char *, int, int);
int snd_pcm_set_params(void *, int, int, int, int, int, int);
int snd_pcm_writei(void *, const void *, unsigned long);
int snd_pcm_recover(void *, int, int);
int snd_pcm_close(void *);
static void *pcm = NULL;
/* pcm buffer stores 1ms of audio data */
static unsigned char pcm_buf[SAMPLE_RATE / 100];
static unsigned char *pcm_ptr = pcm_buf;
static int alsa_start() {
  if (snd_pcm_open(&pcm, "default", 0, 0)) {
    return -1;
  }
  snd_pcm_set_params(pcm, 1, 3, 1, SAMPLE_RATE, 1, 20000);
  return 0;
}
static void alsa_stop() { snd_pcm_close(pcm); }
static void alsa_write(unsigned char sample) {
  *pcm_ptr++ = sample ? 0x30 : 0;
  if (pcm_ptr >= pcm_buf + sizeof(pcm_buf)) {
    int r = snd_pcm_writei(pcm, pcm_buf, sizeof(pcm_buf));
    if (r < 0) {
      snd_pcm_recover(pcm, r, 0);
    }
    pcm_ptr = pcm_buf;
  }
}
static struct backend playback = {
    alsa_start,
    alsa_stop,
    alsa_write,
};
#elif __APPLE__
#include <AudioUnit/AudioUnit.h>
/* TODO */
#endif

/* WAV file backend, used when stdout is redirected into a file */
static int wav_start() { return 0; }
static void wav_stop() {}
static void wav_write(unsigned char sample) {
  printf("%c", sample ? 0xff : 0);
  fflush(stdout);
}

static struct backend wav = {
    wav_start,
    wav_stop,
    wav_write,
};

/*
 * This is the most simple beeper routine. It takes 2 columns:
 * - Note (zero means silence), lower numbers mean higher pitch.
 * - Tempo (zero means no tempo change), lower numbers mean faster playback.
 */
static void engine_0(int *row, void (*out)(unsigned char)) {
  static int tempo = SAMPLE_RATE / 8;
  int counter = row[0] / 2;
  unsigned char value = 0;
  if (row[1]) {
    tempo = row[1] * 100;
  }
  for (int timer = 0; timer < tempo; timer++) {
    if (--counter == 0) {
      value = !value;
      counter = row[0];
    }
    out(value);
  }
}

/*
 * This is a 2-channel PFM engine with click drums and a few effects
 * - CH1 note
 * - CH2 note
 * - DRUM (1..4 - various click samples)
 * - FX: 1x = pulse width CH1
 *       2x = pulse width CH2
 *       3x = slide up CH1
 *       4x = slide down CH1
 *       fx = set tepmo
 */
static void engine_1(int *row, void (*out)(unsigned char)) {
  static int tempo = SAMPLE_RATE / 16;
  static int w1 = 0x8000, w2 = 0x8000;
  static int c1 = 0, c2 = 0;
  static int drums[5][16] = {
      {4, 2, 3, 2, 4, 1, 10, 1, 4, 2, 3, 8, 2, 2, 3, 0},
      {3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 3, 4, 4, 0},
      {2, 1, 2, 1, 2, 1, 1, 1, 2, 1, 2, 2, 1, 1, 1, 0},
      {5, 1, 5, 2, 5, 1, 5, 1, 5, 1, 5, 1, 5, 3, 5, 0},
      {6, 5, 4, 3, 2, 1, 1, 2, 3, 4, 5, 6, 7, 7, 8, 0},
  };
  /*
   * row[0] - note period in samples;
   * 1 - row[0]
   * ? - 65535
   */
  int inc1 = row[0] ? 0xffff / row[0] : 0;
  int inc2 = row[1] ? 0xffff / row[1] : 0;
  int drum = row[2];
  int drc = 0, *d = NULL;
  int param = (row[3] & 0xf);
  int dw = 0, dc = 0, t = 0;
  switch (row[3] >> 4) {
    case 1: w1 = 0x8000 * param / 15; break;
    case 2: w2 = 0x8000 * param / 15; break;
    case 3: dc = param; break;
    case 4: dc = -param; break;
    case 15: tempo = param * SAMPLE_RATE / 64; break;
  }
  if (drum > 0 && drum < 5) {
    d = drums[drum - 1];
    drc = *d;
    while (d && *d) {
      if (--drc == 0) {
        drc = *(++d) * 4;
      }
      out((d - drums[drum - 1]) & 1);
    }
  }
  for (t = 0; t < tempo; t++) {
    c1 = (c1 + inc1) & 0xffff;
    out(c1 > w1);
    inc1 = inc1 + dc;
    w1 = (w1 + dw) & 0xffff;
    if (row[1]) {
      c2 = (c2 + inc2) & 0xffff;
      out(c2 > w2);
    } else {
      out(c1 > w1);
    }
  }
}

static void (*engine)(int *row, void (*out)(unsigned char)) = NULL;

static void set_engine(char c) {
  switch (c) {
    case '0': engine = engine_0; break;
    case '1': engine = engine_1; break;
    default: fprintf(stderr, "invalid engine: %c\n", c); return;
  }
}

int main(int argc, const char *argv[]) {
  int c = 0;
  char word[4] = "\0", *wordptr = word;
  int row[256] = {0}, *rowptr = row;
  int lineno = 1;
  struct backend *backend = isatty(1) ? &playback : &wav;

  if (argc == 2 && strlen(argv[1]) == 2 && argv[1][0] == '-') {
    if (argv[1][1] == 'h') {
      fprintf(stderr, "%s [-X], where X is sound engine ID (try -0 or -1)\n",
              argv[0]);
      return 1;
    }
    set_engine(argv[1][1]);
  }

  if (backend->start() < 0) {
    fprintf(stderr, "failed to start playback: %d\n", errno);
    return 1;
  }

  while ((c = fgetc(stdin)) != EOF) {
    if (c == ';') {
      do {
        c = fgetc(stdin);
        if (lineno == 1 && !isspace(c) && engine == NULL) {
          set_engine(c);
        }
      } while (c != EOF && c != '\n');
    }

    if (c == '\n') {
      lineno++;
    }

    if (engine == NULL) {
      fprintf(stderr, "valid sound engine is not specified\n");
      break;
    }

    if (c == '\n' || c == ' ') {
      if (wordptr > word) {
        unsigned int n = 0;
        if ((word[0] >= 'A' && word[0] <= 'H') &&
            (word[1] == '-' || word[1] == '#') &&
            (word[2] >= '1' && word[2] <= '7')) {
          int notes[] = {9, 11, 0, 2, 4, 5, 7, 11};
          int p =
              notes[word[0] - 'A'] + (word[2] - '1') * 12 + (word[1] == '#');
          float f = 1;
          while (p--) f *= 1.0595;
          /* C-1 would be 32.7Hz, n would be note period in samples */
          n = (SAMPLE_RATE * 10 / 327) / f;
        } else if (word[0] == '-') {
          n = 0;
        } else {
          n = strtol(word, &wordptr, 16);
          if (*wordptr != '\0') {
            fprintf(stderr, "invalid token at line %d: %s\n", lineno, word);
            break;
          }
        }
        wordptr = word;
        *rowptr++ = n;
        if (rowptr >= row + sizeof(row) / sizeof(row[0])) {
          fprintf(stderr, "row is too long at line %d\n", lineno);
          break;
        }
      }
      if (c == '\n' && rowptr > row) {
        engine(row, backend->write);
        memset(row, 0, sizeof(row));
        rowptr = row;
      }
    } else {
      *wordptr++ = toupper(c);
      *wordptr = '\0';
      if (wordptr >= word + sizeof(word)) {
        fprintf(stderr, "token is too long: %s\n", word);
        break;
      }
    }
  }
  backend->stop();
  return 0;
}
