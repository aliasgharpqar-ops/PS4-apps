#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * GAMECLUB PS4 - OFFLINE DEMO
 * Login, registration, profiles, home, videos, channel,
 * following, monetization demo, settings, store and streaming.
 */

extern "C" {
    int32_t sceKernelAllocateDirectMemory(long, long, size_t, size_t, int, long*);
    int32_t sceKernelMapDirectMemory(void**, size_t, int, int, long, size_t);
    int32_t sceKernelReleaseDirectMemory(long, size_t);
    long sceKernelGetDirectMemorySize(void);
    int sceKernelUsleep(unsigned int);

    int32_t sceVideoOutOpen(int32_t, int32_t, int32_t, const void*);
    void sceVideoOutSetBufferAttribute(void*, uint32_t, uint32_t, uint32_t,
                                       uint32_t, uint32_t, uint32_t);
    int32_t sceVideoOutRegisterBuffers(int32_t, int32_t, void* const*,
                                       int32_t, const void*);
    int32_t sceVideoOutSubmitFlip(int32_t, int32_t, uint32_t, int64_t);
    int32_t sceVideoOutGetFlipStatus(int32_t, void*);
    int32_t sceVideoOutSetFlipRate(int32_t, int32_t);

    int32_t scePadInit(void);
    int32_t scePadOpen(int32_t, int32_t, int32_t, void*);
    int32_t scePadReadState(int32_t, void*);

    int32_t sceUserServiceInitialize(void*);
    int32_t sceUserServiceGetInitialUser(int32_t*);
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

struct VideoFlipStatus {
    uint64_t num, ptime, stime;
    int64_t flipArg;
    uint64_t reserved[2];
    int32_t numGpuFlipPending, numFlipPending, currentBuffer;
    uint32_t reserved1;
};

static const int W = 1920;
static const int H = 1080;

static const size_t BUFFER_SIZE = (size_t)W * H * 4;
static const size_t MEMORY_ALIGNMENT = 0x200000;

static uint32_t* buffers[2] = { nullptr, nullptr };

static int videoHandle = -1;
static int padHandle = -1;
static int currentBuffer = 0;

static long allocatedMemoryOffset = 0;
static size_t allocatedMemorySize = 0;

static int64_t frameSequence = 1;
static uint32_t previousButtons = 0;

/* Colors */

static const uint32_t BG      = 0xFF080B14;
static const uint32_t PANEL   = 0xFF141B2A;
static const uint32_t PANEL2  = 0xFF202B40;
static const uint32_t PURPLE  = 0xFFA855F7;
static const uint32_t PINK    = 0xFFFF4D8D;
static const uint32_t CYAN    = 0xFF35D8F2;
static const uint32_t WHITE   = 0xFFF5F7FF;
static const uint32_t MUTED   = 0xFF9AA8C0;
static const uint32_t BORDER  = 0xFF34425A;
static const uint32_t GREEN   = 0xFF52E0A0;
static const uint32_t YELLOW  = 0xFFFFD166;

/* Bitmap font: A-Z, 0-9, punctuation and space */

static const uint8_t font[][5] = {
    {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},
    {127,65,65,34,28},{127,73,73,73,65},{127,9,9,9,1},
    {62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},
    {32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
    {127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},
    {127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},
    {70,73,73,73,49},{1,1,127,1,1},{63,64,64,64,63},
    {31,32,64,32,31},{127,32,24,32,127},{99,20,8,20,99},
    {7,8,112,8,7},{97,81,73,69,67},

    {62,69,73,81,62},{0,66,127,64,0},{98,81,73,73,70},
    {34,65,73,73,54},{24,20,18,127,16},{47,73,73,73,49},
    {62,73,73,73,50},{1,113,9,5,3},{54,73,73,73,54},
    {38,73,73,73,62},

    {0,54,54,0,0},
    {8,8,8,8,8},
    {0,96,96,0,0},
    {0,24,24,0,0},
    {0,0,0,0,0}
};

static int characterIndex(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return 26 + c - '0';
    if (c == ':') return 36;
    if (c == '-') return 37;
    if (c == '.') return 38;
    if (c == '/') return 39;
    return 40;
}

static uint32_t encodePixel(uint32_t color) {
    return 0x80000000u | (color & 0x00FFFFFFu);
}

static void rectangle(int x, int y, int w, int h, uint32_t color) {
    if (!buffers[currentBuffer] || w <= 0 || h <= 0) return;

    int64_t right = int64_t(x) + w;
    int64_t bottom = int64_t(y) + h;

    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = right > W ? W : int(right);
    int y1 = bottom > H ? H : int(bottom);

    if (x0 >= x1 || y0 >= y1) return;

    uint32_t pixel = encodePixel(color);

    for (int yy = y0; yy < y1; ++yy) {
        uint32_t* row = buffers[currentBuffer] + yy * W;
        for (int xx = x0; xx < x1; ++xx) {
            row[xx] = pixel;
        }
    }
}

static void outline(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;

    rectangle(x, y, w, 3, color);
    rectangle(x, y + h - 3, w, 3, color);
    rectangle(x, y, 3, h, color);
    rectangle(x + w - 3, y, 3, h, color);
}

static void drawText(int x, int y, const char* text, int scale,
                     uint32_t color) {
    if (!text || scale <= 0) return;

    while (*text) {
        char c = *text++;

        if (c >= 'a' && c <= 'z') {
            c = char(c - 'a' + 'A');
        }

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

static void clearScreen(uint32_t color) {
    rectangle(0, 0, W, H, color);
}

static void card(int x, int y, int w, int h,
                 uint32_t fill, uint32_t edge = BORDER) {
    rectangle(x, y, w, h, fill);
    outline(x, y, w, h, edge);
}

static void button(int x, int y, int w, int h,
                   const char* label, bool active) {
    rectangle(x, y, w, h, active ? PURPLE : PANEL2);
    outline(x, y, w, h, active ? PINK : BORDER);
    drawText(x + 22, y + (h - 14) / 2, label, 2, WHITE);
}

/* Video initialization */

static bool initializeVideo() {
    videoHandle = sceVideoOutOpen(0xFF, 0, 0, nullptr);

    if (videoHandle < 0) return false;

    size_t required = BUFFER_SIZE * 2;

    size_t total =
        ((required + MEMORY_ALIGNMENT - 1) / MEMORY_ALIGNMENT)
        * MEMORY_ALIGNMENT;

    allocatedMemorySize = total;

    long offset = 0;

    int32_t rc = sceKernelAllocateDirectMemory(
        0,
        sceKernelGetDirectMemorySize(),
        total,
        MEMORY_ALIGNMENT,
        3,
        &offset
    );

    if (rc != 0) return false;

    allocatedMemoryOffset = offset;

    void* base = nullptr;

    rc = sceKernelMapDirectMemory(
        &base,
        total,
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

    void* registered[2] = { buffers[0], buffers[1] };

    rc = sceVideoOutRegisterBuffers(
        videoHandle,
        0,
        registered,
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

    sceVideoOutSetFlipRate(videoHandle, 0);

    currentBuffer = 0;

    return true;
}

static void presentFrame() {
    if (videoHandle < 0 || !buffers[currentBuffer]) return;

    int32_t rc = sceVideoOutSubmitFlip(
        videoHandle,
        currentBuffer,
        1,
        frameSequence
    );

    if (rc != 0) {
        sceKernelUsleep(16667);
        return;
    }

    for (int i = 0; i < 50; ++i) {
        VideoFlipStatus status = {};

        if (sceVideoOutGetFlipStatus(videoHandle, &status) == 0 &&
            status.flipArg == frameSequence) {
            break;
        }

        sceKernelUsleep(1000);
    }

    currentBuffer = 1 - currentBuffer;
    ++frameSequence;
}

static void initializeController() {
    int32_t userId = 0xFF;

    struct UserServiceParams {
        uint32_t priority;
    };

    UserServiceParams params = {};
    params.priority = 0x2FF;

    sceUserServiceInitialize(&params);

    if (sceUserServiceGetInitialUser(&userId) != 0) {
        userId = 0xFF;
    }

    if (scePadInit() != 0) return;

    padHandle = scePadOpen(userId, 0, 0, nullptr);

    if (padHandle < 0 && userId != 0xFF) {
        padHandle = scePadOpen(0xFF, 0, 0, nullptr);
    }
}

/* App navigation */

enum Screen {
    WELCOME,
    LOGIN,
    REGISTER,
    PROFILE_PICK,
    HOME,
    VIDEOS,
    CHANNEL,
    FOLLOWING,
    MONETIZATION,
    SETTINGS,
    STORE,
    STREAM,
    ABOUT
};

static Screen screen = WELCOME;
static int selected = 0;
static int language = 0;
static int followers = 128;
static bool followed = false;
static int toastFrames = 0;
static char toast[64] = "WELCOME TO GAMECLUB";

static void setToast(const char* message) {
    int i = 0;

    while (message[i] && i < 62) {
        toast[i] = message[i];
        ++i;
    }

    toast[i] = 0;
    toastFrames = 180;
}

static void drawToast() {
    if (toastFrames <= 0) return;

    card(1180, 875, 650, 75, PANEL2, CYAN);
    drawText(1210, 902, toast, 2, WHITE);

    --toastFrames;
}

static void header(const char* title) {
    rectangle(0, 0, W, 10, PURPLE);

    drawText(80, 35, "GAMECLUB", 6, WHITE);
    drawText(90, 100, "GAMING COMMUNITY", 2, CYAN);

    drawText(1450, 65, "PS4 DEMO", 2, MUTED);

    rectangle(80, 155, 1760, 2, BORDER);

    drawText(85, 180, title, 3, WHITE);
}

static void footer(const char* left = "UP DOWN MOVE",
                   const char* right = "X SELECT O BACK") {
    rectangle(80, 1000, 1760, 2, BORDER);

    drawText(85, 1025, left, 2, MUTED);
    drawText(1390, 1025, right, 2, CYAN);
}

/* Welcome */

static void drawWelcome() {
    clearScreen(BG);

    rectangle(0, 0, W, 12, PURPLE);
    rectangle(0, 700, W, 380, 0xFF0D1423);

    drawText(400, 240, "GAMECLUB", 13, WHITE);
    drawText(590, 420, "YOUR GAMING WORLD", 4, CYAN);
    drawText(620, 490, "VIDEOS CHANNELS COMMUNITY", 2, MUTED);

    button(690, 640, 540, 85, "PRESS X TO START", true);

    drawText(710, 820, "OFFLINE DEMO - NO REAL ACCOUNT", 2, YELLOW);
    drawText(770, 900, "CREATED BY ALI ASGHAR", 2, MUTED);

    footer("WELCOME", "X START");
    presentFrame();
}

/* Login / register */

static void drawLogin(bool isRegister) {
    clearScreen(BG);

    header(isRegister ? "CREATE ACCOUNT" : "SIGN IN");

    card(470, 250, 980, 630, PANEL, PURPLE);

    drawText(
        560, 300,
        isRegister ? "JOIN GAMECLUB" : "WELCOME BACK PLAYER",
        3, WHITE
    );

    drawText(560, 375, "DEMO USERNAME", 2, CYAN);
    card(550, 410, 820, 70, PANEL2);
    drawText(580, 435, "PLAYER001", 2, WHITE);

    drawText(560, 515, "DEMO PASSWORD", 2, CYAN);
    card(550, 550, 820, 70, PANEL2);
    drawText(580, 575, "********", 2, WHITE);

    drawText(560, 650, "SIMULATED FORM - NO DATA IS SENT", 2, YELLOW);

    button(
        560, 715, 360, 75,
        isRegister ? "CREATE DEMO ACCOUNT" : "SIGN IN DEMO",
        selected == 0
    );

    button(
        960, 715, 360, 75,
        isRegister ? "BACK TO SIGN IN" : "REGISTER DEMO",
        selected == 1
    );

    footer("LEFT RIGHT SELECT", "X CONFIRM O BACK");

    presentFrame();
}

/* Profile picker */

static void drawProfiles() {
    clearScreen(BG);
    header("CHOOSE YOUR PROFILE");

    const char* names[] = {
        "MAIN PLAYER",
        "STREAMER MODE",
        "CREATE PROFILE"
    };

    const char* details[] = {
        "LEVEL 12 - GAME LOVER",
        "VIDEO CREATOR - DEMO",
        "ADD A DEMO PROFILE"
    };

    for (int i = 0; i < 3; ++i) {
        int x = 150 + i * 550;

        card(x, 330, 450, 360, PANEL,
             selected == i ? PURPLE : BORDER);

        rectangle(
            x + 145, 370, 160, 160,
            selected == i ? PURPLE : PANEL2
        );

        drawText(
            x + 190, 420,
            i == 0 ? "P1" : (i == 1 ? "P2" : "+"),
            5, WHITE
        );

        drawText(x + 45, 565, names[i], 2, WHITE);
        drawText(x + 45, 610, details[i], 1, CYAN);
    }

    button(650, 780, 620, 75, "CONTINUE TO GAMECLUB", true);

    footer("LEFT RIGHT CHANGE PROFILE", "X CONTINUE");

    presentFrame();
}

/* Main dashboard */

static void drawHome() {
    clearScreen(BG);
    header("HOME");

    card(80, 235, 390, 700, PANEL);

    drawText(115, 270, "MAIN MENU", 3, CYAN);

    const char* items[] = {
        "HOME",
        "VIDEOS",
        "MY CHANNEL",
        "FOLLOWING",
        "MONETIZATION",
        "SETTINGS",
        "GAME STORE",
        "STREAM",
        "ABOUT"
    };

    for (int i = 0; i < 9; ++i) {
        int y = 330 + i * 63;

        if (i == selected) {
            rectangle(105, y - 12, 340, 48, PURPLE);
        }

        drawText(
            130, y, items[i], 2,
            i == selected ? WHITE : MUTED
        );
    }

    card(510, 235, 1330, 330, PANEL, PURPLE);

    drawText(570, 285, "WELCOME TO YOUR GAMING WORLD", 4, WHITE);
    drawText(570, 360, "WATCH GAMEPLAY - FOLLOW CREATORS", 3, CYAN);
    drawText(570, 425, "YOUR NEXT LEVEL STARTS HERE", 2, MUTED);

    card(510, 600, 420, 330, PANEL);
    card(965, 600, 420, 330, PANEL);
    card(1420, 600, 420, 330, PANEL);

    rectangle(535, 625, 370, 150, 0xFF31204A);
    rectangle(990, 625, 370, 150, 0xFF123846);
    rectangle(1445, 625, 370, 150, 0xFF462235);

    drawText(560, 690, "NEW VIDEOS", 3, WHITE);
    drawText(1015, 690, "CREATORS", 3, WHITE);
    drawText(1470, 690, "YOUR CHANNEL", 3, WHITE);

    drawText(555, 810, "OPEN VIDEO FEED", 2, CYAN);
    drawText(1010, 810, "FIND PLAYERS", 2, CYAN);
    drawText(1465, 810, "EDIT PROFILE", 2, CYAN);

    drawToast();

    footer("UP DOWN NAVIGATE", "X OPEN O LOG OUT");

    presentFrame();
}

/* Video feed */

static void drawVideos() {
    clearScreen(BG);
    header("VIDEO FEED");

    const char* titles[] = {
        "EPIC BATTLE GAMEPLAY",
        "TOP GAMING MOMENTS",
        "RACING NIGHT SESSION"
    };

    const char* creators[] = {
        "BY PLAYER001",
        "BY PROGAMER",
        "BY SPEEDRUNNER"
    };

    for (int i = 0; i < 3; ++i) {
        int x = 100 + i * 590;

        card(x, 280, 520, 470, PANEL,
             selected == i ? PURPLE : BORDER);

        rectangle(
            x + 20, 305, 480, 255,
            i == 0 ? 0xFF34234C :
            (i == 1 ? 0xFF153847 : 0xFF472438)
        );

        drawText(
            x + 60, 390,
            i == 0 ? "GAMEPLAY" :
            (i == 1 ? "HIGHLIGHTS" : "RACING"),
            3, WHITE
        );

        drawText(x + 35, 590, titles[i], 2, WHITE);
        drawText(x + 35, 635, creators[i], 2, CYAN);

        drawText(
            x + 35, 690,
            i == 0 ? "1.2K VIEWS" :
            (i == 1 ? "840 VIEWS" : "530 VIEWS"),
            2, MUTED
        );
    }

    button(610, 800, 300, 70, "FOLLOW CREATOR", selected == 0);
    button(970, 800, 300, 70, "PLAY DEMO VIDEO", selected == 1);

    footer("LEFT RIGHT SELECT CARD", "X ACTION O BACK");

    presentFrame();
}

/* User channel */

static void drawChannel() {
    clearScreen(BG);
    header("CREATOR CHANNEL");

    card(100, 250, 1720, 280, PANEL, PURPLE);

    rectangle(150, 300, 170, 170, PURPLE);
    drawText(190, 355, "P1", 5, WHITE);

    drawText(370, 305, "PLAYER001", 4, WHITE);
    drawText(370, 370, "GAMER - VIDEO CREATOR", 2, CYAN);
    drawText(370, 420, "WELCOME TO MY GAMECLUB CHANNEL", 2, MUTED);

    drawText(1330, 325, "FOLLOWERS", 2, MUTED);
    drawText(1350, 375, followed ? "129" : "128", 4, WHITE);

    button(
        1320, 430, 350, 65,
        followed ? "FOLLOWING" : "FOLLOW CHANNEL",
        true
    );

    drawText(110, 590, "UPLOADED VIDEOS", 3, CYAN);

    card(110, 640, 500, 250, PANEL);
    card(650, 640, 500, 250, PANEL);
    card(1190, 640, 500, 250, PANEL);

    rectangle(135, 665, 450, 120, 0xFF31204A);
    rectangle(675, 665, 450, 120, 0xFF153847);
    rectangle(1215, 665, 450, 120, 0xFF462235);

    drawText(155, 815, "GAMEPLAY VIDEO", 2, WHITE);
    drawText(695, 815, "FUNNY MOMENTS", 2, WHITE);
    drawText(1235, 815, "RACING CLIP", 2, WHITE);

    footer("CHANNEL DEMO", "X FOLLOW O BACK");

    presentFrame();
}

/* Following list */

static void drawFollowing() {
    clearScreen(BG);
    header("FOLLOWING");

    drawText(110, 250, "CREATORS YOU FOLLOW", 3, CYAN);

    const char* names[] = {
        "PROGAMER",
        "SPEEDRUNNER",
        "PLAYER001"
    };

    for (int i = 0; i < 3; ++i) {
        int y = 330 + i * 175;

        card(120, y, 1650, 135, PANEL,
             i == selected ? PURPLE : BORDER);

        rectangle(
            150, y + 20, 95, 95,
            i == 0 ? PURPLE : (i == 1 ? CYAN : PINK)
        );

        drawText(285, y + 38, names[i], 3, WHITE);
        drawText(285, y + 82, "GAMING CREATOR - DEMO", 2, MUTED);
        drawText(1450, y + 55, "VIEW CHANNEL", 2, CYAN);
    }

    footer("UP DOWN SELECT CREATOR", "X OPEN O BACK");

    presentFrame();
}

/* Monetization demo */

static void drawMonetization() {
    clearScreen(BG);
    header("CREATOR MONETIZATION");

    card(250, 250, 1420, 620, PANEL, PURPLE);

    drawText(350, 310, "LEVEL UP YOUR CHANNEL", 4, WHITE);
    drawText(350, 395, "DEMO REQUIREMENT: 150 FOLLOWERS", 3, CYAN);

    drawText(350, 475, "YOUR FOLLOWERS", 2, MUTED);

    drawText(
        350, 520,
        followers >= 150 ? "150 / 150" : "128 / 150",
        5, WHITE
    );

    rectangle(350, 610, 1120, 25, PANEL2);

    rectangle(
        350, 610,
        followers >= 150 ? 1120 : 957,
        25,
        followers >= 150 ? GREEN : PURPLE
    );

    drawText(
        350, 680,
        followers >= 150
            ? "DEMO ELIGIBILITY UNLOCKED"
            : "KEEP CREATING TO REACH THE GOAL",
        3,
        followers >= 150 ? GREEN : YELLOW
    );

    drawText(
        350, 750,
        "REAL PAYMENTS ARE NOT ENABLED",
        2, MUTED
    );

    button(350, 790, 430, 65, "TEST +22 FOLLOWERS", true);

    footer("MONETIZATION DEMO", "X TEST ACTION O BACK");

    presentFrame();
}

/* Settings and language selector */

static void drawSettings() {
    clearScreen(BG);
    header("SETTINGS");

    card(250, 260, 1420, 590, PANEL, PURPLE);

    drawText(350, 320, "LANGUAGE", 3, CYAN);

    button(350, 400, 500, 85, "ENGLISH", selected == 0);
    button(920, 400, 500, 85, "FARSI DEMO", selected == 1);

    drawText(350, 545, "DISPLAY", 2, WHITE);
    drawText(350, 595, "DARK GAMING THEME - PURPLE / CYAN", 2, MUTED);

    drawText(350, 660, "ACCOUNT", 2, WHITE);
    drawText(350, 710, "LOCAL DEMO SESSION - NOT ONLINE", 2, YELLOW);

    drawText(
        350, 770,
        "PERSIAN TEXT NEEDS A PROPER FONT RENDERER",
        2, MUTED
    );

    footer("LEFT RIGHT CHANGE OPTION", "X APPLY O BACK");

    presentFrame();
}

/* Coming-soon pages */

static void drawComingSoon(const char* title,
                           const char* line1,
                           const char* line2) {
    clearScreen(BG);
    header(title);

    card(300, 280, 1320, 500, PANEL, PURPLE);

    drawText(480, 365, "COMING SOON", 6, WHITE);
    drawText(450, 500, line1, 3, CYAN);
    drawText(450, 570, line2, 2, MUTED);
    drawText(450, 665, "LOCAL DEMO SCREEN", 2, YELLOW);

    footer("GAMECLUB", "O BACK");

    presentFrame();
}

static void drawAbout() {
    clearScreen(BG);
    header("ABOUT GAMECLUB");

    card(250, 260, 1420, 590, PANEL, PURPLE);

    drawText(370, 330, "GAMECLUB PS4 DEMO", 4, WHITE);
    drawText(370, 420, "GAMING COMMUNITY CONCEPT", 2, CYAN);
    drawText(370, 500, "CREATED BY ALI ASGHAR MEHRI", 3, WHITE);
    drawText(370, 565, "HANDLE: PQAR0011", 2, PINK);

    drawText(
        370, 660,
        "OFFLINE PROTOTYPE - NO ONLINE FEATURES",
        2, MUTED
    );

    footer("ABOUT", "O BACK");

    presentFrame();
}

/* Render current page */

static void render() {
    switch (screen) {
        case WELCOME:
            drawWelcome();
            break;

        case LOGIN:
            drawLogin(false);
            break;

        case REGISTER:
            drawLogin(true);
            break;

        case PROFILE_PICK:
            drawProfiles();
            break;

        case HOME:
            drawHome();
            break;

        case VIDEOS:
            drawVideos();
            break;

        case CHANNEL:
            drawChannel();
            break;

        case FOLLOWING:
            drawFollowing();
            break;

        case MONETIZATION:
            drawMonetization();
            break;

        case SETTINGS:
            drawSettings();
            break;

        case STORE:
            drawComingSoon(
                "GAME STORE",
                "GAME STORE PREVIEW",
                "GAME DOWNLOADS WILL BE ADDED LATER"
            );
            break;

        case STREAM:
            drawComingSoon(
                "STREAMING",
                "STREAM AND STREAM KEY",
                "LIVE STREAMING IS NOT CONNECTED"
            );
            break;

        case ABOUT:
            drawAbout();
            break;
    }
}

/* Navigation helpers */

static void goHome() {
    screen = HOME;
    selected = 0;
}

static void handleConfirm() {
    switch (screen) {
        case WELCOME:
            screen = LOGIN;
            selected = 0;
            break;

        case LOGIN:
            if (selected == 0) {
                screen = PROFILE_PICK;
                selected = 0;
                setToast("DEMO LOGIN SUCCESS");
            } else {
                screen = REGISTER;
                selected = 0;
            }
            break;

        case REGISTER:
            if (selected == 0) {
                screen = PROFILE_PICK;
                selected = 0;
                setToast("DEMO ACCOUNT CREATED");
            } else {
                screen = LOGIN;
                selected = 0;
            }
            break;

        case PROFILE_PICK:
            goHome();
            setToast("PROFILE SELECTED");
            break;

        case HOME:
            switch (selected) {
                case 0:
                    setToast("YOU ARE ON HOME");
                    break;

                case 1:
                    screen = VIDEOS;
                    selected = 0;
                    break;

                case 2:
                    screen = CHANNEL;
                    selected = 0;
                    break;

                case 3:
                    screen = FOLLOWING;
                    selected = 0;
                    break;

                case 4:
                    screen = MONETIZATION;
                    selected = 0;
                    break;

                case 5:
                    screen = SETTINGS;
                    selected = 0;
                    break;

                case 6:
                    screen = STORE;
                    selected = 0;
                    break;

                case 7:
                    screen = STREAM;
                    selected = 0;
                    break;

                default:
                    screen = ABOUT;
                    selected = 0;
                    break;
            }
            break;

        case VIDEOS:
            if (selected == 0) {
                screen = CHANNEL;
                selected = 0;
            } else {
                setToast("VIDEO PLAYER IS DEMO ONLY");
            }
            break;

        case CHANNEL:
            if (!followed) {
                followed = true;
                followers = 129;
                setToast("FOLLOWED - DEMO COUNT UPDATED");
            } else {
                setToast("ALREADY FOLLOWING - DEMO");
            }
            break;

        case FOLLOWING:
            screen = CHANNEL;
            selected = 0;
            break;

        case MONETIZATION:
            followers = 150;
            setToast("DEMO GOAL REACHED - NO REAL PAYMENTS");
            break;

        case SETTINGS:
            language = selected;
            setToast(language == 0
                ? "LANGUAGE: ENGLISH"
                : "FARSI DEMO MODE");
            break;

        case STORE:
            setToast("STORE IS NOT CONNECTED YET");
            break;

        case STREAM:
            setToast("STREAMING IS NOT CONNECTED YET");
            break;

        case ABOUT:
            break;
    }
}

static void handleBack() {
    switch (screen) {
        case WELCOME:
            break;

        case LOGIN:
            screen = WELCOME;
            selected = 0;
            break;

        case REGISTER:
            screen = LOGIN;
            selected = 0;
            break;

        case PROFILE_PICK:
            screen = LOGIN;
            selected = 0;
            break;

        case HOME:
            screen = LOGIN;
            selected = 0;
            setToast("SIGNED OUT - DEMO");
            break;

        default:
            goHome();
            break;
    }
}

static void moveSelection(int direction) {
    switch (screen) {
        case LOGIN:
        case REGISTER:
            selected = (selected + direction + 2) % 2;
            break;

        case PROFILE_PICK:
            selected = (selected + direction + 3) % 3;
            break;

        case HOME:
            selected = (selected + direction + 9) % 9;
            break;

        case VIDEOS:
        case FOLLOWING:
            selected = (selected + direction + 3) % 3;
            break;

        case SETTINGS:
            selected = (selected + direction + 2) % 2;
            break;

        default:
            break;
    }
}

/* Entry point */

int main() {
    if (!initializeVideo()) {
        return 1;
    }

    initializeController();

    while (true) {
        if (padHandle >= 0) {
            PadState pad = {};

            if (scePadReadState(padHandle, &pad) == 0) {
                uint32_t pressed = pad.buttons & ~previousButtons;
                previousButtons = pad.buttons;

                /* D-pad navigation */
                if (pressed & 0x0010u) moveSelection(-1);
                if (pressed & 0x0040u) moveSelection(1);

                /* Left/right for horizontal selectors */
                if (pressed & 0x0004u) moveSelection(-1);
                if (pressed & 0x0008u) moveSelection(1);

                /* Cross */
                if (pressed & 0x4000u) handleConfirm();

                /* Circle */
                if (pressed & 0x2000u) handleBack();
            }
        }

        render();

        sceKernelUsleep(16667);
    }

    return 0;
}
