#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;

void render() {
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    SDL_RenderClear(renderer);
    
    SDL_Rect rect = {100, 100, 50, 50};
    SDL_SetRenderDrawColor(renderer, 0, 150, 255, 255);
    SDL_RenderFillRect(renderer, &rect);
    
    SDL_RenderPresent(renderer);
}

bool process_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            return false;
        }
    }
    return true;
}

void main_loop() {
    if (!process_events()) {
#ifdef __EMSCRIPTEN__
        emscripten_cancel_main_loop();
#endif
        SDL_Quit();
        exit(0);
    }
    
    render();
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        return 1;
    }
    
    window = SDL_CreateWindow("Smasher", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_FULLSCREEN_DESKTOP);
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
    while (process_events()) {
        render();
        SDL_Delay(16);
    }
#endif
    
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    return 0;
}
