#!/usr/bin/env bash
# A small ffmpeg for Deckboy Mini's Linux downloads (x86_64 and ARM64).
#
# Mini only DECODES, so the general-purpose ffmpeg the desk ships -- every
# encoder, every external library, libavcodec alone 83 MB -- made "Mini" a
# 166 MB folder. This builds the same release (n8.1, the one the desk's build
# and Mini were compiled against, so the libraries are drop-in) with only what
# Mini uses: ffmpeg's own decoders and demuxers, the Linux hardware decoders
# (V4L2 memory-to-memory on a Raspberry Pi, VAAPI on a PC when present), and
# the one encoder and muxer Mini's subtitle extraction asks for (SRT), and
# raw video and audio out, for what Mini plays through the ffmpeg program
# (live streams, and anything the in-process decoder cannot open).
#
#   tools/build_mini_ffmpeg.sh [prefix]     # default /opt/ffmpeg-mini
set -euo pipefail
PREFIX="${1:-/opt/ffmpeg-mini}"
SRC="${TMPDIR:-/tmp}/ffmpeg-mini-src"
# x86 builds need an assembler for the hand-written decoders; ARM does not.
if [ "$(uname -m)" = x86_64 ] && ! command -v nasm >/dev/null; then sudo apt-get install -y nasm; fi
rm -rf "$SRC"
git clone --depth 1 --branch n8.1 https://github.com/FFmpeg/FFmpeg.git "$SRC"
cd "$SRC"
./configure --prefix="$PREFIX" --enable-shared --disable-static \
  --disable-doc --disable-debug --disable-ffplay \
  --disable-encoders --enable-encoder=srt,subrip,text,rawvideo,pcm_s16le \
  --disable-muxers --enable-muxer=srt,null,rawvideo,s16le \
  --disable-devices --disable-indevs --disable-outdevs \
  --disable-filters --enable-filter=buffer,buffersink,abuffer,abuffersink,scale,format,null,anull,aresample,aformat,copy,acopy,yadif,bwdif,hflip,vflip,transpose,rotate,setpts,asetpts,atempo,volume,pan \
  --disable-bsfs --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,vp9_superframe_split,extract_extradata,null \
  --disable-lzma --disable-bzlib --disable-iconv --disable-xlib --disable-libxcb \
  --disable-sdl2 --disable-vulkan --enable-libdrm
make -j"$(nproc)"
sudo make install
sudo ldconfig
"$PREFIX/bin/ffmpeg" -hide_banner -version | head -1
du -sh "$PREFIX/lib"
