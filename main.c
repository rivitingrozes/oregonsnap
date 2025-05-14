#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <stdbool.h>

// max world-space distance from player to plant
#define WIDTH 1280
#define HEIGHT 720
#define PLANT_TYPES 4
#define FOCUS_RANGE 256

typedef struct {
    int x, y;
    SDL_Rect sprite;
    int active;
    int id;
} Plant;

const char* plantNames[PLANT_TYPES] = {
    "Lupine", "Trillium", "Golden Paintbrush", "Oregon Grape"
};

const char* plantDescriptions[PLANT_TYPES] = {
    "A beautiful purple wildflower found in meadows.",
    "A three-petaled flower often found in forests.",
    "A rare golden plant native to the Pacific Northwest.",
    "An evergreen shrub with holly-like leaves and yellow flowers."
};

SDL_Rect plantSprites[PLANT_TYPES] = {
    {0, 0, 64, 64},
    {64, 0, 64, 64},
    {0, 64, 64, 64},
    {64, 64, 64, 64}
};

Plant* plants = NULL;
int plantCount = 0, plantCapacity = 0;
int plantSnaps[PLANT_TYPES] = {0};
int discovered[PLANT_TYPES] = {0};
int photoPlantIds[PLANT_TYPES] = {0};
int photoIndex = 0;
int snapCount = 0;

int playerWorldX = WIDTH / 2;
int playerY;
int facingDirection = 1;  // 1 means facing right, -1 means facing left.

int cameraX = 0;
int showLogOverlay = 0;
int mouseX = 0, mouseY = 0;
int currentBackground = 0;

int playerW = 0, playerH = 0;

SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;
SDL_Texture* backgrounds[2];
SDL_Texture* titleTexture = NULL;
SDL_Texture* playerTexture = NULL;
SDL_Texture* plantSheet = NULL;
TTF_Font* font = NULL;
Mix_Music* bgMusic = NULL;

char messageBuffer[256] = "";
int messageTimer = 0;

void ensureCapacity() {
    if (plantCount >= plantCapacity) {
        plantCapacity = plantCapacity ? plantCapacity * 2 : 64;
        plants = realloc(plants, plantCapacity * sizeof(Plant));
        if (!plants) {
            fprintf(stderr, "Memory allocation failed\n");
            exit(1);
        }
    }
}

int plantExistsNear(int x) {
    for (int i = 0; i < plantCount; i++) {
        if (abs(plants[i].x - x) < 400) return 1;
    }
    return 0;
}

void addPlant(int x, int y, int id) {
    ensureCapacity();
    plants[plantCount++] = (Plant){x, y, plantSprites[id], 1, id};
}

void generatePlantsAroundPlayer() {
    int groundY = HEIGHT - 64;
    int viewMinX = playerWorldX - 2000;
    int viewMaxX = playerWorldX + 2000;
    for (int x = viewMinX; x < viewMaxX; x += 600 + rand() % 400) {
        if (!plantExistsNear(x)) {
            int id = rand() % PLANT_TYPES;
            addPlant(x, groundY + (rand() % 6 - 3), id);
        }
    }
}

