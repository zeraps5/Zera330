/* Extrai os quadros e o áudio de um vídeo com o Media Foundation do Windows
 * (ferramenta offline: o zera330 não depende dela em tempo de execução).
 * Saída: <prefixo>_WxH.rgb (quadros RGB24 em sequência), <prefixo>.wav (PCM
 * 16 bits) e <prefixo>.txt (largura, altura, quadros, fps).
 * Uso: intro_extract <video.mp4> <prefixo>
 * Build (x64 Native Tools): cl /O2 intro_extract.c mfplat.lib mfreadwrite.lib mfuuid.lib ole32.lib */
#define COBJMACROS
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <stdio.h>
#include <stdlib.h>

static void wav_header(FILE *f, unsigned rate, unsigned ch, unsigned bytes) {
    unsigned br = rate * ch * 2u, v;
    unsigned short s;
    fwrite("RIFF", 1, 4, f); v = 36u + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); s = 1; fwrite(&s, 2, 1, f); s = (unsigned short)ch; fwrite(&s, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f); s = (unsigned short)(ch * 2u); fwrite(&s, 2, 1, f);
    s = 16; fwrite(&s, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
}

int main(int argc, char **argv) {
    if (argc < 3) { printf("uso: %s <video.mp4> <prefixo>\n", argv[0]); return 2; }
    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, wpath, MAX_PATH);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, 0);

    IMFAttributes *attr = NULL;
    MFCreateAttributes(&attr, 1);
    IMFAttributes_SetUINT32(attr, &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    IMFSourceReader *rd = NULL;
    if (FAILED(MFCreateSourceReaderFromURL(wpath, attr, &rd))) { printf("erro: nao abriu o video\n"); return 1; }

    IMFMediaType *vt = NULL, *at = NULL;
    MFCreateMediaType(&vt);
    IMFMediaType_SetGUID(vt, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    IMFMediaType_SetGUID(vt, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
    if (FAILED(IMFSourceReader_SetCurrentMediaType(rd, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, vt))) {
        printf("erro: sem conversao para RGB32\n"); return 1;
    }
    MFCreateMediaType(&at);
    IMFMediaType_SetGUID(at, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    IMFMediaType_SetGUID(at, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
    IMFMediaType_SetUINT32(at, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    int has_audio = SUCCEEDED(IMFSourceReader_SetCurrentMediaType(rd, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, at));

    IMFMediaType *cur = NULL;
    IMFSourceReader_GetCurrentMediaType(rd, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur);
    UINT32 w = 0, h = 0, fn = 0, fd = 1;
    UINT64 pk = 0; /* MFGetAttributeSize/Ratio só existem em C++ */
    if (SUCCEEDED(IMFMediaType_GetUINT64(cur, &MF_MT_FRAME_SIZE, &pk))) { w = (UINT32)(pk >> 32); h = (UINT32)pk; }
    if (SUCCEEDED(IMFMediaType_GetUINT64(cur, &MF_MT_FRAME_RATE, &pk))) { fn = (UINT32)(pk >> 32); fd = (UINT32)pk; }
    UINT32 rate = 0, ch = 0;
    if (has_audio) {
        IMFMediaType *ac = NULL;
        IMFSourceReader_GetCurrentMediaType(rd, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &ac);
        IMFMediaType_GetUINT32(ac, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        IMFMediaType_GetUINT32(ac, &MF_MT_AUDIO_NUM_CHANNELS, &ch);
    }

    char name[512];
    snprintf(name, sizeof(name), "%s_%ux%u.rgb", argv[2], w, h);
    FILE *fv = fopen(name, "wb");
    snprintf(name, sizeof(name), "%s.wav", argv[2]);
    FILE *fa = has_audio ? fopen(name, "wb") : NULL;
    if (fa) wav_header(fa, rate, ch, 0);
    unsigned char *row = malloc((size_t)w * 3u);
    unsigned frames = 0, abytes = 0;
    int video_done = 0, audio_done = !has_audio;

    while (!video_done || !audio_done) {
        DWORD stream = 0, flags = 0; LONGLONG ts = 0; IMFSample *s = NULL;
        if (FAILED(IMFSourceReader_ReadSample(rd, (DWORD)MF_SOURCE_READER_ANY_STREAM, 0, &stream, &flags, &ts, &s))) break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            IMFMediaType *t = NULL; GUID major;
            IMFSourceReader_GetCurrentMediaType(rd, stream, &t);
            IMFMediaType_GetGUID(t, &MF_MT_MAJOR_TYPE, &major);
            if (IsEqualGUID(&major, &MFMediaType_Video)) video_done = 1; else audio_done = 1;
        }
        if (!s) continue;
        IMFMediaBuffer *b = NULL; BYTE *p = NULL; DWORD len = 0;
        IMFSample_ConvertToContiguousBuffer(s, &b);
        IMFMediaBuffer_Lock(b, &p, NULL, &len);
        IMFMediaType *t = NULL; GUID major;
        IMFSourceReader_GetCurrentMediaType(rd, stream, &t);
        IMFMediaType_GetGUID(t, &MF_MT_MAJOR_TYPE, &major);
        if (IsEqualGUID(&major, &MFMediaType_Video)) {
            /* RGB32 do MF é BGRX de baixo pra cima só se o stride for negativo;
             * com processamento de vídeo vem de cima pra baixo. */
            for (UINT32 y = 0; y < h; y++) {
                const BYTE *src = p + (size_t)y * w * 4u;
                for (UINT32 x = 0; x < w; x++) { row[x*3] = src[x*4+2]; row[x*3+1] = src[x*4+1]; row[x*3+2] = src[x*4]; }
                fwrite(row, 3, w, fv);
            }
            frames++;
        } else if (fa) {
            fwrite(p, 1, len, fa); abytes += len;
        }
        IMFMediaBuffer_Unlock(b); IMFMediaBuffer_Release(b); IMFSample_Release(s);
    }
    fclose(fv);
    if (fa) { fseek(fa, 0, SEEK_SET); wav_header(fa, rate, ch, abytes); fclose(fa); }
    snprintf(name, sizeof(name), "%s.txt", argv[2]);
    FILE *fi = fopen(name, "w");
    fprintf(fi, "%u %u %u %u %u %u %u\n", w, h, frames, fn, fd, rate, ch);
    fclose(fi);
    printf("ok: %ux%u, %u quadros a %u/%u fps; audio %s (%u Hz, %u canais, %u bytes)\n",
           w, h, frames, fn, fd, has_audio ? "sim" : "nao", rate, ch, abytes);
    MFShutdown();
    return 0;
}
