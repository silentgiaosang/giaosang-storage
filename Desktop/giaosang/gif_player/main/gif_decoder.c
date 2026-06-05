#include "gif_decoder.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "gif";

struct gif_decoder {
    const uint8_t *d; size_t len;
    uint16_t w, h;
    uint8_t *gct; int gct_n;
    int fc; size_t *foffs, *goffs;
    int loop; bool ok;
};

// ---- LZW decompressor ----
static uint8_t *lzw(const uint8_t *in, size_t in_len, size_t *out_len)
{
    if (!in_len) { *out_len=0; return NULL; }
    int ms = in[0]; if (ms<2) ms=2;
    int clr=1<<ms, eoi=clr+1, bits=ms+1, next=eoi+1;
    struct { int16_t p; uint8_t s; } dict[4096];
    for (int i=0; i<256; i++) { dict[i].p=-1; dict[i].s=i; }

    size_t bp=8, cap=4096, olen=0;
    uint8_t *out=malloc(cap);
    int old=-1, first=0;

    while (1) {
        int code=0;
        for (int i=0; i<bits; i++) {
            size_t bp2 = bp>>3; if (bp2>=in_len) { code=-1; break; }
            code |= ((in[bp2]>>(bp&7))&1)<<i; bp++;
        }
        if (code<0||code==eoi) break;
        if (code==clr) { bits=ms+1; next=eoi+1; old=-1; continue; }
        if (old<0) {
            if (code>=256) break;
            if (olen+1>cap) { cap*=2; out=realloc(out,cap); }
            out[olen++]=code; first=code; old=code; continue;
        }

        uint8_t stack[4096]; int sp=0; int c=code;
        if (code==next) { c=old; stack[sp++]=first; }
        while (c>=0&&sp<4096) {
            if (c<256) { stack[sp++]=c; break; }
            if (c>=next) break;
            stack[sp++]=dict[c].s; c=dict[c].p;
        }
        if (olen+sp>cap) { cap=olen+sp+4096; out=realloc(out,cap); }
        for (int i=sp-1; i>=0; i--) out[olen++]=stack[i];

        if (next<4096) { dict[next].p=old; dict[next].s=first; next++;
            if (next>(1<<bits)&&bits<12) bits++; }
        int idx=code; while (idx>=256) idx=dict[idx].p;
        first=idx; old=code;
    }
    *out_len=olen; return out;
}

// ---- GIF parser ----
static gif_result_t parse(gif_decoder_t *g)
{
    const uint8_t *d=g->d; size_t len=g->len;
    if (len<14) return GIF_ERR_CORRUPT;
    if (d[0]!='G'||d[1]!='I'||d[2]!='F') return GIF_ERR_INVALID_SIG;
    g->w=d[6]|(d[7]<<8); g->h=d[8]|(d[9]<<8);
    uint8_t p=d[10]; int gct_f=(p>>7)&1, gct_s=gct_f?(1<<((p&7)+1)):0;

    size_t pos=13;
    if (gct_f) { g->gct=malloc(gct_s*3); memcpy(g->gct,d+pos,gct_s*3); g->gct_n=gct_s; pos+=gct_s*3; }

    int cap=16; g->foffs=malloc(cap*sizeof(size_t)); g->goffs=malloc(cap*sizeof(size_t));
    g->fc=0; size_t lgce=0;

    while (pos<len) {
        uint8_t t=d[pos];
        if (t==0x3B) break;
        if (t==0x21) {
            if (pos+1<len&&d[pos+1]==0xF9) lgce=pos;
            pos+=2; while(pos<len){uint8_t bs=d[pos];if(!bs)break;pos+=1+bs;} pos++;
        } else if (t==0x2C) {
            if(g->fc>=cap){cap*=2;g->foffs=realloc(g->foffs,cap*sizeof(size_t));g->goffs=realloc(g->goffs,cap*sizeof(size_t));}
            g->foffs[g->fc]=pos; g->goffs[g->fc]=lgce; g->fc++; lgce=0;
            int lct_s=(d[pos+9]>>7)&1?(1<<((d[pos+9]&7)+1)):0;
            pos+=10+lct_s*3;
            if(pos<len){pos++;while(pos<len){uint8_t bs=d[pos];if(!bs)break;pos+=1+bs;}pos++;}
        } else pos++;
    }

    // NETSCAPE loop count
    pos=13+(gct_f?gct_s*3:0);
    while(pos+2<len){
        if(d[pos]==0x21&&d[pos+1]==0xFF){
            pos+=2;uint8_t bs=d[pos];
            if(bs==11&&memcmp(d+pos+1,"NETSCAPE2.0",11)==0){ pos+=12;
                if(pos+3<len&&d[pos]==3&&d[pos+1]==1) g->loop=d[pos+2]|(d[pos+3]<<8); }
            break;
        } if(d[pos]==0x2C||d[pos]==0x3B) break; pos++;
    }
    g->ok=true; return GIF_OK;
}