void saveScreenshot(const char* path) {
    int w, h;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
        0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_RGBA32,
                         surface->pixels, surface->pitch);
    IMG_SavePNG(surface, path);
    SDL_FreeSurface(surface);
}
// Draws current messageBuffer and decrements timer.
void renderMessage() {
    if (messageTimer <= 0) return;
    SDL_Color white = {255,255,255,255};
    SDL_Surface* surf = TTF_RenderText_Blended(font, messageBuffer, white);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect dst = {20, 20, surf->w, surf->h};
    SDL_FreeSurface(surf);
    SDL_RenderCopy(renderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
    --messageTimer;
}

void showMessage(const char* msg) {
    snprintf(messageBuffer, sizeof(messageBuffer), "%s", msg);
    messageTimer = 180;
}

// Draw only the world (background, plants, player), no UI overlays.
void renderSceneNoUI() {
    SDL_SetRenderDrawColor(renderer, 0, 30, 20, 255);
    SDL_RenderClear(renderer);

    float parallax = 0.5f;
    int bgW = 1024;
    int startX = -(int)(cameraX * parallax) % bgW;
    if (startX > 0) startX -= bgW;
    for (int x = startX; x < WIDTH; x += bgW) {
        SDL_Rect dst = { x, 0, bgW, HEIGHT };
        SDL_RenderCopy(renderer, backgrounds[currentBackground], NULL, &dst);
    }

    for (int i = 0; i < plantCount; i++) {
        if (!plants[i].active) continue;
        SDL_Rect dst = { plants[i].x - cameraX, plants[i].y, 64, 64 };
        SDL_RenderCopy(renderer, plantSheet, &plants[i].sprite, &dst);
    }

SDL_Rect playerDst = { WIDTH/2 - playerW/2, playerY, playerW, playerH };
SDL_RenderCopyEx(renderer, playerTexture, NULL, &playerDst,
                 0.0, NULL,
                 (facingDirection == -1 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE));
}

void takePhoto() {
    SDL_Rect focusBox = { mouseX - 16, mouseY - 16, 32, 32 };
    for (int i = 0; i < plantCount; i++) {
        if (!plants[i].active) continue;
        SDL_Rect plantBox = { plants[i].x - cameraX, plants[i].y, 64, 64 };
        if (!SDL_HasIntersection(&focusBox, &plantBox)) continue;
        int dx = playerWorldX - plants[i].x;
        int dy = playerY      - plants[i].y;
        if (dx*dx + dy*dy > FOCUS_RANGE * FOCUS_RANGE) {
            showMessage("Out of focus! Get closer :]");
            return;
        }
        if (!discovered[plants[i].id]) {
            discovered[plants[i].id] = 1;
            char filename[64];
            snprintf(filename, sizeof(filename), "gallery/photo_%d.png", photoIndex);
            renderSceneNoUI();
            saveScreenshot(filename);
            photoPlantIds[photoIndex] = plants[i].id;
            photoIndex++;
            char msg[128];
            snprintf(msg, sizeof(msg), "New plant documented: %s!", plantNames[plants[i].id]);
            showMessage(msg);
        }
        plants[i].active = 0;
        plantSnaps[plants[i].id]++;
        snapCount++;
        return;
    }
    showMessage("Nothing to photograph there!");
}

void waitForClick(SDL_Window* win) {
    SDL_Event e;
    while (1) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) exit(0);
            if (e.type == SDL_MOUSEBUTTONDOWN) return;
        }
        SDL_Delay(10);
    }
}
void viewGallery() {
    // --- NEW GUARD: if no photos, show a message and return immediately ---
    if (photoIndex == 0) {
        showMessage("Gallery is empty! Take some photos first.");
        return;
    }

    // --- existing gallery code ---
    SDL_Window* galleryWin = SDL_CreateWindow(
        "Gallery",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIDTH / 2, HEIGHT / 2,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    SDL_Renderer* galleryRenderer = SDL_CreateRenderer(
        galleryWin, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    SDL_RenderSetLogicalSize(galleryRenderer, WIDTH, HEIGHT);

    for (int i = 0; i < photoIndex; i++) {
        char filename[64];
        snprintf(filename, sizeof(filename), "gallery/photo_%d.png", i);
        SDL_Surface* img = IMG_Load(filename);
        if (!img) continue;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(galleryRenderer, img);
        SDL_FreeSurface(img);

        SDL_SetRenderDrawColor(galleryRenderer, 20, 20, 20, 255);
        SDL_RenderClear(galleryRenderer);

        SDL_Rect dst = {40, 40, 1200, 640};
        SDL_Rect border = {38, 38, dst.w + 4, dst.h + 4};
        SDL_SetRenderDrawColor(galleryRenderer, 0, 0, 0, 255);
        SDL_RenderDrawRect(galleryRenderer, &border);
        SDL_RenderCopy(galleryRenderer, tex, NULL, &dst);

        int id = photoPlantIds[i];
        SDL_Color white = {255,255,255,255};
        char msg[256];
        snprintf(msg, sizeof(msg), "%s: %s", plantNames[id], plantDescriptions[id]);
        SDL_Surface* textSurf = TTF_RenderText_Blended_Wrapped(font, msg, white, dst.w);
        SDL_Texture* textTex = SDL_CreateTextureFromSurface(galleryRenderer, textSurf);
        SDL_Rect textDst = {40, HEIGHT - 40 - textSurf->h, textSurf->w, textSurf->h};
        SDL_FreeSurface(textSurf);
        SDL_RenderCopy(galleryRenderer, textTex, NULL, &textDst);
        SDL_DestroyTexture(textTex);

        SDL_RenderPresent(galleryRenderer);

        // wait for click/key
        int viewing = 1;
        SDL_Event e;
        while (viewing) {
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT ||
                    (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) ||
                    e.type == SDL_MOUSEBUTTONDOWN ||
                    e.type == SDL_KEYDOWN) {
                    viewing = 0;
                    break;
                }
            }
            SDL_Delay(16);
        }

        SDL_DestroyTexture(tex);
    }

    SDL_DestroyRenderer(galleryRenderer);
    SDL_DestroyWindow(galleryWin);
}

