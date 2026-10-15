#include <stdint.h>
#include <stddef.h>

extern "C" {
    int32_t sceKernelAllocateDirectMemory(long, long, size_t, size_t, int, long*);
    int32_t sceKernelMapDirectMemory(void**, size_t, int, int, long, size_t);
    long sceKernelGetDirectMemorySize(void);
    int sceKernelUsleep(unsigned int);

    int32_t sceVideoOutOpen(int32_t, int32_t, int32_t, const void*);
    void sceVideoOutSetBufferAttribute(void*, uint32_t, uint32_t, uint32_t,
                                       uint32_t, uint32_t, uint32_t);
    int32_t sceVideoOutRegisterBuffers(int32_t, int32_t, void* const*,
                                       int32_t, const void*);
    int32_t sceVideoOutSubmitFlip(int32_t, int32_t, uint32_t, int64_t);
    int32_t sceVideoOutSetFlipRate(int32_t, int32_t);

    int32_t scePadInit(void);
    int32_t scePadOpen(int32_t, int32_t, int32_t, void*);
    int32_t scePadReadState(int32_t, void*);
}

struct VideoBufAttr {
    int32_t format, tmode, aspect;
    uint32_t width, height, pitch;
    uint64_t reserved[2];
};

struct PadState {
    uint32_t buttons;
    uint8_t rest[252];
};

static const int W = 1920;
static const int H = 1080;

static uint32_t* buffers[2];
static int videoHandle = -1;
static int padHandle = -1;
static int currentBuffer = 0;

static const uint32_t BG      = 0xFF0B0D18;
static const uint32_t PANEL   = 0xFF202538;
static const uint32_t PANEL2  = 0xFF2A3045;
static const uint32_t PURPLE  = 0xFFA855F7;
static const uint32_t PINK    = 0xFFFF4D8D;
static const uint32_t CYAN    = 0xFF35D8F2;
static const uint32_t WHITE   = 0xFFF5F7FF;
static const uint32_t MUTED   = 0xFF98A4B8;
static const uint32_t BORDER  = 0xFF414A60;

static void rectangle(int x, int y, int w, int h, uint32_t color) {
    if (!buffers[currentBuffer] || w <= 0 || h <= 0) return;

    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > W ? W : x + w;
    int y1 = y + h > H ? H : y + h;

    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
            buffers[currentBuffer][yy * W + xx] = color;
        }
    }
}

static void outline(int x, int y, int w, int h, uint32_t color) {
    rectangle(x, y, w, 3, color);
    rectangle(x, y + h - 3, w, 3, color);
    rectangle(x, y, 3, h, color);
    rectangle(x + w - 3, y, 3, h, color);
}

/* 5x7 bitmap font */
static const uint8_t font[][5] = {
    {126,17,17,17,126}, {127,73,73,73,54},
    {62,65,65,65,34}, {127,65,65,34,28},
    {127,73,73,73,65}, {127,9,9,9,1},
    {62,65,73,73,122}, {127,8,8,8,127},
    {0,65,127,65,0}, {32,64,65,63,1},
    {127,8,20,34,65}, {127,64,64,64,64},
    {127,2,12,2,127}, {127,4,8,16,127},
    {62,65,65,65,62}, {127,9,9,9,6},
    {62,65,81,33,94}, {127,9,25,41,70},
    {70,73,73,73,49}, {1,1,127,1,1},
    {63,64,64,64,63}, {31,32,64,32,31},
    {127,32,24,32,127}, {99,20,8,20,99},
    {7,8,112,8,7}, {97,81,73,69,67},
    {62,69,73,81,62}, {0,66,127,64,0},
    {98,81,73,73,70}, {34,65,73,73,54},
    {24,20,18,127,16}, {47,73,73,73,49},
    {62,73,73,73,50}, {1,113,9,5,3},
    {54,73,73,73,54}, {38,73,73,73,62},
    {0,54,54,0,0}, {0,0,0,0,0}
};

static int characterIndex(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return 26 + c - '0';
    if (c == ':') return 36;
    return 37;
}

