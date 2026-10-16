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
    {62,65,73,73,122},
    {127,8,8,8,127},
    {0,65,127,65,0},
    {32,64,65,63,1},
    {127,8,20,34,65},
    {127,64,64,64,64},
    {127,2,12,2,127},
    {127,4,8,16,127},
    {62,65,65,65,62},
    {127,9,9,9,6},
    {62,65,81,33,94},
    {127,9,25,41,70},
    {70,73,73,73,49},
    {1,1,127,1,1},
    {63,64,64,64,63},
    {31,32,64,32,31},
    {127,32,24,32,127},
    {99,20,8,20,99},
    {7,8,112,8,7},
    {97,81,73,69,67},

    {62,69,73,81,62},
    {0,66,127,64,0},
    {98,81,73,73,70},
    {34,65,73,73,54},
    {24,20,18,127,16},
    {47,73,73,73,49},
    {62,73,73,73,50},
    {1,113,9,5,3},
    {54,73,73,73,54},
    {38,73,73,73,62},

    {0,54,54,0,0},
    {0,0,0,0,0}
};

static int characterIndex(char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';

    if (c >= '0' && c <= '9')
        return 26 + c - '0';

    if (c == ':')
        return 36;

    return 37;
}

/*
    رسم متن ASCII
*/

static void drawText(
    int x,
    int y,
    const char* text,
    int scale,
    uint32_t color
) {
    if (!text || scale <= 0)
        return;

    while (*text) {
        char c = *text++;

        if (c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');

        int index = characterIndex(c);

        for (int col = 0; col < 5; ++col) {
            for (int row = 0; row < 7; ++row) {
                if (font[index][col] & (1 << row)) {
                    rectangle(
                        x + col * scale,
                        y + row * scale,
                        scale,
                        scale,
                        color
                    );
                }
            }
        }

        x += 6 * scale;
    }
}

/*
    پاک‌کردن صفحه
*/

static void clearScreen(uint32_t color) {
    rectangle(0, 0, W, H, color);
}

/*
    راه‌اندازی تصویر

    اگر این مرحله شکست بخورد،
    برنامه وارد حلقه بی‌نهایت نمی‌شود.
*/

static bool initializeVideo() {
    videoHandle = sceVideoOutOpen(
        0xFF, 0, 0, nullptr
    );

    if (videoHandle < 0)
        return false;

    const size_t requiredSize = BUFFER_SIZE * 2;

    const size_t totalSize =
        ((requiredSize + MEMORY_ALIGNMENT - 1)
         / MEMORY_ALIGNMENT) * MEMORY_ALIGNMENT;

    allocatedMemorySize = totalSize;

    long offset = 0;

    int32_t rc = sceKernelAllocateDirectMemory(
        0,
        sceKernelGetDirectMemorySize(),
        totalSize,
        MEMORY_ALIGNMENT,
        3,
        &offset
    );

    if (rc != 0)
        return false;

    allocatedMemoryOffset = offset;

    void* base = nullptr;

    rc = sceKernelMapDirectMemory(
        &base,
        totalSize,
        0x33,
        0,
        allocatedMemoryOffset,
        MEMORY_ALIGNMENT
    );

    if (rc != 0 || base == nullptr) {
        sceKernelReleaseDirectMemory(
            allocatedMemoryOffset,
            allocatedMemorySize
        );

        allocatedMemorySize = 0;
        return false;
    }

    buffers[0] = static_cast<uint32_t*>(base);

    buffers[1] = reinterpret_cast<uint32_t*>(
        reinterpret_cast<uint8_t*>(base) + BUFFER_SIZE
    );

    /*
        مقدارهای ویژگی بافر
    */

    VideoBufAttr attributes = {};

    sceVideoOutSetBufferAttribute(
        &attributes,
        0x80000000u,
        1,
        0,
        W,
        H,
        W
    );

    void* registeredBuffers[2] = {
        buffers[0],
        buffers[1]
    };

    rc = sceVideoOutRegisterBuffers(
        videoHandle,
        0,
        registeredBuffers,
        2,
        &attributes
    );

    if (rc != 0) {
        sceKernelReleaseDirectMemory(
            allocatedMemoryOffset,
            allocatedMemorySize
        );

        allocatedMemorySize = 0;
        buffers[0] = nullptr;
        buffers[1] = nullptr;

        return false;
    }

    /*
        نرخ نمایش 60 هرتز
    */

    sceVideoOutSetFlipRate(videoHandle, 0);

    currentBuffer = 0;

    return true;
}

/*
    نمایش فریم

    حالت VSync برابر 1 است.
*/

static void presentFrame() {
    if (videoHandle < 0 || !buffers[currentBuffer])
        return;

    const int32_t rc = sceVideoOutSubmitFlip(
        videoHandle,
        currentBuffer,
        1,
        frameSequence
    );

    if (rc != 0) {
        sceKernelUsleep(16667);
        return;
    }

    /*
        منتظر تکمیل فریم می‌مانیم،
        ولی حلقه انتظار زمان محدود دارد.
    */

    for (int i = 0; i < 50; ++i) {
        VideoFlipStatus status = {};

        if (sceVideoOutGetFlipStatus(
                videoHandle,
                &status
            ) == 0) {

            if (status.flipArg == frameSequence)
                break;
        }

        sceKernelUsleep(1000);
    }

    currentBuffer = 1 - currentBuffer;

    ++frameSequence;
}

/*
    هدر گیم‌کلاب
*/

static void drawBrand() {
    rectangle(0, 0, W, 12, PURPLE);

    drawText(
        100, 65,
        "GAMECLUB",
        8,
        WHITE
    );

    drawText(
        105, 145,
        "PS4 GAMING HUB",
        3,
        CYAN
    );

    rectangle(100, 200, 1720, 2, BORDER);

    drawText(
        1510, 82,
        "FIRMWARE 9.00",
        2,
        MUTED
    );
}

/*
    پنجره خوشامدگویی
*/

static void drawWelcomeDialog() {
    clearScreen(BG);

    rectangle(0, 0, W, 12, PURPLE);

    drawText(
        100, 65,
        "GAMECLUB",
        6,
        WHITE
    );

    rectangle(
        300, 260,
        1320, 570,
        PANEL
    );

    outline(
        300, 260,
        1320, 570,
        PURPLE
    );

    drawText(
        520, 330,
        "WELCOME TO GAMECLUB",
        4,
        WHITE
    );

    rectangle(420, 405, 1080, 3, BORDER);

    drawText(
        460, 465,
        "CREATED BY",
        3,
        CYAN
    );

    drawText(
        460, 520,
        "ALI ASGHAR MEHRI",
        4,
        WHITE
    );

    drawText(
        460, 610,
        "PROGRAMMING TEAM",
        3,
        CYAN
    );

    drawText(
        460, 665,
        "PQAR0011",
        4,
        PINK
    );

    rectangle(
        560, 740,
        800, 65,
        PURPLE
    );

    drawText(
        700, 760,
        "PRESS X TO CONTINUE",
        2,
        WHITE
    );

    presentFrame();
}

/*
    اگر کنترلر در دسترس نباشد،
    صفحه خوشامدگویی دائمی نمی‌شود.
*/

static void showWelcomeDialog() {
    const int maxFrames = (padHandle < 0) ? 180 : 600;

    for (int frame = 0; frame < maxFrames; ++frame) {
        drawWelcomeDialog();

        if (padHandle >= 0) {
            PadState pad = {};

            if (scePadReadState(
                    padHandle,
                    &pad
                ) == 0) {

                const uint32_t pressed =
                    pad.buttons & ~previousButtons;

                previousButtons = pad.buttons;

                if (pressed & 0x4000u)
                    return;
            }
        }

        /*
            پایان خودکار پس از زمان محدود
        */
    }
}

/*
    لودینگ
*/

static void loadingScreen() {
    const char* stages[] = {
        "INITIALIZING GAMECLUB",
        "CHECKING LOCAL SYSTEM",
        "STARTING GAMECLUB"
    };

    for (int stage = 0; stage < 3; ++stage) {
        for (int frame = 0; frame < 50; ++frame) {
            clearScreen(BG);

            rectangle(0, 0, W, 12, PURPLE);

            drawText(
                460, 300,
                "GAMECLUB",
                12,
                WHITE
            );

            drawText(
                600, 465,
                stages[stage],
                3,
                CYAN
            );

            rectangle(
                460, 540,
                1000, 12,
                PANEL2
            );

            rectangle(
                460, 540,
                (frame + 1) * 1000 / 50,
                12,
                stage == 2 ? PINK : PURPLE
            );

            drawText(
                820, 590,
                "PLEASE WAIT",
                2,
                MUTED
            );

            presentFrame();
        }
    }
}

/*
    منوی اصلی
*/

static const char* menuItems[] = {
    "MY GAMES",
    "GAME STORE",
    "DOWNLOAD THEMES",
    "STREAM",
    "STREAM KEY",
    "ABOUT GAMECLUB"
};

static const int MENU_COUNT = 6;

static int selected = 0;

static const char* statusMessage = "READY TO PLAY";

/*
    انتخاب گزینه
*/

static void selectMenuItem() {
    switch (selected) {
        case 0:
            statusMessage = "MY GAMES: LOCAL DEMO";
            break;

        case 1:
            statusMessage = "GAME STORE: COMING SOON";
            break;

        case 2:
            statusMessage = "THEMES: COMING SOON";
            break;

        case 3:
            statusMessage = "STREAM: DEMO MODE";
            break;

        case 4:
            statusMessage = "STREAM KEY: DEMO";
            break;

        default:
            statusMessage = "CREATED BY ALI ASGHAR MEHRI";
            break;
    }
}

/*
    رسم صفحه اصلی
*/

static void drawMenu() {
    clearScreen(BG);

    for (int i = 0; i < 12; ++i) {
        rectangle(
            1100 + i * 55,
            260,
            2,
            620,
            0xFF151C2B
        );
    }

    drawBrand();

    /*
        پنل منو
    */

    rectangle(
        100, 250,
        520, 690,
        PANEL
    );

    drawText(
        145, 285,
        "MAIN MENU",
        3,
        CYAN
    );

    for (int i = 0; i < MENU_COUNT; ++i) {
        const int y = 355 + i * 88;

        if (i == selected) {
            rectangle(
                125, y - 18,
                470, 66,
                PURPLE
            );

            rectangle(
                125, y - 18,
                7, 66,
                PINK
            );

            drawText(
                160, y,
                menuItems[i],
                2,
                WHITE
            );
        } else {
            drawText(
                160, y,
                menuItems[i],
                2,
                MUTED
            );
        }
    }

    /*
        پنل معرفی
    */

    rectangle(
        675, 250,
        1145, 420,
        PANEL
    );

    outline(
        675, 250,
        1145, 420,
        BORDER
    );

    drawText(
        745, 310,
        "WELCOME TO GAMECLUB",
        4,
        WHITE
    );

    drawText(
        745, 380,
        "YOUR GAMES YOUR STREAM",
        3,
        CYAN
    );

    rectangle(
        745, 455,
        1000, 2,
        BORDER
    );

    drawText(
        745, 500,
        "SELECT AN OPTION",
        3,
        MUTED
    );

    drawText(
        745, 550,
        "PRESS X TO OPEN",
        2,
        PINK
    );

    /*
        پنل وضعیت
    */

    rectangle(
        675, 710,
        1145, 230,
        PANEL
    );

    drawText(
        735, 755,
        "STATUS",
        2,
        CYAN
    );

    drawText(
        735, 810,
        statusMessage,
        3,
        WHITE
    );

    drawText(
        735, 875,
        "FIRST TEST BUILD",
        2,
        MUTED
    );

    /*
        نوار پایین
    */

    rectangle(
        100, 980,
        1720, 2,
        BORDER
    );

    drawText(
        100, 1010,
        "UP DOWN MOVE",
        2,
        MUTED
    );

    drawText(
        500, 1010,
        "X SELECT",
        2,
        MUTED
    );

    drawText(
        800, 1010,
        "O BACK",
        2,
        MUTED
    );

    drawText(
        1450, 1010,
        "GAMECLUB DEMO 1",
        2,
        PURPLE
    );

    presentFrame();
}

/*
    مقداردهی اولیه کنترلر
*/

static void initializeController() {
    int32_t userId = 0xFF;

    /*
        اطلاعات کاربر فعال
    */

    struct UserServiceParams {
        uint32_t priority;
    };

    UserServiceParams params = {};
    params.priority = 0x2FF;

    sceUserServiceInitialize(&params);

    /*
        شناسه واقعی کاربر فعال را دریافت می‌کنیم.
    */

    if (sceUserServiceGetInitialUser(&userId) != 0) {
        userId = 0xFF;
    }

    if (scePadInit() != 0)
        return;

    padHandle = scePadOpen(
        userId,
        0,
        0,
        nullptr
    );

    /*
        اگر دسته باز نشد،
        یک تلاش جایگزین انجام می‌دهیم.
    */

    if (padHandle < 0 && userId != 0xFF) {
        padHandle = scePadOpen(
            0xFF,
            0,
            0,
            nullptr
        );
    }
}

/*
    نقطه ورود برنامه
*/

int main() {
    /*
        ابتدا تصویر را راه‌اندازی می‌کنیم.

        اگر راه‌اندازی شکست بخورد،
        برنامه با حلقه بی‌نهایت گیر نمی‌کند.
    */

    if (!initializeVideo()) {
        return 1;
    }

    /*
        کنترلر
    */

    initializeController();

    /*
        لودینگ اولیه
    */

    loadingScreen();

    /*
        معرفی سازنده
    */

    showWelcomeDialog();

    /*
        حلقه اصلی
    */

    while (true) {
        PadState pad = {};

        if (padHandle >= 0) {
            if (scePadReadState(
                    padHandle,
                    &pad
                ) == 0) {

                const uint32_t pressed =
                    pad.buttons & ~previousButtons;

                previousButtons = pad.buttons;

                /*
                    بالا
                */

                if (pressed & 0x0010u) {
                    selected =
                        (selected + MENU_COUNT - 1)
                        % MENU_COUNT;

                    statusMessage = "READY TO PLAY";
                }

                /*
                    پایین
                */

                if (pressed & 0x0040u) {
                    selected =
                        (selected + 1)
                        % MENU_COUNT;

                    statusMessage = "READY TO PLAY";
                }

                /*
                    ضربدر
                */

                if (pressed & 0x4000u) {
                    selectMenuItem();
                }

                /*
                    دایره
                */

                if (pressed & 0x2000u) {
                    statusMessage = "GAMECLUB MAIN MENU";
                }
            }
        }

        /*
            رسم منو در هر فریم
        */

        drawMenu();
    }

    return 0;
}
