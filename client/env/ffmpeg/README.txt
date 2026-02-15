FFmpeg 64-bit shared Windows build from www.gyan.dev

Version: 8.0.1-full_build-www.gyan.dev

License: GPL v3

Source Code: https://github.com/FFmpeg/FFmpeg/commit/894da5ca7d

External Assets
frei0r plugins:   https://www.gyan.dev/ffmpeg/builds/ffmpeg-frei0r-plugins
lensfun database: https://www.gyan.dev/ffmpeg/builds/ffmpeg-lensfun-db
whisper models:   https://huggingface.co/ggerganov/whisper.cpp/tree/main

release-full build configuration: 

ARCH                      x86 (generic)
big-endian                no
runtime cpu detection     yes
standalone assembly       yes
x86 assembler             nasm
MMX enabled               yes
MMXEXT enabled            yes
3DNow! enabled            yes
3DNow! extended enabled   yes
SSE enabled               yes
SSSE3 enabled             yes
AESNI enabled             yes
AVX enabled               yes
AVX2 enabled              yes
AVX-512 enabled           yes
AVX-512ICL enabled        yes
XOP enabled               yes
FMA3 enabled              yes
FMA4 enabled              yes
i686 features enabled     yes
CMOV is fast              yes
EBX available             yes
EBP available             yes
debug symbols             yes
strip symbols             yes
optimize for size         no
optimizations             yes
static                    no
shared                    yes
network support           yes
threading support         pthreads
safe bitstream reader     yes
texi2html enabled         no
perl enabled              yes
pod2man enabled           yes
makeinfo enabled          yes
makeinfo supports HTML    yes
xmllint enabled           yes

External libraries:
avisynth                libharfbuzz             libtheora
bzlib                   libilbc                 libtwolame
chromaprint             libjxl                  libuavs3d
frei0r                  liblc3                  libvidstab
gmp                     liblensfun              libvmaf
gnutls                  libmodplug              libvo_amrwbenc
iconv                   libmp3lame              libvorbis
ladspa                  libmysofa               libvpx
lcms2                   liboapv                 libvvenc
libaom                  libopencore_amrnb       libwebp
libaribb24              libopencore_amrwb       libx264
libaribcaption          libopenjpeg             libx265
libass                  libopenmpt              libxavs2
libbluray               libopus                 libxevd
libbs2b                 libplacebo              libxeve
libcaca                 libqrencode             libxml2
libcdio                 libquirc                libxvid
libcodec2               librav1e                libzimg
libdav1d                librist                 libzmq
libdavs2                librubberband           libzvbi
libdvdnav               libshaderc              lzma
libdvdread              libshine                mediafoundation
libflite                libsnappy               openal
libfontconfig           libsoxr                 sdl2
libfreetype             libspeex                whisper
libfribidi              libsrt                  zlib
libgme                  libssh
libgsm                  libsvtav1

External libraries providing hardware acceleration:
amf                     d3d12va                 nvdec
cuda                    dxva2                   nvenc
cuda_llvm               ffnvcodec               opencl
cuvid                   libmfx                  vaapi
d3d11va                 libvpl                  vulkan

Libraries:
avcodec                 avformat                swscale
avdevice                avutil
avfilter                swresample

Programs:
ffmpeg                  ffplay                  ffprobe

