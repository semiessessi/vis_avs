#include <windows.h>
#include "r_defs.h"
#include "resource.h"
#include <xmmintrin.h>
#include <emmintrin.h>


#define MOD_NAME "Trans / Normalise"
#define UNIQUEIDSTRING "Trans: Normalise"
/* This is a tradeoff heuristic used in the SSE2 implementation. Checking more often
(->lower number) will increase total cost for scan, but will exit (slightly) earlier, if
possible. Assuming Normalise is only used if necessary most of the time (i.e. image
colors not already spanning the full brightness range), it's probably safe to use a
fairly large number here.
Must be divisible by four, or early-exit check condition will never be hit! */
#define TRY_BAIL_SCAN_EARLY_EVERY_NTH_PIXEL 512
#define NUM_COLOR_VALUES 256  // 2 ^ BITS_PER_CHANNEL (i.e. 8)

class C_Normalise : public C_RBASE {
    public:
        C_Normalise();
        virtual ~C_Normalise();
        virtual int render(char visdata[2][2][576], int isBeat, int *framebuffer, int *fbout, int w, int);
        virtual HWND conf(HINSTANCE hInstance, HWND hwndParent);
        virtual char *get_desc();
        virtual void load_config(unsigned char *data, int len);
        virtual int  save_config(unsigned char *data);
        bool enabled;

    protected:
        int scan_min_max(int* framebuffer, int fb_length, unsigned char * max_out, unsigned char * min_out);
        int scan_min_max_sse2(int* framebuffer, int fb_length, unsigned char * max_out, unsigned char * min_out);
        void make_scale_table(unsigned char max, unsigned char min, unsigned int scale_table_out[]);
        void apply(int* framebuffer, int fb_length, unsigned int scale_table[]);
};

static C_Normalise* g_ConfigThis;
static HINSTANCE g_hDllInstance;

static BOOL CALLBACK g_DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam) {
    switch (uMsg) {
        case WM_INITDIALOG:
            CheckDlgButton(hwndDlg, IDC_NORMALISE_ENABLED, g_ConfigThis->enabled);
            return 1;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_NORMALISE_ENABLED) {
                g_ConfigThis->enabled = IsDlgButtonChecked(hwndDlg, IDC_NORMALISE_ENABLED) == 1;
                return 0;
            }
    }
    return 0;
}

C_Normalise::C_Normalise() {
    this->enabled = true;
}

C_Normalise::~C_Normalise() {}

int C_Normalise::scan_min_max(
    int* framebuffer, int fb_length, unsigned char * max_out, unsigned char * min_out
) {
    for(int i = 0; i < fb_length; i++) {
        unsigned char r = framebuffer[i] & 0xff;
        unsigned char g = (framebuffer[i] & 0xff00) >> 8;
        unsigned char b = (framebuffer[i] & 0xff0000) >> 16;
        unsigned char rg_max = r > g ? r : g;
        unsigned char max_channel = rg_max > b ? rg_max : b;
        if(max_channel > *max_out) {
            *max_out = max_channel;
        }
        unsigned char rg_min = r < g ? r : g;
        unsigned char min_channel = rg_min < b ? rg_min : b;
        if(min_channel < *min_out) {
            *min_out = min_channel;
        }
        // We don't use TRY_BAIL_SCAN_EARLY_EVERY_NTH_PIXEL here because it's so cheap.
        if(*max_out == 0xff && *min_out == 0) {
            return 1;
        }
    }
    return 0;
}


