# 1bitr

1bitr ("One Bitter" or "The Bitter One") is a minimalistic text-based music tracker. It only supports 1-bit audio playback and encourages users to write their own sound routines (engines), as well as the music itself.

## Text file format

A code snippet is worth a thousand words:

```
; 0
; Fifth Symphony (ta-da-da  dah...)
G-4 60
G-4
G-4
D#4 e0
```

You can play it as `./1bitr < examples/fifth`.

1bitr reads music line by line from `stdin`, parses it and passes each parsed row to the sound engine, which plays it. There are no patterns, no looping, no samples. Only notes and effects.

Line comments start with a `;`. Comments are ignored. Blank line are skipped as well. First line comment is a special one, see below about the engines.

Other lines must contain musical data that the sound engine can interpret and play.

Each line is a row of numerical values (up to 256 columns are supported, but most likely you will never use more than 16). Numerical values are writted in hexadecimal format (without `0x` prefix). Also, notes can be written as `C-3` or `A#4` and they are converted into numbers by parser. Values starting with a dash are interpreted as zeros (i.e. `--` is zero and `---` is zero and `00` is also zero).

## Playback

There are no loops or patterns beacause one can easily simulate them:

```
# Put each part into a separate file and concatenate them
$ cat INTRO P1 P2 P1 P3 | ./1bitr

# Play part of the file from line 10 to line 20
$ tail -n +20 MELODY | head -n 10 | ./1bitr

# Play random notes (shuffle music file)
$ cat MUSIC | sort -R | ./1bitr
```

If the stdout is not a terminal, `1bitr` write WAV data into the stdout (instead of playing it). This allows you to save music or to pass it further to other apps:

```
$ ./1bitr < MUSIC > music.wav
$ ./1bitr < MUSIC | aplay
```

## Engines

You are the master of your music. If you are into programming, please try writing your own 1-bit sound routine, it's likely to be fun! Please, make a PR to share it with the others.

Sound routine is a function that takes a row of up to 256 integers and a special `out` function, that set audio output to 0 or non-zero level.

Here's a sound routine that always plays noise and ignores all music data:

```c
void noise(int *row, void (*out)(unsigned char)) {
  out(rand() % 2);
}
```

And here's the Sound Engine Zero, it can only play 1 square wave of the given frequency with 50% duty cycle:

```c
static void zero(int *row, void (*out)(unsigned char)) {
  int counter = row[0];
  unsigned char value = 0;
  for (int timer = 0; timer < 5000; timer++) {
    if (--counter == 0) {
      value = !value;
      counter = row[0];
    }
    out(value);
  }
}
```

If you have added a new engine - choose a single-byte identifier for it (it can be a letter or a digit), and adjust `set_engine` function to switch to your engine. For example, for our engine "zero" would like to use identifier "Z":

```c
static void set_engine(char c) {
  switch (c) {
    ...
    case 'Z': engine = zero; break;
    ...
  }
}
```

Now you can write music for your engine. Either make the first line of your music file to be `; Z` or pass it via CLI switch: 

```
$ cat << EOF | ./1bitr
; Z
C-4
G-4
EOF

or

$ (echo "C-4"; echo "G-4") | ./1bitr -Z
```

## Default sound engines

Out of the box there are two sound engines: Zero and One. Engine Zero is mostly boring and useless, first column is note value, second is tempo.

Engine One is a bit more advanced. It can support 2 PWM channels, and click drums. The columns are:

```
; Note1 Note2 Drums FX
  C-4   C-5   03    15
  ---   D-5   00    00
```

Notes are obvious. Drums are short click samples, values from 1 to 5 are allowed.
Effects are also rather simple. High nibble is effect ID, low nibble is effect value.

* 1x - set channel 1 duty cycle to X. 1f - 50%, 10 - 0%.
* 2x - same for channel 2.
* 3x - swipe frequency up at speex X.
* 4x - swipe frequency down at speed X.
* fx - set tempo to X.

Both sound engines are provided mosly as references to let you build your own.

## License

Code is distributed under MIT license. PRs are welcome! If you have written any interesting (or buzzing and noisy) music - feel free to share it using the issue tracker or twitter with #1bitr hashtag!