Enabled decoders:
aac                     g723_1                  pcx
aac_fixed               g728                    pdv
aac_latm                g729                    pfm
aasc                    gdv                     pgm
ac3                     gem                     pgmyuv
ac3_fixed               gif                     pgssub
acelp_kelvin            gremlin_dpcm            pgx
adpcm_4xm               gsm                     phm
adpcm_adx               gsm_ms                  photocd
adpcm_afc               h261                    pictor
adpcm_agm               h263                    pixlet
adpcm_aica              h263i                   pjs
adpcm_argo              h263p                   png
adpcm_ct                h264                    ppm
adpcm_dtk               h264_amf                prores
adpcm_ea                h264_cuvid              prores_raw
adpcm_ea_maxis_xa       h264_qsv                prosumer
adpcm_ea_r1             hap                     psd
adpcm_ea_r2             hca                     ptx
adpcm_ea_r3             hcom                    qcelp
adpcm_ea_xas            hdr                     qdm2
adpcm_g722              hevc                    qdmc
adpcm_g726              hevc_amf                qdraw
adpcm_g726le            hevc_cuvid              qoa
adpcm_ima_acorn         hevc_qsv                qoi
adpcm_ima_alp           hnm4_video              qpeg
adpcm_ima_amv           hq_hqa                  qtrle
adpcm_ima_apc           hqx                     r10k
adpcm_ima_apm           huffyuv                 r210
adpcm_ima_cunning       hymt                    ra_144
adpcm_ima_dat4          iac                     ra_288
adpcm_ima_dk3           idcin                   ralf
adpcm_ima_dk4           idf                     rasc
adpcm_ima_ea_eacs       iff_ilbm                rawvideo
adpcm_ima_ea_sead       ilbc                    realtext
adpcm_ima_iss           imc                     rka
adpcm_ima_moflex        imm4                    rl2
adpcm_ima_mtf           imm5                    roq
adpcm_ima_oki           indeo2                  roq_dpcm
adpcm_ima_qt            indeo3                  rpza
adpcm_ima_rad           indeo4                  rscc
adpcm_ima_smjpeg        indeo5                  rtv1
adpcm_ima_ssi           interplay_acm           rv10
adpcm_ima_wav           interplay_dpcm          rv20
adpcm_ima_ws            interplay_video         rv30
adpcm_ima_xbox          ipu                     rv40
adpcm_ms                jacosub                 rv60
adpcm_mtaf              jpeg2000                s302m
adpcm_psx               jpegls                  sami
adpcm_sanyo             jv                      sanm
adpcm_sbpro_2           kgv1                    sbc
adpcm_sbpro_3           kmvc                    scpr
adpcm_sbpro_4           lagarith                screenpresso
adpcm_swf               lead                    sdx2_dpcm
adpcm_thp               libaom_av1              sga
adpcm_thp_le            libaribb24              sgi
adpcm_vima              libaribcaption          sgirle
adpcm_xa                libcodec2               sheervideo
adpcm_xmd               libdav1d                shorten
adpcm_yamaha            libdavs2                simbiosis_imx
adpcm_zork              libgsm                  sipr
agm                     libgsm_ms               siren
aic                     libilbc                 smackaud
alac                    libjxl                  smacker
alias_pix               libjxl_anim             smc
als                     liblc3                  smvjpeg
amrnb                   libopencore_amrnb       snow
amrwb                   libopencore_amrwb       sol_dpcm
amv                     libopus                 sonic
anm                     libspeex                sp5x
ansi                    libuavs3d               speedhq
anull                   libvorbis               speex
apac                    libvpx_vp8              srgc
ape                     libvpx_vp9              srt
apng                    libxevd                 ssa
aptx                    libzvbi_teletext        stl
aptx_hd                 loco                    subrip
apv                     lscr                    subviewer
arbc                    m101                    subviewer1
argo                    mace3                   sunrast
ass                     mace6                   svq1
asv1                    magicyuv                svq3
asv2                    mdec                    tak
atrac1                  media100                targa
atrac3                  metasound               targa_y216
atrac3al                microdvd                tdsc
atrac3p                 mimic                   text
atrac3pal               misc4                   theora
atrac9                  mjpeg                   thp
aura                    mjpeg_cuvid             tiertexseqvideo
aura2                   mjpeg_qsv               tiff
av1                     mjpegb                  tmv
av1_amf                 mlp                     truehd
av1_cuvid               mmvideo                 truemotion1
av1_qsv                 mobiclip                truemotion2
avrn                    motionpixels            truemotion2rt
avrp                    movtext                 truespeech
avs                     mp1                     tscc
avui                    mp1float                tscc2
bethsoftvid             mp2                     tta
bfi                     mp2float                twinvq
bink                    mp3                     txd
binkaudio_dct           mp3adu                  ulti
binkaudio_rdft          mp3adufloat             utvideo
bintext                 mp3float                v210
bitpacked               mp3on4                  v210x
bmp                     mp3on4float             v308
bmv_audio               mpc7                    v408
bmv_video               mpc8                    v410
bonk                    mpeg1_cuvid             vb
brender_pix             mpeg1video              vble
c93                     mpeg2_cuvid             vbn
cavs                    mpeg2_qsv               vc1
cbd2_dpcm               mpeg2video              vc1_cuvid
ccaption                mpeg4                   vc1_qsv
cdgraphics              mpeg4_cuvid             vc1image
cdtoons                 mpegvideo               vcr1
cdxl                    mpl2                    vmdaudio
cfhd                    msa1                    vmdvideo
cinepak                 mscc                    vmix
clearvideo              msmpeg4v1               vmnc
cljr                    msmpeg4v2               vnull
cllc                    msmpeg4v3               vorbis
comfortnoise            msnsiren                vp3
cook                    msp2                    vp4
cpia                    msrle                   vp5
cri                     mss1                    vp6
cscd                    mss2                    vp6a
cyuv                    msvideo1                vp6f
dca                     mszh                    vp7
dds                     mts2                    vp8
derf_dpcm               mv30                    vp8_cuvid
dfa                     mvc1                    vp8_qsv
dfpwm                   mvc2                    vp9
dirac                   mvdv                    vp9_amf
dnxhd                   mvha                    vp9_cuvid
dolby_e                 mwsc                    vp9_qsv
dpx                     mxpeg                   vplayer
dsd_lsbf                nellymoser              vqa
dsd_lsbf_planar         notchlc                 vqc
dsd_msbf                nuv                     vvc
dsd_msbf_planar         on2avc                  vvc_qsv
dsicinaudio             opus                    wady_dpcm
dsicinvideo             osq                     wavarc
dss_sp                  paf_audio               wavpack
dst                     paf_video               wbmp
dvaudio                 pam                     wcmv
dvbsub                  pbm                     webp
dvdsub                  pcm_alaw                webvtt
dvvideo                 pcm_bluray              wmalossless
dxa                     pcm_dvd                 wmapro
dxtory                  pcm_f16le               wmav1
dxv                     pcm_f24le               wmav2
eac3                    pcm_f32be               wmavoice
eacmv                   pcm_f32le               wmv1
eamad                   pcm_f64be               wmv2
eatgq                   pcm_f64le               wmv3
eatgv                   pcm_lxf                 wmv3image
eatqi                   pcm_mulaw               wnv1
eightbps                pcm_s16be               wrapped_avframe
eightsvx_exp            pcm_s16be_planar        ws_snd1
eightsvx_fib            pcm_s16le               xan_dpcm
escape124               pcm_s16le_planar        xan_wc3
escape130               pcm_s24be               xan_wc4
evrc                    pcm_s24daud             xbin
exr                     pcm_s24le               xbm
fastaudio               pcm_s24le_planar        xface
ffv1                    pcm_s32be               xl
ffvhuff                 pcm_s32le               xma1
ffwavesynth             pcm_s32le_planar        xma2
fic                     pcm_s64be               xpm
fits                    pcm_s64le               xsub
flac                    pcm_s8                  xwd
flashsv                 pcm_s8_planar           y41p
flashsv2                pcm_sga                 ylc
flic                    pcm_u16be               yop
flv                     pcm_u16le               yuv4
fmvc                    pcm_u24be               zero12v
fourxm                  pcm_u24le               zerocodec
fraps                   pcm_u32be               zlib
frwu                    pcm_u32le               zmbv
ftr                     pcm_u8
g2m                     pcm_vidc