int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    IMG_Init(IMG_INIT_PNG);
    TTF_Init();
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
    srand((unsigned int)time(NULL));
    mkdir("gallery", 0777);

    window = SDL_CreateWindow("Snap Game",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIDTH, HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(renderer, WIDTH, HEIGHT);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    bool fullscreen = false;
    SDL_Event e;

    font = TTF_OpenFont("assets/DejaVuSans.ttf", 20);
    if (!font) {
        font = TTF_OpenFont(
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 20);
    }

    SDL_Surface* bg1 = IMG_Load("assets/forest_park.png");
    SDL_Surface* bg2 = IMG_Load("assets/meadow.png");
    backgrounds[0] = SDL_CreateTextureFromSurface(renderer, bg1);
    backgrounds[1] = SDL_CreateTextureFromSurface(renderer, bg2);
    SDL_FreeSurface(bg1);
    SDL_FreeSurface(bg2);

    SDL_Surface* psurf = IMG_Load("assets/player.png");
    playerTexture = SDL_CreateTextureFromSurface(renderer, psurf);
    SDL_FreeSurface(psurf);
    SDL_QueryTexture(playerTexture, NULL, NULL, &playerW, &playerH);
    playerY = HEIGHT - playerH;

    SDL_Surface* plantImg = IMG_Load("assets/plants.png");
    plantSheet = SDL_CreateTextureFromSurface(renderer, plantImg);
    SDL_FreeSurface(plantImg);

    SDL_Surface* titleSurf = IMG_Load("assets/title.png");
    titleTexture = SDL_CreateTextureFromSurface(renderer, titleSurf);
    SDL_FreeSurface(titleSurf);

    bgMusic = Mix_LoadMUS("assets/bgmusic.ogg");
    if (bgMusic) Mix_PlayMusic(bgMusic, -1);

    int showTitle = 1;
    while (showTitle) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) exit(0);
            if (e.type == SDL_KEYDOWN || e.type == SDL_MOUSEBUTTONDOWN)
                showTitle = 0;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F11) {
                fullscreen = !fullscreen;
                SDL_SetWindowFullscreen(window,
                    fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            }
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED) {
                // handled by logical size
            }
        }
        SDL_SetRenderDrawColor(renderer, 0,0,0,255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, titleTexture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    generatePlantsAroundPlayer();

    int running = 1;
    const Uint8* keys;
    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
                takePhoto();
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = 0;
                if (e.key.keysym.sym == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window,
                        fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                }
                if (e.key.keysym.sym == SDLK_c)
                    showLogOverlay = !showLogOverlay;
                if (e.key.keysym.sym == SDLK_g)
                    viewGallery();
            }
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED) {
                // new size accessible via e.window.data1/2
            }
        }

keys = SDL_GetKeyboardState(NULL);

// If "A" is pressed, move the player left and face left.
if (keys[SDL_SCANCODE_A]) {
    playerWorldX -= 2;
    facingDirection = -1;  // Player is facing left.
}

// If "D" is pressed, move the player right and face right.
if (keys[SDL_SCANCODE_D]) {
    playerWorldX += 2;
    facingDirection = 1;  // Player is facing right.
}



        cameraX = playerWorldX - WIDTH/2;
        currentBackground = (playerWorldX/3000) % 2;
        generatePlantsAroundPlayer();
        // scale mouse position to logical coordinates using float scaling
int winW, winH;
SDL_GetWindowSize(window, &winW, &winH);
int mx, my;
SDL_GetMouseState(&mx, &my);
float scaleX = (float)winW / WIDTH;
float scaleY = (float)winH / HEIGHT;
mouseX = (int)(mx / scaleX + 0.5f);
mouseY = (int)(my / scaleY + 0.5f);

        SDL_SetRenderDrawColor(renderer, 0,30,20,255);
        SDL_RenderClear(renderer);

        // Draw scene
        renderSceneNoUI();

        // reticle
        SDL_SetRenderDrawColor(renderer, 255,255,255,255);
        SDL_Rect ret = { mouseX-16, mouseY-16, 32, 32 };
        SDL_RenderDrawRect(renderer, &ret);

        // overlay/log
        if (showLogOverlay) {
            SDL_Color white = {255,255,255,255};
            SDL_Rect box = { WIDTH/2-150, 20, 300, 20 + 20*PLANT_TYPES };
            SDL_SetRenderDrawColor(renderer, 0,0,0,180);
            SDL_RenderFillRect(renderer, &box);
            SDL_SetRenderDrawColor(renderer, 255,255,255,255);
            SDL_RenderDrawRect(renderer, &box);
            for (int i = 0; i < PLANT_TYPES; i++) {
                char line[64];
                snprintf(line, sizeof(line), "%s: %d", plantNames[i], plantSnaps[i]);
                SDL_Surface* surf = TTF_RenderText_Blended(font, line, white);
                SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
                SDL_Rect d = { box.x + 10, box.y + 10 + i*20, surf->w, surf->h };
                SDL_RenderCopy(renderer, tex, NULL, &d);
                SDL_FreeSurface(surf);
                SDL_DestroyTexture(tex);
            }
        }

        renderMessage();
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    free(plants);
    Mix_FreeMusic(bgMusic);
    Mix_CloseAudio();
    TTF_CloseFont(font);
    SDL_DestroyTexture(titleTexture);
    SDL_DestroyTexture(playerTexture);
    SDL_DestroyTexture(plantSheet);
    for (int i = 0; i < 2; i++) SDL_DestroyTexture(backgrounds[i]);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    return 0;
}