// ---- Public API ----
gif_result_t gif_decoder_init(gif_decoder_t **out,const uint8_t *data,size_t len){
    gif_decoder_t *g=calloc(1,sizeof(gif_decoder_t)); g->d=data; g->len=len;
    gif_result_t r=parse(g);
    if(r){free(g->gct);free(g->foffs);free(g->goffs);free(g);return r;}
    *out=g; return GIF_OK;
}

gif_result_t gif_get_frame_info(gif_decoder_t *g,int n,gif_frame_info_t *inf){
    if(n<0||n>=g->fc) return GIF_ERR_CORRUPT;
    memset(inf,0,sizeof(*inf)); inf->delay_ms=100;
    size_t go=g->goffs[n];
    if(go&&g->d[go]==0x21&&g->d[go+1]==0xF9){
        uint8_t p=g->d[go+3]; inf->disposal=(p>>2)&7; inf->has_transparent=p&1;
        inf->delay_ms=(g->d[go+4]|(g->d[go+5]<<8))*10;
        if(inf->delay_ms<20) inf->delay_ms=100;
        if(inf->has_transparent) inf->transparent_idx=g->d[go+6];
    }
    size_t fo=g->foffs[n];
    inf->left=g->d[fo+1]|(g->d[fo+2]<<8); inf->top=g->d[fo+3]|(g->d[fo+4]<<8);
    inf->width=g->d[fo+5]|(g->d[fo+6]<<8); inf->height=g->d[fo+7]|(g->d[fo+8]<<8);
    inf->interlaced=(g->d[fo+9]>>6)&1; return GIF_OK;
}

gif_result_t gif_decode_frame(gif_decoder_t *g,int n,uint16_t *canvas,int cw,int ch){
    if(!g->ok||n<0||n>=g->fc) return GIF_ERR_CORRUPT;
    gif_frame_info_t inf; gif_result_t r=gif_get_frame_info(g,n,&inf); if(r) return r;
    const uint8_t *d=g->d; size_t fo=g->foffs[n];
    int lct_n=(d[fo+9]>>7)&1?(1<<((d[fo+9]&7)+1)):0;
    size_t lzw_pos=fo+10+lct_n*3; if(lzw_pos>=g->len) return GIF_ERR_CORRUPT;
    int ms=d[lzw_pos]; if(ms<2) ms=2;

    size_t pos=lzw_pos+1,cap=4096,llen=0; uint8_t *ld=malloc(cap);
    while(pos<g->len){uint8_t bs=d[pos];if(!bs)break;pos++;
        if(llen+bs>cap){cap=llen+bs+1024;ld=realloc(ld,cap);}
        memcpy(ld+llen,d+pos,bs);llen+=bs;pos+=bs;}
    uint8_t *full=malloc(llen+1); full[0]=ms; memcpy(full+1,ld,llen); free(ld);
    size_t dlen=0; uint8_t *dec=lzw(full,llen+1,&dlen); free(full);
    if(!dec) return GIF_ERR_CORRUPT;

    uint8_t lct[768],*ct;int cn;
    if(lct_n){memcpy(lct,d+fo+10,lct_n*3);ct=lct;cn=lct_n;}else{ct=g->gct;cn=g->gct_n;}
    if(!ct){free(dec);return GIF_ERR_CORRUPT;}

    int rm[inf.height];
    if(inf.interlaced){int k=0;for(int y=0;y<inf.height;y+=8)rm[k++]=y;
        for(int y=4;y<inf.height;y+=8)rm[k++]=y;for(int y=2;y<inf.height;y+=4)rm[k++]=y;
        for(int y=1;y<inf.height;y+=2)rm[k++]=y;}
    else for(int i=0;i<inf.height;i++) rm[i]=i;

    for(int r=0;r<inf.height;r++){
        int cy=inf.top+rm[r]; if(cy>=ch) continue;
        for(int x=0;x<inf.width;x++){
            int cx=inf.left+x; if(cx>=cw) continue;
            size_t idx=(size_t)r*inf.width+x; if(idx>=dlen) break;
            uint8_t px=dec[idx];
            if(inf.has_transparent&&px==inf.transparent_idx) continue;
            if(px<cn){uint16_t c=((ct[px*3]>>3)<<11)|((ct[px*3+1]>>2)<<5)|(ct[px*3+2]>>3);
                canvas[cy*cw+cx]=c;}
        }
    }
    free(dec); return GIF_OK;
}

int gif_get_frame_count(gif_decoder_t *g){return g->fc;}
int gif_get_loop_count(gif_decoder_t *g){return g->loop;}
void gif_get_size(gif_decoder_t *g,uint16_t *w,uint16_t *h){*w=g->w;*h=g->h;}
void gif_decoder_deinit(gif_decoder_t *g){if(g){free(g->gct);free(g->foffs);free(g->goffs);free(g);}}