Enabled encoders:
a64multi                hevc_nvenc              pcm_s32le_planar
a64multi5               hevc_qsv                pcm_s64be
aac                     hevc_vaapi              pcm_s64le
aac_mf                  hevc_vulkan             pcm_s8
ac3                     huffyuv                 pcm_s8_planar
ac3_fixed               jpeg2000                pcm_u16be
ac3_mf                  jpegls                  pcm_u16le
adpcm_adx               libaom_av1              pcm_u24be
adpcm_argo              libcodec2               pcm_u24le
adpcm_g722              libgsm                  pcm_u32be
adpcm_g726              libgsm_ms               pcm_u32le
adpcm_g726le            libilbc                 pcm_u8
adpcm_ima_alp           libjxl                  pcm_vidc
adpcm_ima_amv           libjxl_anim             pcx
adpcm_ima_apm           liblc3                  pfm
adpcm_ima_qt            libmp3lame              pgm
adpcm_ima_ssi           liboapv                 pgmyuv
adpcm_ima_wav           libopencore_amrnb       phm
adpcm_ima_ws            libopenjpeg             png
adpcm_ms                libopus                 ppm
adpcm_swf               librav1e                prores
adpcm_yamaha            libshine                prores_aw
alac                    libspeex                prores_ks
alias_pix               libsvtav1               qoi
amv                     libtheora               qtrle
anull                   libtwolame              r10k
apng                    libvo_amrwbenc          r210
aptx                    libvorbis               ra_144
aptx_hd                 libvpx_vp8              rawvideo
ass                     libvpx_vp9              roq
asv1                    libvvenc                roq_dpcm
asv2                    libwebp                 rpza
av1_amf                 libwebp_anim            rv10
av1_mf                  libx264                 rv20
av1_nvenc               libx264rgb              s302m
av1_qsv                 libx265                 sbc
av1_vaapi               libxavs2                sgi
av1_vulkan              libxeve                 smc
avrp                    libxvid                 snow
avui                    ljpeg                   speedhq
bitpacked               magicyuv                srt
bmp                     mjpeg                   ssa
cfhd                    mjpeg_qsv               subrip
cinepak                 mjpeg_vaapi             sunrast
cljr                    mlp                     svq1
comfortnoise            movtext                 targa
dca                     mp2                     text
dfpwm                   mp2fixed                tiff
dnxhd                   mp3_mf                  truehd
dpx                     mpeg1video              tta
dvbsub                  mpeg2_qsv               ttml
dvdsub                  mpeg2_vaapi             utvideo
dvvideo                 mpeg2video              v210
dxv                     mpeg4                   v308
eac3                    msmpeg4v2               v408
exr                     msmpeg4v3               v410
ffv1                    msrle                   vbn
ffv1_vulkan             msvideo1                vc2
ffvhuff                 nellymoser              vnull
fits                    opus                    vorbis
flac                    pam                     vp8_vaapi
flashsv                 pbm                     vp9_qsv
flashsv2                pcm_alaw                vp9_vaapi
flv                     pcm_bluray              wavpack
g723_1                  pcm_dvd                 wbmp
gif                     pcm_f32be               webvtt
h261                    pcm_f32le               wmav1
h263                    pcm_f64be               wmav2
h263p                   pcm_f64le               wmv1
h264_amf                pcm_mulaw               wmv2
h264_mf                 pcm_s16be               wrapped_avframe
h264_nvenc              pcm_s16be_planar        xbm
h264_qsv                pcm_s16le               xface
h264_vaapi              pcm_s16le_planar        xsub
h264_vulkan             pcm_s24be               xwd
hap                     pcm_s24daud             y41p
hdr                     pcm_s24le               yuv4
hevc_amf                pcm_s24le_planar        zlib
hevc_d3d12va            pcm_s32be               zmbv
hevc_mf                 pcm_s32le