static void drawText(int x, int y, const char* text,
                     int scale, uint32_t color) {
    while (*text) {
        char c = *text++;

        if (c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');

        int index = characterIndex(c);

        for (int col = 0; col < 5; ++col) {
            for (int row = 0; row < 7; ++row) {
                if (font[index][col] & (1 << row)) {
                    rectangle(x + col * scale,
                              y + row * scale,
                              scale, scale, color);
                }
            }
        }

        x += 6 * scale;
    }
}

static void clearScreen(uint32_t color) {
    rectangle(0, 0, W, H, color);
}

static bool initializeVideo() {
    videoHandle = sceVideoOutOpen(0xFF, 0, 0, 0);

    if (videoHandle < 0)
        return false;

    const size_t oneBuffer = size_t(W) * H * 4;
    const size_t alignment = 0x200000;
    const size_t total =
        ((oneBuffer * 2 + alignment - 1) / alignment) * alignment;

    long offset = 0;

    if (sceKernelAllocateDirectMemory(
            0, sceKernelGetDirectMemorySize(),
            total, alignment, 3, &offset) < 0) {
        return false;
    }

    void* base = 0;

    if (sceKernelMapDirectMemory(
            &base, total, 0x33, 0, offset, alignment) < 0) {
        return false;
    }

    buffers[0] = static_cast<uint32_t*>(base);
    buffers[1] = reinterpret_cast<uint32_t*>(
        reinterpret_cast<char*>(base) + oneBuffer);

    VideoBufAttr attributes = {};

    sceVideoOutSetBufferAttribute(
        &attributes, 0x80000000u, 1, 0, W, H, W);

    void* registeredBuffers[2] = { buffers[0], buffers[1] };

    if (sceVideoOutRegisterBuffers(
            videoHandle, 0, registeredBuffers, 2,
            &attributes) != 0) {
        return false;
    }

    sceVideoOutSetFlipRate(videoHandle, 0);
    return true;
}

static void presentFrame() {
    sceVideoOutSubmitFlip(videoHandle, currentBuffer, 0, 0);
    sceKernelUsleep(16667);
    currentBuffer = 1 - currentBuffer;
}

static void drawBrand() {
    rectangle(0, 0, W, 12, PURPLE);

    drawText(100, 65, "GAMECLUB", 8, WHITE);
    drawText(105, 145, "PS4 GAMING HUB", 3, CYAN);

    rectangle(100, 200, 1720, 2, BORDER);
    drawText(1510, 82, "FIRMWARE 9.00", 2, MUTED);
}

static void loadingScreen() {
    const char* stages[] = {
        "INITIALIZING GAMECLUB",
        "CHECKING LOCAL SYSTEM",
        "CONNECTING TO GAMECLUB"
    };

    for (int stage = 0; stage < 3; ++stage) {
        for (int frame = 0; frame < 60; ++frame) {
            clearScreen(BG);
            rectangle(0, 0, W, 12, PURPLE);

            drawText(460, 300, "GAMECLUB", 12, WHITE);
            drawText(600, 465, stages[stage], 3, CYAN);

            rectangle(460, 540, 1000, 12, PANEL2);
            rectangle(460, 540, (frame + 1) * 1000 / 60,
                      12, stage == 2 ? PINK : PURPLE);

            drawText(820, 590, "PLEASE WAIT", 2, MUTED);

            presentFrame();
        }
    }
}

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
            statusMessage = "GAMECLUB TEST BUILD";
            break;
    }
}

static void drawMenu() {
    clearScreen(BG);

    for (int i = 0; i < 12; ++i)
        rectangle(1100 + i * 55, 260, 2, 620, 0xFF151C2B);

    drawBrand();

    rectangle(100, 250, 520, 690, PANEL);
    drawText(145, 285, "MAIN MENU", 3, CYAN);

    for (int i = 0; i < MENU_COUNT; ++i) {
        int y = 355 + i * 88;

        if (i == selected) {
            rectangle(125, y - 18, 470, 66, PURPLE);
            rectangle(125, y - 18, 7, 66, PINK);
            drawText(160, y, menuItems[i], 2, WHITE);
        } else {
            drawText(160, y, menuItems[i], 2, MUTED);
        }
    }

    rectangle(675, 250, 1145, 420, PANEL);
    outline(675, 250, 1145, 420, BORDER);

    drawText(745, 310, "WELCOME TO GAMECLUB", 4, WHITE);
    drawText(745, 380, "YOUR GAMES YOUR STREAM", 3, CYAN);

    rectangle(745, 455, 1000, 2, BORDER);

    drawText(745, 500, "SELECT AN OPTION", 3, MUTED);
    drawText(745, 550, "PRESS X TO OPEN", 2, PINK);

    rectangle(675, 710, 1145, 230, PANEL);
    drawText(735, 755, "STATUS", 2, CYAN);
    drawText(735, 810, statusMessage, 3, WHITE);
    drawText(735, 875, "FIRST TEST BUILD", 2, MUTED);

    rectangle(100, 980, 1720, 2, BORDER);

    drawText(100, 1010, "UP DOWN MOVE", 2, MUTED);
    drawText(500, 1010, "X SELECT", 2, MUTED);
    drawText(800, 1010, "O BACK", 2, MUTED);
    drawText(1450, 1010, "GAMECLUB DEMO 1", 2, PURPLE);

    presentFrame();
}

int main() {
    if (!initializeVideo()) {
        while (1)
            sceKernelUsleep(1000000);
    }

    scePadInit();
    padHandle = scePadOpen(0, 0, 0, 0);

    loadingScreen();

    uint32_t previousButtons = 0;

    while (1) {
        PadState pad = {};

        if (padHandle >= 0 &&
            scePadReadState(padHandle, &pad) == 0) {

            uint32_t pressed = pad.buttons & ~previousButtons;
            previousButtons = pad.buttons;

            // Controller button mappings should be verified on hardware.
            if (pressed & 0x0010u) {
                selected = (selected + MENU_COUNT - 1) % MENU_COUNT;
                statusMessage = "READY TO PLAY";
            }

            if (pressed & 0x0040u) {
                selected = (selected + 1) % MENU_COUNT;
                statusMessage = "READY TO PLAY";
            }

            if (pressed & 0x4000u)
                selectMenuItem();

            if (pressed & 0x2000u)
                statusMessage = "GAMECLUB MAIN MENU";
        }

        drawMenu();
    }

    return 0;
}
