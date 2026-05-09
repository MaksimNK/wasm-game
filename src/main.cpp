#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 600;
const int SQUARE_SIZE = 50;
const int SPEED = 5;

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;

struct Square {
    float x, y;
};

Square player = {WINDOW_WIDTH / 2.0f - SQUARE_SIZE / 2.0f, WINDOW_HEIGHT / 2.0f - SQUARE_SIZE / 2.0f};

void handle_input() {
    const Uint8* keystate = SDL_GetKeyboardState(nullptr);
    
    if (keystate[SDL_SCANCODE_LEFT] || keystate[SDL_SCANCODE_A]) {
        player.x -= SPEED;
    }
    if (keystate[SDL_SCANCODE_RIGHT] || keystate[SDL_SCANCODE_D]) {
        player.x += SPEED;
    }
    if (keystate[SDL_SCANCODE_UP] || keystate[SDL_SCANCODE_W]) {
        player.y -= SPEED;
    }
    if (keystate[SDL_SCANCODE_DOWN] || keystate[SDL_SCANCODE_S]) {
        player.y += SPEED;
    }
    
    // Keep square on screen
    if (player.x < 0) player.x = 0;
    if (player.x > WINDOW_WIDTH - SQUARE_SIZE) player.x = WINDOW_WIDTH - SQUARE_SIZE;
    if (player.y < 0) player.y = 0;
    if (player.y > WINDOW_HEIGHT - SQUARE_SIZE) player.y = WINDOW_HEIGHT - SQUARE_SIZE;
}

void render() {
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    SDL_RenderClear(renderer);
    
    SDL_Rect rect = {(int)player.x, (int)player.y, SQUARE_SIZE, SQUARE_SIZE};
    SDL_SetRenderDrawColor(renderer, 0, 150, 255, 255);
    SDL_RenderFillRect(renderer, &rect);
    
    SDL_RenderPresent(renderer);
}

void main_loop() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
#ifdef __EMSCRIPTEN__
            emscripten_cancel_main_loop();
#endif
            SDL_Quit();
            exit(0);
        }
    }
    
    handle_input();
    render();
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        return 1;
    }
    
    window = SDL_CreateWindow("Moving Square", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    if (!window) {
        SDL_Quit();
        return 1;
    }
    
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(main_loop, 0, 1);
#else
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            }
        }
        
        handle_input();
        render();
        SDL_Delay(ы);
    }
#endif
    
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    return 0;
}