Enabled hwaccels:
av1_d3d11va             hevc_dxva2              vc1_dxva2
av1_d3d11va2            hevc_nvdec              vc1_nvdec
av1_d3d12va             hevc_vaapi              vc1_vaapi
av1_dxva2               hevc_vulkan             vp8_nvdec
av1_nvdec               mjpeg_nvdec             vp8_vaapi
av1_vaapi               mjpeg_vaapi             vp9_d3d11va
av1_vulkan              mpeg1_nvdec             vp9_d3d11va2
ffv1_vulkan             mpeg2_d3d11va           vp9_d3d12va
h263_vaapi              mpeg2_d3d11va2          vp9_dxva2
h264_d3d11va            mpeg2_d3d12va           vp9_nvdec
h264_d3d11va2           mpeg2_dxva2             vp9_vaapi
h264_d3d12va            mpeg2_nvdec             vp9_vulkan
h264_dxva2              mpeg2_vaapi             vvc_vaapi
h264_nvdec              mpeg4_nvdec             wmv3_d3d11va
h264_vaapi              mpeg4_vaapi             wmv3_d3d11va2
h264_vulkan             prores_raw_vulkan       wmv3_d3d12va
hevc_d3d11va            vc1_d3d11va             wmv3_dxva2
hevc_d3d11va2           vc1_d3d11va2            wmv3_nvdec
hevc_d3d12va            vc1_d3d12va             wmv3_vaapi

Enabled parsers:
aac                     dvdsub                  mpegvideo
aac_latm                evc                     opus
ac3                     ffv1                    png
adx                     flac                    pnm
amr                     ftr                     prores_raw
apv                     g723_1                  qoi
av1                     g729                    rv34
avs2                    gif                     sbc
avs3                    gsm                     sipr
bmp                     h261                    tak
cavsvideo               h263                    vc1
cook                    h264                    vorbis
cri                     hdr                     vp3
dca                     hevc                    vp8
dirac                   ipu                     vp9
dnxhd                   jpeg2000                vvc
dnxuc                   jpegxl                  webp
dolby_e                 misc4                   xbm
dpx                     mjpeg                   xma
dvaudio                 mlp                     xwd
dvbsub                  mpeg4video
dvd_nav                 mpegaudio

