# Audio file decoders

`dr_wav.h` and `dr_mp3.h` are vendored from
https://github.com/mackron/dr_libs at commit
`dfe8377631000664666519fdb83da193fd8037f4`.

They decode WAV and MP3 when a sample bank is loaded. Rendering does not call
these decoders. Their public-domain/MIT-0 licence is included in `LICENSE` and
at the ends of the headers. No network downloads or system codec libraries are
needed to build libcsynth.