int C_Normalise::scan_min_max_sse2(
    int* framebuffer, int fb_length, unsigned char * max_out, unsigned char * min_out
) {
    /*
    Assume the framebuffer length is a multiple of 4, which is a given since the AVS
    window can only have a multiple-4 width. Go four 32-bit pixels at a time, and
    operate on 128bit numbers.
    */
    __m128i max4 = _mm_set_epi32(0, 0, 0, 0);
    __m128i min4 = _mm_set_epi32(0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff);
    __m128i white = _mm_set_epi32(0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff);
    __m128i black = _mm_set_epi32(0x00000000, 0x00000000, 0x00000000, 0x00000000);
    unsigned int cmp_mask = 0b0111011101110111;  // ignore 4th channel comparison results

    for(int i = 0; i < fb_length; i += 4) {
        __m128i four_px = _mm_loadu_si128((__m128i*)&framebuffer[i]);
        max4 = _mm_max_epu8(max4, four_px);
        min4 = _mm_min_epu8(min4, four_px);

        if(i % TRY_BAIL_SCAN_EARLY_EVERY_NTH_PIXEL == 0) {
            __m128i maxed_out = _mm_cmpeq_epi8(max4, white);
            __m128i bottomed_out = _mm_cmpeq_epi8(min4, black);
            unsigned int has_maxed_out = _mm_movemask_epi8(maxed_out) & cmp_mask;
            unsigned int has_bottomed_out = _mm_movemask_epi8(bottomed_out) & cmp_mask;
            if(has_maxed_out != 0 && has_bottomed_out != 0) {
                return 1;
            }
        }
    }

    unsigned char four_px_max[16];
    unsigned char four_px_min[16];
    _mm_store_si128((__m128i*)four_px_max, max4);
    _mm_store_si128((__m128i*)four_px_min, min4);
    for(int i = 0; i < 16; i++) {
        *max_out = four_px_max[i] > *max_out ? four_px_max[i] : *max_out;
        *min_out = four_px_min[i] < *min_out ? four_px_min[i] : *min_out;
        if(i % 4 == 2) i++;
    }
    return 0;
}


void C_Normalise::make_scale_table(
    unsigned char max, unsigned char min, unsigned int scale_table_out[]
) {
    if(max - min) {
        float diff = (NUM_COLOR_VALUES - 1) / (float)(max - min);
        for(int i = min, j=0; i <= max; i++, j++) {
            scale_table_out[i] = j * diff;
        }
    } else {
        memset(scale_table_out, 0, NUM_COLOR_VALUES * sizeof(int));
    }
}

void C_Normalise::apply(int* framebuffer, int fb_length, unsigned int scale_table[]) {
    for(int i = 0; i < fb_length; i++) {
        unsigned char r = framebuffer[i] & 0xff;
        unsigned char g = (framebuffer[i] & 0xff00) >> 8;
        unsigned char b = (framebuffer[i] & 0xff0000) >> 16;
        framebuffer[i] = scale_table[r]
                         | (scale_table[g] << 8)
                         | (scale_table[b] << 16);
    }
}

int C_Normalise::render(char visdata[2][2][576], int isBeat, int *framebuffer, int *fbout, int w, int h) {
    unsigned char max = 0;
    unsigned char min = 0xff;
    unsigned int scale_table[NUM_COLOR_VALUES];
    bool no_scaling_needed;

    if(!this->enabled) return 0;
    no_scaling_needed = this->scan_min_max_sse2(framebuffer, w * h, &max, &min);
    if(no_scaling_needed) return 0;
    this->make_scale_table(max, min, scale_table);
    this->apply(framebuffer, w * h, scale_table);
    return 0;
}

HWND C_Normalise::conf(HINSTANCE hInstance, HWND hwndParent) {
    g_ConfigThis = this;
    return CreateDialog(hInstance, MAKEINTRESOURCE(IDD_CFG_NORMALISE), hwndParent, g_DlgProc);
}

char *C_Normalise::get_desc(void) {
    return MOD_NAME;
}

#define GET_INT() (data[pos]|(data[pos+1]<<8)|(data[pos+2]<<16)|(data[pos+3]<<24))
void C_Normalise::load_config(unsigned char *data, int len) {
    int pos = 0;
    if (len - pos >= 4) {
        this->enabled = GET_INT();
    } else {
        this->enabled = false;
    }
}

#define PUT_INT(y) data[pos]=(y)&255; data[pos+1]=((y)>>8)&255; data[pos+2]=((y)>>16)&255; data[pos+3]=((y)>>24)&255
int  C_Normalise::save_config(unsigned char *data) {
    int pos = 0;
    PUT_INT((int)this->enabled);
    return 4;
}

C_RBASE *R_Normalise(char *desc) {
    if (desc) {
        strcpy(desc,MOD_NAME); 
        return NULL; 
    }
    return (C_RBASE *) new C_Normalise();
}