Enabled demuxers:
aa                      ico                     pcm_f64be
aac                     idcin                   pcm_f64le
aax                     idf                     pcm_mulaw
ac3                     iff                     pcm_s16be
ac4                     ifv                     pcm_s16le
ace                     ilbc                    pcm_s24be
acm                     image2                  pcm_s24le
act                     image2_alias_pix        pcm_s32be
adf                     image2_brender_pix      pcm_s32le
adp                     image2pipe              pcm_s8
ads                     image_bmp_pipe          pcm_u16be
adx                     image_cri_pipe          pcm_u16le
aea                     image_dds_pipe          pcm_u24be
afc                     image_dpx_pipe          pcm_u24le
aiff                    image_exr_pipe          pcm_u32be
aix                     image_gem_pipe          pcm_u32le
alp                     image_gif_pipe          pcm_u8
amr                     image_hdr_pipe          pcm_vidc
amrnb                   image_j2k_pipe          pdv
amrwb                   image_jpeg_pipe         pjs
anm                     image_jpegls_pipe       pmp
apac                    image_jpegxl_pipe       pp_bnk
apc                     image_pam_pipe          pva
ape                     image_pbm_pipe          pvf
apm                     image_pcx_pipe          qcp
apng                    image_pfm_pipe          qoa
aptx                    image_pgm_pipe          r3d
aptx_hd                 image_pgmyuv_pipe       rawvideo
apv                     image_pgx_pipe          rcwt
aqtitle                 image_phm_pipe          realtext
argo_asf                image_photocd_pipe      redspark
argo_brp                image_pictor_pipe       rka
argo_cvg                image_png_pipe          rl2
asf                     image_ppm_pipe          rm
asf_o                   image_psd_pipe          roq
ass                     image_qdraw_pipe        rpl
ast                     image_qoi_pipe          rsd
au                      image_sgi_pipe          rso
av1                     image_sunrast_pipe      rtp
avi                     image_svg_pipe          rtsp
avisynth                image_tiff_pipe         s337m
avr                     image_vbn_pipe          sami
avs                     image_webp_pipe         sap
avs2                    image_xbm_pipe          sbc
avs3                    image_xpm_pipe          sbg
bethsoftvid             image_xwd_pipe          scc
bfi                     imf                     scd
bfstm                   ingenient               sdns
bink                    ipmovie                 sdp
binka                   ipu                     sdr2
bintext                 ircam                   sds
bit                     iss                     sdx
bitpacked               iv8                     segafilm
bmv                     ivf                     ser
boa                     ivr                     sga
bonk                    jacosub                 shorten
brstm                   jpegxl_anim             siff
c93                     jv                      simbiosis_imx
caf                     kux                     sln
cavsvideo               kvag                    smacker
cdg                     laf                     smjpeg
cdxl                    lc3                     smush
cine                    libgme                  sol
codec2                  libmodplug              sox
codec2raw               libopenmpt              spdif
concat                  live_flv                srt
dash                    lmlm4                   stl
data                    loas                    str
daud                    lrc                     subviewer
dcstr                   luodat                  subviewer1
derf                    lvf                     sup
dfa                     lxf                     svag
dfpwm                   m4v                     svs
dhav                    matroska                swf
dirac                   mca                     tak
dnxhd                   mcc                     tedcaptions
dsf                     mgsts                   thp
dsicin                  microdvd                threedostr
dss                     mjpeg                   tiertexseq
dts                     mjpeg_2000              tmv
dtshd                   mlp                     truehd
dv                      mlv                     tta
dvbsub                  mm                      tty
dvbtxt                  mmf                     txd
dvdvideo                mods                    ty
dxa                     moflex                  usm
ea                      mov                     v210
ea_cdata                mp3                     v210x
eac3                    mpc                     vag
epaf                    mpc8                    vc1
evc                     mpegps                  vc1t
ffmetadata              mpegts                  vividas
filmstrip               mpegtsraw               vivo
fits                    mpegvideo               vmd
flac                    mpjpeg                  vobsub
flic                    mpl2                    voc
flv                     mpsub                   vpk
fourxm                  msf                     vplayer
frm                     msnwc_tcp               vqf
fsb                     msp                     vvc
fwse                    mtaf                    w64
g722                    mtv                     wady
g723_1                  musx                    wav
g726                    mv                      wavarc
g726le                  mvi                     wc3
g728                    mxf                     webm_dash_manifest
g729                    mxg                     webvtt
gdv                     nc                      wsaud
genh                    nistsphere              wsd
gif                     nsp                     wsvqa
gsm                     nsv                     wtv
gxf                     nut                     wv
h261                    nuv                     wve
h263                    obu                     xa
h264                    ogg                     xbin
hca                     oma                     xmd
hcom                    osq                     xmv
hevc                    paf                     xvag
hls                     pcm_alaw                xwma
hnm                     pcm_f32be               yop
iamf                    pcm_f32le               yuv4mpegpipe

