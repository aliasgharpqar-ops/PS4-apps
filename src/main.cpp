#include <stdint.h>
#include <stddef.h>

/*
    GAMECLUB PS4 - TEST BUILD

    اصلاحات:
    1. فرمت صحیح رنگ فریم‌بافر
    2. استفاده از VSync برای نمایش فریم
    3. بررسی خطاهای تخصیص حافظه
    4. جلوگیری از گیرکردن دائمی صفحه خوشامدگویی
    5. اصلاح راه‌اندازی کنترلر
    6. جلوگیری از خروج از محدوده فریم‌بافر
    7. حفظ رابط گرافیکی بنفش و آبی
*/

extern "C" {

    int32_t sceKernelAllocateDirectMemory(
        long, long, size_t, size_t, int, long*
    );

    int32_t sceKernelMapDirectMemory(
        void**, size_t, int, int, long, size_t
    );

    int32_t sceKernelReleaseDirectMemory(long, size_t);

    long sceKernelGetDirectMemorySize(void);

    int sceKernelUsleep(unsigned int);

    int32_t sceVideoOutOpen(
        int32_t, int32_t, int32_t, const void*
    );

    void sceVideoOutSetBufferAttribute(
        void*, uint32_t, uint32_t, uint32_t,
        uint32_t, uint32_t, uint32_t
    );

    int32_t sceVideoOutRegisterBuffers(
        int32_t, int32_t, void* const*,
        int32_t, const void*
    );

    int32_t sceVideoOutSubmitFlip(
        int32_t, int32_t, uint32_t, int64_t
    );

    int32_t sceVideoOutGetFlipStatus(
        int32_t, void*
    );

    int32_t sceVideoOutSetFlipRate(int32_t, int32_t);

    int32_t scePadInit(void);

    int32_t scePadOpen(
        int32_t, int32_t, int32_t, void*
    );

    int32_t scePadReadState(int32_t, void*);

    int32_t sceUserServiceInitialize(void*);

    int32_t sceUserServiceGetInitialUser(int32_t*);
}

/*
    اطلاعات وضعیت فریم‌بافر
*/

struct VideoBufAttr {
    int32_t format;
    int32_t tmode;
    int32_t aspect;

    uint32_t width;
    uint32_t height;
    uint32_t pitch;

    uint64_t reserved[2];
};

/*
    فضای کافی برای خروجی کنترلر
*/

struct PadState {
    uint32_t buttons;
    uint8_t rest[252];
};

/*
    ساختار وضعیت نمایش مطابق API ویدئوی OpenOrbis
*/

struct VideoFlipStatus {
    uint64_t num;
    uint64_t ptime;
    uint64_t stime;

    int64_t flipArg;

    uint64_t reserved[2];

    int32_t numGpuFlipPending;
    int32_t numFlipPending;
    int32_t currentBuffer;

    uint32_t reserved1;
};

/*
    تنظیمات اصلی
*/

static const int W = 1920;
static const int H = 1080;

static const size_t BYTES_PER_PIXEL = 4;
static const size_t BUFFER_SIZE =
    size_t(W) * size_t(H) * BYTES_PER_PIXEL;

static const size_t MEMORY_ALIGNMENT = 0x200000;

static uint32_t* buffers[2] = { nullptr, nullptr };

static int videoHandle = -1;
static int padHandle = -1;
static int currentBuffer = 0;

static long allocatedMemoryOffset = 0;
static size_t allocatedMemorySize = 0;

static int64_t frameSequence = 1;

static uint32_t previousButtons = 0;

/*
    رنگ‌ها

    در توابع رابط کاربری از قالب 0xFFRRGGBB
    استفاده می‌کنیم و موقع نوشتن پیکسل،
    آن را به قالب مورد استفاده فریم‌بافر تبدیل می‌کنیم.
*/

static const uint32_t BG     = 0xFF0B0D18;
static const uint32_t PANEL  = 0xFF202538;
static const uint32_t PANEL2 = 0xFF2A3045;

static const uint32_t PURPLE = 0xFFA855F7;
static const uint32_t PINK   = 0xFFFF4D8D;
static const uint32_t CYAN   = 0xFF35D8F2;

static const uint32_t WHITE  = 0xFFF5F7FF;
static const uint32_t MUTED  = 0xFF98A4B8;
static const uint32_t BORDER = 0xFF414A60;

/*
    تبدیل رنگ برای فریم‌بافر
*/

static uint32_t encodePixel(uint32_t color) {
    return 0x80000000u | (color & 0x00FFFFFFu);
}

/*
    رسم مستطیل
*/

static void rectangle(
    int x, int y, int w, int h, uint32_t color
) {
    if (!buffers[currentBuffer] || w <= 0 || h <= 0)
        return;

    int64_t right = int64_t(x) + w;
    int64_t bottom = int64_t(y) + h;

    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;

    int x1 = right > W ? W : int(right);
    int y1 = bottom > H ? H : int(bottom);

    if (x0 >= x1 || y0 >= y1)
        return;

    const uint32_t pixel = encodePixel(color);

    for (int yy = y0; yy < y1; ++yy) {
        uint32_t* row = buffers[currentBuffer] + yy * W;

        for (int xx = x0; xx < x1; ++xx) {
            row[xx] = pixel;
        }
    }
}

/*
    کادر دور مستطیل
*/

static void outline(
    int x, int y, int w, int h, uint32_t color
) {
    if (w <= 0 || h <= 0)
        return;

    rectangle(x, y, w, 3, color);
    rectangle(x, y + h - 3, w, 3, color);

    rectangle(x, y, 3, h, color);
    rectangle(x + w - 3, y, 3, h, color);
}

/*
    فونت Bitmap پنج در هفت

    A-Z
    0-9
    :
    Space
*/

static const uint8_t font[][5] = {
    {126,17,17,17,126},
    {127,73,73,73,54},
    {62,65,65,65,34},
    {127,65,65,34,28},
    {127,73,73,73,65},
    {127,9,9,9,1},
    {62,65,73,73,122
