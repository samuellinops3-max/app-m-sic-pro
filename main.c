// BR MUSIC - TOCA WAV DE VERDADE DENTRO DO APP!
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>
#include <dirent.h>
#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <rsx/rsx.h>
#include <io/pad.h>
#include <audio/audio.h>

#define CB_SIZE   (0x100000)
#define HOST_SIZE (32 * 1024 * 1024)
#define MAXSONGS 500

typedef struct { char name[128]; char path[256]; } Song;
static Song songs[MAXSONGS];
static int songCount = 0;
static int selected = 0;
static int playingIdx = -1;
static int playing = 0;
static int playTime = 0;
static int playDur = 180;

static gcmContextData *ctx;
static u32 W, H;
static u32 *fb[2];
static u32 fboff[2];
static u32 *back;
static int cur = 0, first_flip = 1;
static volatile int running = 1;

static AudioPort audioPort = 0;
static int audioOpen = 0;
static FILE *wavFile = NULL;
static int wavDataLeft = 0;

static void sysutil_cb(u64 status, u64 param, void *usrdata){
    (void)param; (void)usrdata;
    if(status == SYSUTIL_EXIT_GAME) running = 0;
}
static void init_screen(void){
    void *host = memalign(1024*1024, HOST_SIZE);
    rsxInit(&ctx, CB_SIZE, HOST_SIZE, host);
    videoState st; videoGetState(VIDEO_PRIMARY, 0, &st);
    u32 resid = st.displayMode.resolution;
    if(videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720, VIDEO_ASPECT_AUTO, 0))
        resid = VIDEO_RESOLUTION_720;
    videoResolution res; videoGetResolution(resid, &res);
    W = res.width; H = res.height;
    videoConfiguration vc; memset(&vc,0,sizeof(vc));
    vc.resolution = resid; vc.format = VIDEO_BUFFER_FORMAT_XRGB; vc.aspect = VIDEO_ASPECT_AUTO; vc.pitch = W*4;
    videoConfigure(VIDEO_PRIMARY, &vc, NULL, 0);
    videoGetState(VIDEO_PRIMARY, 0, &st);
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    for(int i=0;i<2;i++){ fb[i]=(u32*)rsxMemalign(64, W*H*4); gcmAddressToOffset(fb[i], &fboff[i]); gcmSetDisplayBuffer(i, fboff[i], W*4, W, H); }
    back=(u32*)memalign(64, W*H*4); memset(back,0,W*H*4);
    gcmResetFlipStatus();
}
static void wait_flip(void){ while(gcmGetFlipStatus()!=0) usleep(200); gcmResetFlipStatus(); }
static void present(void){
    if(!first_flip) wait_flip(); else gcmResetFlipStatus();
    memcpy(fb[cur], back, W*H*4);
    gcmSetFlip(ctx, cur); rsxFlushBuffer(ctx); gcmSetWaitFlip(ctx);
    cur ^= 1; first_flip = 0;
}
static void fill_rect(int x,int y,int w,int h,u32 c){
    int x0=x<0?0:x, y0=y<0?0:y; int x1=x+w, y1=y+h;
    if(x1>(int)W) x1=W; if(y1>(int)H) y1=H;
    for(int yy=y0;yy<y1;yy++){ u32 *p=back+yy*W; for(int xx=x0;xx<x1;xx++) p[xx]=c; }
}
static void circle(int cx,int cy,int r,u32 c){
    if(r<1) r=1;
    for(int dy=-r;dy<=r;dy++){ int y=cy+dy; if(y<0||y>=(int)H) continue;
        int hw=(int)sqrtf((float)(r*r-dy*dy)); int x0=cx-hw, x1=cx+hw;
        if(x0<0) x0=0; if(x1>=(int)W) x1=W-1; u32 *p=back+y*W; for(int x=x0;x<=x1;x++) p[x]=c; }
}
typedef struct { char c; const char *p; } Glyph;
static const Glyph glyphs[] = {
    {'0',"###" "#.#" "#.#" "#.#" "###"}, {'1',".#." "##." ".#." ".#." "###"},
    {'2',"###" "..#" "###" "#.." "###"}, {'3',"###" "..#" "###" "..#" "###"},
    {'4',"#.#" "#.#" "###" "..#" "..#"}, {'5',"###" "#.." "###" "..#" "###"},
    {'6',"###" "#.." "###" "#.#" "###"}, {'7',"###" "..#" "..#" "..#" "..#"},
    {'8',"###" "#.#" "###" "#.#" "###"}, {'9',"###" "#.#" "###" "..#" "###"},
    {'A',".#." "#.#" "###" "#.#" "#.#"}, {'B',"##." "#.#" "##." "#.#" "##."},
    {'C',"###" "#.." "#.." "#.." "###"}, {'D',"##." "#.#" "#.#" "#.#" "##."},
    {'E',"###" "#.." "###" "#.." "###"}, {'F',"###" "#.." "###" "#.." "#.."},
    {'G',"###" "#.." "#.#" "#.#" "###"}, {'H',"#.#" "#.#" "###" "#.#" "#.#"},
    {'I',"###" ".#." ".#." ".#." "###"}, {'J',"###" "..#" "..#" "#.#" "###"},
    {'K',"#.#" "#.#" "##." "#.#" "#.#"}, {'L',"#.." "#.." "#.." "#.." "###"},
    {'M',"#.#" "###" "###" "#.#" "#.#"}, {'N',"#.#" "###" "###" "#.#" "#.#"},
    {'O',"###" "#.#" "#.#" "#.#" "###"}, {'P',"###" "#.#" "###" "#.." "#.."},
    {'Q',"###" "#.#" "#.#" ".#." ".##"}, {'R',"##." "#.#" "##." "#.#" "#.#"},
    {'S',"###" "#.." "###" "..#" "###"}, {'T',"###" ".#." ".#." ".#." ".#."},
    {'U',"#.#" "#.#" "#.#" "#.#" "###"}, {'V',"#.#" "#.#" "#.#" "#.#" ".#."},
    {'W',"#.#" "#.#" "###" "###" "#.#"}, {'X',"#.#" "#.#" ".#." "#.#" "#.#"},
    {'Y',"#.#" "#.#" ".#." ".#." ".#."}, {'Z',"###" "..#" ".#." "#.." "###"},
    {' ',"..." "..." "..." "..." "..."}, {'.',"..." "..." "..." "..." "##."},
    {'-', "..." "..." "###" "..." "..."}, {'_', "..." "..." "..." "..." "###"},
    {'/',"..#" "..#" ".#." "#.." "#.."}, {':',"..." "##." "..." "##." "..."},
    {'(',".##" "#.." "#.." "#.." ".##"}, {')',"##." "..#" "..#" "..#" "##."},
    {'!',"##." "##." "##." "..." "##."}, {'?',"###" "..#" ".#." "..." ".#."},
};
static const char *find_glyph(char c){
    if(c>='a'&&c<='z') c-=32;
    for(unsigned i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++) if(glyphs[i].c==c) return glyphs[i].p;
    return NULL;
}
static int text_width(const char *s,int sc){ return (int)strlen(s)*4*sc - sc; }
static void draw_text(const char *s,int x,int y,int sc,u32 c){
    for(;*s;s++){ const char *g=find_glyph(*s);
        if(g) for(int r=0;r<5;r++) for(int k=0;k<3;k++) if(g[r*3+k]=='#') fill_rect(x+k*sc,y+r*sc,sc,sc,c);
        x+=4*sc; }
}
static void draw_text_clip(const char *s,int x,int y,int sc,u32 c,int maxChars){
    char buf[128]; strncpy(buf,s,sizeof(buf)-1); buf[sizeof(buf)-1]=0;
    if((int)strlen(buf)>maxChars){ buf[maxChars-3]=0; strcat(buf,"..."); }
    draw_text(buf,x,y,sc,c);
}
static void scan_folder(const char *folder){
    DIR *d=opendir(folder); if(!d) return;
    struct dirent *ent;
    while((ent=readdir(d))!=NULL && songCount<MAXSONGS){
        if(ent->d_name[0]=='.') continue;
        const char *e=strrchr(ent->d_name,'.');
        if(!e || strcasecmp(e,".wav")!=0) continue;
        Song *s=&songs[songCount];
        strncpy(s->name,ent->d_name,sizeof(s->name)-1); s->name[sizeof(s->name)-1]=0;
        char *dot=strrchr(s->name,'.'); if(dot) *dot=0;
        snprintf(s->path,sizeof(s->path),"%s/%s",folder,ent->d_name);
        songCount++;
    }
    closedir(d);
}
static void scan_all(void){
    songCount=0; selected=0;
    const char *fixas[][2] = {
        {"Smack That (feat. Eminem)", "/dev_hdd0/game/BRMU00001/USRDIR/music/Smack That (feat. Eminem).wav"},
        {"Losing My Religion", "/dev_hdd0/game/BRMU00001/USRDIR/music/Losing My Religion.wav"},
        {"Time", "/dev_hdd0/game/BRMU00001/USRDIR/music/Time.wav"},
        {"Yeah! (feat. Lil Jon & Ludacris)", "/dev_hdd0/game/BRMU00001/USRDIR/music/Yeah! (feat. Lil Jon & Ludacris).wav"},
        {"Yeah! (feat. Lil Jon)", "/dev_hdd0/game/BRMU00001/USRDIR/music/Yeah! (feat. Lil Jon).wav"},
    };
    for(int i=0;i<5;i++){
        FILE *f=fopen(fixas[i][1],"rb");
        if(!f) continue; fclose(f);
        Song *s=&songs[songCount];
        strncpy(s->name, fixas[i][0], sizeof(s->name)-1);
        strncpy(s->path, fixas[i][1], sizeof(s->path)-1);
        songCount++;
    }
    scan_folder("/dev_hdd0/music");
    scan_folder("/dev_usb000/music");
}
static void stop_music(void){
    if(wavFile){ fclose(wavFile); wavFile=NULL; }
    if(audioOpen){ audioPortClose(audioPort); audioOpen=0; }
    playing=0; playingIdx=-1; playTime=0;
}
static int start_music(const char *path){
    stop_music();
    wavFile = fopen(path, "rb");
    if(!wavFile) return 0;
    char hdr[44]; fread(hdr, 1, 44, wavFile);
    fseek(wavFile, 44, SEEK_SET);
    fseek(wavFile, 0, SEEK_END);
    long total = ftell(wavFile) - 44;
    fseek(wavFile, 44, SEEK_SET);
    wavDataLeft = total;
    playDur = total / (44100 * 4);
    if(playDur < 1) playDur = 180;
    AudioPortParam p; memset(&p,0,sizeof(p));
    p.numChannels = 2;
    p.numBlocks = 8;
    p.attrib = 0;
    p.level = 1.0f;
    if(audioPortOpen(&p, &audioPort) != 0){ fclose(wavFile); wavFile=NULL; return 0; }
    audioOpen = 1;
    playing = 1; playTime = 0;
    return 1;
}
static void pump_audio(void){
    if(!playing || !wavFile || !audioOpen) return;
    short pcm[2048];
    size_t read = fread(pcm, 2, 2048, wavFile);
    if(read == 0){
        int next = (playingIdx+1) % songCount;
        if(songCount>0) { playingIdx = next; start_music(songs[next].path); }
        else stop_music();
        return;
    }
    float out[2048];
    for(size_t i=0;i<read;i++) out[i] = pcm[i] / 32768.0f;
    audioPortSend(audioPort, out);
    static int cnt=0; cnt++;
    if(cnt>=43){ cnt=0; playTime++; }
}
static void fmt_time(char *out,int sec){ sprintf(out,"%d:%02d",sec/60,sec%60); }
static void render(void){
    float sc=(float)W/1280.0f; int ui=(int)(4*sc+0.5f); if(ui<3) ui=3;
    u32 n=W*H; for(u32 i=0;i<n;i++) back[i]=0x0008080F;
    fill_rect(0,0,W,90,0x000A0A14);
    draw_text("BR", 24, 18, ui+3, 0x00FFFFFF);
    draw_text("MUSIC", 24+text_width("BR ",ui+3), 18, ui+3, 0x001ED760);
    char cnt[64]; sprintf(cnt,"%d MUSICAS", songCount);
    draw_text(cnt, W - text_width(cnt,ui) - 24, 28, ui, 0x00809BBF);
    draw_text("CIMA/BAIXO NAVEGAR  X TOCAR  QUADRADO PARAR  TRIANGULO ATUALIZAR", 24, 68, 2, 0x00666688);
    if(songCount==0){
        draw_text("NENHUMA MUSICA WAV ACHADA!", W/2 - text_width("NENHUMA MUSICA WAV ACHADA!",ui+2)/2, H/2-80, ui+2, 0x00FF4D4D);
        draw_text("CONVERTE MP3 PRA WAV E BOTA NA PASTA MUSIC!", W/2 - text_width("CONVERTE MP3 PRA WAV E BOTA NA PASTA MUSIC!",ui)/2, H/2-20, ui, 0x00AAAAAA);
    } else {
        int startY=110; int rowH=52; int visible=(H-110-150)/rowH;
        static int scroll=0;
        if(selected<scroll) scroll=selected; if(selected>=scroll+visible) scroll=selected-visible+1;
        for(int i=0;i<visible;i++){
            int idx=scroll+i; if(idx>=songCount) break;
            int y=startY+i*rowH;
            if(idx==selected) fill_rect(20,y,W-40,rowH-6,0x001A2A1A);
            else if(idx==playingIdx) fill_rect(20,y,W-40,rowH-6,0x000F1A2A);
            circle(52, y+22, 16, idx==playingIdx?0x001ED760:0x002A2A3A);
            draw_text(idx==playingIdx?"X":">", 46, y+12, 3, 0x00FFFFFF);
            char num[16]; sprintf(num,"%d.",idx+1);
            draw_text(num, 82, y+14, 3, 0x00809BBF);
            draw_text_clip(songs[idx].name, 140, y+14, 3, idx==selected?0x00FFFFFF:0x00CCCCCC, 42);
            if(idx==playingIdx) draw_text("TOCANDO", W-220, y+14, 3, 0x001ED760);
        }
        int py=H-130;
        fill_rect(0,py,W,130,0x0012121E);
        if(playingIdx>=0){
            draw_text_clip(songs[playingIdx].name, 24, py+16, ui+1, 0x00FFFFFF, 36);
            char t1[16], t2[16]; fmt_time(t1,playTime); fmt_time(t2,playDur);
            draw_text(t1, 24, py+58, ui, 0x00AAAAAA);
            int bx=110, bw=W-110-110;
            fill_rect(bx, py+62, bw, 10, 0x00333333);
            int pw = playDur>0 ? (bw*playTime/playDur) : 0;
            if(pw>0) fill_rect(bx, py+62, pw, 10, 0x001ED760);
            draw_text(t2, W-90, py+58, ui, 0x00AAAAAA);
            draw_text(playing?"|> TOCANDO - APERTA X PRA PAUSAR":"|| PAUSADO - APERTA X PRA TOCAR", 24, py+88, ui, playing?0x001ED760:0x00FFD23F);
        } else {
            draw_text("APERTA X NUMA MUSICA PRA TOCAR", 24, py+40, ui+1, 0x00809BBF);
        }
    }
}
int main(int argc,char **argv){
    (void)argc; (void)argv;
    init_screen();
    ioPadInit(7);
    sysUtilRegisterCallback(0, sysutil_cb, NULL);
    audioInit();
    scan_all();
    padInfo info; padData d;
    int prevUp=0, prevDown=0, prevCross=0, prevSquare=0, prevTri=0, prevStart=0;
    while(running){
        sysUtilCheckCallback();
        ioPadGetInfo(&info);
        int up=0, down=0, cross=0, square=0, tri=0, start=0;
        if(info.status[0]){ ioPadGetData(0,&d); up=d.BTN_UP; down=d.BTN_DOWN; cross=d.BTN_CROSS; square=d.BTN_SQUARE; tri=d.BTN_TRIANGLE; start=d.BTN_START; }
        if(up && !prevUp && songCount>0){ selected--; if(selected<0) selected=songCount-1; }
        if(down && !prevDown && songCount>0){ selected++; if(selected>=songCount) selected=0; }
        if(cross && !prevCross && songCount>0){
            if(playingIdx==selected){ playing=!playing; }
            else { playingIdx=selected; start_music(songs[playingIdx].path); }
        }
        if(square && !prevSquare){ stop_music(); }
        if(tri && !prevTri){ scan_all(); }
        if(start && !prevStart) break;
        prevUp=up; prevDown=down; prevCross=cross; prevSquare=square; prevTri=tri; prevStart=start;
        pump_audio();
        render();
        present();
        usleep(8000);
    }
    stop_music();
    return 0;
}