Enabled muxers:
a64                     h261                    pcm_s16be
ac3                     h263                    pcm_s16le
ac4                     h264                    pcm_s24be
adts                    hash                    pcm_s24le
adx                     hds                     pcm_s32be
aea                     hevc                    pcm_s32le
aiff                    hls                     pcm_s8
alp                     iamf                    pcm_u16be
amr                     ico                     pcm_u16le
amv                     ilbc                    pcm_u24be
apm                     image2                  pcm_u24le
apng                    image2pipe              pcm_u32be
aptx                    ipod                    pcm_u32le
aptx_hd                 ircam                   pcm_u8
apv                     ismv                    pcm_vidc
argo_asf                ivf                     psp
argo_cvg                jacosub                 rawvideo
asf                     kvag                    rcwt
asf_stream              latm                    rm
ass                     lc3                     roq
ast                     lrc                     rso
au                      m4v                     rtp
avi                     matroska                rtp_mpegts
avif                    matroska_audio          rtsp
avm2                    mcc                     sap
avs2                    md5                     sbc
avs3                    microdvd                scc
bit                     mjpeg                   segafilm
caf                     mkvtimestamp_v2         segment
cavsvideo               mlp                     smjpeg
chromaprint             mmf                     smoothstreaming
codec2                  mov                     sox
codec2raw               mp2                     spdif
crc                     mp3                     spx
dash                    mp4                     srt
data                    mpeg1system             stream_segment
daud                    mpeg1vcd                streamhash
dfpwm                   mpeg1video              sup
dirac                   mpeg2dvd                swf
dnxhd                   mpeg2svcd               tee
dts                     mpeg2video              tg2
dv                      mpeg2vob                tgp
eac3                    mpegts                  truehd
evc                     mpjpeg                  tta
f4v                     mxf                     ttml
ffmetadata              mxf_d10                 uncodedframecrc
fifo                    mxf_opatom              vc1
filmstrip               null                    vc1t
fits                    nut                     voc
flac                    obu                     vvc
flv                     oga                     w64
framecrc                ogg                     wav
framehash               ogv                     webm
framemd5                oma                     webm_chunk
g722                    opus                    webm_dash_manifest
g723_1                  pcm_alaw                webp
g726                    pcm_f32be               webvtt
g726le                  pcm_f32le               wsaud
gif                     pcm_f64be               wtv
gsm                     pcm_f64le               wv
gxf                     pcm_mulaw               yuv4mpegpipe

Enabled protocols:
async                   http                    rtmp
bluray                  httpproxy               rtmpe
cache                   https                   rtmps
concat                  icecast                 rtmpt
concatf                 ipfs_gateway            rtmpte
crypto                  ipns_gateway            rtmpts
data                    librist                 rtp
fd                      libsrt                  srtp
ffrtmpcrypt             libssh                  subfile
ffrtmphttp              libzmq                  tcp
file                    md5                     tee
ftp                     mmsh                    tls
gopher                  mmst                    udp
gophers                 pipe                    udplite
hls                     prompeg

Enabled filters:
a3dscope                deblock                 perms
aap                     decimate                perspective
abench                  deconvolve              phase
abitscope               dedot                   photosensitivity
acompressor             deesser                 pixdesctest
acontrast               deflate                 pixelize
acopy                   deflicker               pixscope
acrossfade              deinterlace_qsv         pp7
acrossover              deinterlace_vaapi       premultiply
acrusher                dejudder                prewitt
acue                    delogo                  prewitt_opencl
addroi                  denoise_vaapi           procamp_vaapi
adeclick                deshake                 program_opencl
adeclip                 deshake_opencl          pseudocolor
adecorrelate            despill                 psnr
adelay                  detelecine              pullup
adenorm                 dialoguenhance          qp
aderivative             dilation                qrencode
adrawgraph              dilation_opencl         qrencodesrc
adrc                    displace                quirc
adynamicequalizer       doubleweave             random
adynamicsmooth          drawbox                 readeia608
aecho                   drawbox_vaapi           readvitc
aemphasis               drawgraph               realtime
aeval                   drawgrid                remap
aevalsrc                drawtext                remap_opencl
aexciter                drmeter                 removegrain
afade                   dynaudnorm              removelogo
afdelaysrc              earwax                  repeatfields
afftdn                  ebur128                 replaygain
afftfilt                edgedetect              reverse
afir                    elbg                    rgbashift
afireqsrc               entropy                 rgbtestsrc
afirsrc                 epx                     roberts
aformat                 eq                      roberts_opencl
afreqshift              equalizer               rotate
afwtdn                  erosion                 rubberband
agate                   erosion_opencl          sab
agraphmonitor           estdif                  scale
ahistogram              exposure                scale2ref
aiir                    extractplanes           scale_cuda
aintegral               extrastereo             scale_d3d11
ainterleave             fade                    scale_qsv
alatency                feedback                scale_vaapi
alimiter                fftdnoiz                scale_vulkan
allpass                 fftfilt                 scdet
allrgb                  field                   scdet_vulkan
allyuv                  fieldhint               scharr
aloop                   fieldmatch              scroll
alphaextract            fieldorder              segment
alphamerge              fillborders             select
amerge                  find_rect               selectivecolor
ametadata               firequalizer            sendcmd
amix                    flanger                 separatefields
amovie                  flip_vulkan             setdar
amplify                 flite                   setfield
amultiply               floodfill               setparams
anequalizer             format                  setpts
anlmdn                  fps                     setrange
anlmf                   framepack               setsar
anlms                   framerate               settb
anoisesrc               framestep               sharpness_vaapi
anull                   freezedetect            shear
anullsink               freezeframes            showcqt
anullsrc                frei0r                  showcwt
apad                    frei0r_src              showfreqs
aperms                  fspp                    showinfo
aphasemeter             fsync                   showpalette
aphaser                 gblur                   showspatial
aphaseshift             gblur_vulkan            showspectrum
apsnr                   geq                     showspectrumpic
apsyclip                gradfun                 showvolume
apulsator               gradients               showwaves
arealtime               graphmonitor            showwavespic
aresample               grayworld               shuffleframes
areverse                greyedge                shufflepixels
arls                    guided                  shuffleplanes
arnndn                  haas                    sidechaincompress
asdr                    haldclut                sidechaingate
asegment                haldclutsrc             sidedata
aselect                 hdcd                    sierpinski
asendcmd                headphone               signalstats
asetnsamples            hflip                   signature
asetpts                 hflip_vulkan            silencedetect
asetrate                highpass                silenceremove
asettb                  highshelf               sinc
ashowinfo               hilbert                 sine
asidedata               histeq                  siti
asisdr                  histogram               smartblur
asoftclip               hqdn3d                  smptebars
aspectralstats          hqx                     smptehdbars
asplit                  hstack                  sobel
ass                     hstack_qsv              sobel_opencl
astats                  hstack_vaapi            sofalizer
astreamselect           hsvhold                 spectrumsynth
asubboost               hsvkey                  speechnorm
asubcut                 hue                     split
asupercut               huesaturation           spp
asuperpass              hwdownload              sr_amf
asuperstop              hwmap                   ssim
atadenoise              hwupload                ssim360
atempo                  hwupload_cuda           stereo3d
atilt                   hysteresis              stereotools
atrim                   iccdetect               stereowiden
avectorscope            iccgen                  streamselect
avgblur                 identity                subtitles
avgblur_opencl          idet                    super2xsai
avgblur_vulkan          il                      superequalizer
avsynctest              inflate                 surround
axcorrelate             interlace               swaprect
azmq                    interlace_vulkan        swapuv
backgroundkey           interleave              tblend
bandpass                join                    telecine
bandreject              kerndeint               testsrc
bass                    kirsch                  testsrc2
bbox                    ladspa                  thistogram
bench                   lagfun                  threshold
bilateral               latency                 thumbnail
bilateral_cuda          lenscorrection          thumbnail_cuda
biquad                  lensfun                 tile
bitplanenoise           libplacebo              tiltandshift
blackdetect             libvmaf                 tiltshelf
blackdetect_vulkan      life                    tinterlace
blackframe              limitdiff               tlut2
blend                   limiter                 tmedian
blend_vulkan            loop                    tmidequalizer
blockdetect             loudnorm                tmix
blurdetect              lowpass                 tonemap
bm3d                    lowshelf                tonemap_opencl
boxblur                 lumakey                 tonemap_vaapi
boxblur_opencl          lut                     tpad
bs2b                    lut1d                   transpose
bwdif                   lut2                    transpose_opencl
bwdif_cuda              lut3d                   transpose_vaapi
bwdif_vulkan            lutrgb                  transpose_vulkan
cas                     lutyuv                  treble
ccrepack                mandelbrot              tremolo
cellauto                maskedclamp             trim
channelmap              maskedmax               unpremultiply
channelsplit            maskedmerge             unsharp
chorus                  maskedmin               unsharp_opencl
chromaber_vulkan        maskedthreshold         untile
chromahold              maskfun                 uspp
chromakey               mcdeint                 v360
chromakey_cuda          mcompand                vaguedenoiser
chromanr                median                  varblur
chromashift             mergeplanes             vectorscope
ciescope                mestimate               vflip
codecview               metadata                vflip_vulkan
color                   midequalizer            vfrdet
color_vulkan            minterpolate            vibrance
colorbalance            mix                     vibrato
colorchannelmixer       monochrome              vidstabdetect
colorchart              morpho                  vidstabtransform
colorcontrast           movie                   vif
colorcorrect            mpdecimate              vignette
colordetect             mptestsrc               virtualbass
colorhold               msad                    vmafmotion
colorize                multiply                volume
colorkey                negate                  volumedetect
colorkey_opencl         nlmeans                 vpp_amf
colorlevels             nlmeans_opencl          vpp_qsv
colormap                nlmeans_vulkan          vstack
colormatrix             nnedi                   vstack_qsv
colorspace              noformat                vstack_vaapi
colorspace_cuda         noise                   w3fdif
colorspectrum           normalize               waveform
colortemperature        null                    weave
compand                 nullsink                whisper
compensationdelay       nullsrc                 xbr
concat                  openclsrc               xcorrelate
convolution             oscilloscope            xfade
convolution_opencl      overlay                 xfade_opencl
convolve                overlay_cuda            xfade_vulkan
copy                    overlay_opencl          xmedian
corr                    overlay_qsv             xpsnr
cover_rect              overlay_vaapi           xstack
crop                    overlay_vulkan          xstack_qsv
cropdetect              owdenoise               xstack_vaapi
crossfeed               pad                     yadif
crystalizer             pad_cuda                yadif_cuda
cue                     pad_opencl              yaepblur
curves                  pad_vaapi               yuvtestsrc
datascope               pal100bars              zmq
dblur                   pal75bars               zoneplate
dcshift                 palettegen              zoompan
dctdnoiz                paletteuse              zscale
ddagrab                 pan
deband                  perlin

Enabled bsfs:
aac_adtstoasc           h264_metadata           pcm_rechunk
apv_metadata            h264_mp4toannexb        pgs_frame_merge
av1_frame_merge         h264_redundant_pps      prores_metadata
av1_frame_split         hapqa_extract           remove_extradata
av1_metadata            hevc_metadata           setts
chomp                   hevc_mp4toannexb        showinfo
dca_core                imx_dump_header         smpte436m_to_eia608
dovi_rpu                media100_to_mjpegb      text2movsub
dts2pts                 mjpeg2jpeg              trace_headers
dump_extradata          mjpega_dump_header      truehd_core
dv_error_marker         mov2textsub             vp9_metadata
eac3_core               mpeg2_metadata          vp9_raw_reorder
eia608_to_smpte436m     mpeg4_unpack_bframes    vp9_superframe
evc_frame_merge         noise                   vp9_superframe_split
extract_extradata       null                    vvc_metadata
filter_units            opus_metadata           vvc_mp4toannexb

Enabled indevs:
dshow                   lavfi                   openal
gdigrab                 libcdio                 vfwcap

Enabled outdevs:
caca

release-full external libraries' versions: 

AMF v1.5.0
aom v3.13.1-119-g2e124f462b
aribb24 v1.0.3-5-g5e9be27
aribcaption 1.1.1
AviSynthPlus v3.7.5-37-gb6d79944
bs2b 3.1.0
cairo 1.18.5
chromaprint 1.6.0
codec2 1.2.0-106-g96e8a19c
dav1d 1.5.2-8-ge7c280e
davs2 1.7-1-gb41cf11
dvdnav 7.0.0
dvdread 7.0.1
ffnvcodec n13.0.19.0-2-g876af32
flite v2.2-55-g6c9f20d
freetype VER-2-14-1
frei0r v2.5.0-7-g40a50be
fribidi v1.0.16-2-gb28f43b
gsm 1.0.22
harfbuzz 12.2.0-18-g3129b36c
ladspa-sdk 1.17
lame 3.100
lc3 1.1.3
lcms2 2.16
lensfun v0.3.95-1836-g71fcb51a
libass 0.17.4-16-ge60dddb
libcdio-paranoia 10.2
libgme 0.6.4
libilbc v3.0.4-346-g6adb26d4a4
libjxl v0.11-snapshot-456-g9174e635
libopencore-amrnb 0.1.6
libopencore-amrwb 0.1.6
libplacebo v7.351.0-90-g2e5a392
libsoxr 0.1.3
libssh 0.11.3
libtheora v1.2.0
libwebp v1.6.0-123-g2760d87
openal-soft latest
openapv v0.2.0.4-3-g8d2e357
openmpt libopenmpt-0.6.25-27-gfcf6687cb
opus v1.5.2-320-g2c0b1187
qrencode 4.1.1
quirc 1.2
rav1e p20250624-1-gb7bf390
rist 0.2.12
rubberband v1.8.1
SDL release-2.32.0-137-gc44d37b5c
shaderc v2025.4-1-g5dcb000
shine 3.1.1
snappy 1.2.2
speex Speex-1.2.1-51-g0589522
srt v1.5.5-rc.0a-5-gc09532f8
SVT-AV1 v3.1.0-194-g090bdfba
twolame 0.4.0
uavs3d v1.1-47-g1fd0491
VAAPI 2.23.0.
vidstab v1.1.1-20-g4bd81e3
vmaf v3.0.0-122-ge0d9b82d
vo-amrwbenc 0.1.3
vorbis v1.3.7-21-g851cce99
VPL 2.15
vpx v1.15.2-142-g9a7674e1a
vulkan-loader v1.4.333-2-g655909f
vvenc v1.13.1-201-g45e89ef
whisper.cpp 1.8.2
x264 v0.165.3223
x265 4.1-212-g9e551a994
xavs2 1.4
xevd 0.5.0
xeve 0.5.1
xvid v1.3.7
zeromq 4.3.5
zimg release-3.0.6-211-gdf9c147
zvbi v0.2.44-4-g41477c9

