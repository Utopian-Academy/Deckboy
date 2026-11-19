// Deckboy Pocket: first light. Proves the Android build chain end to end -- SDL3
// built from source by Gradle's CMake, a renderer on the phone's GPU, touch and
// clicker keys -- before any of Deckboy's own engine is brought across.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <string>

namespace {

struct Ink { Uint8 r, g, b; };
constexpr Ink kLight {196, 207, 161};
constexpr Ink kMid {140, 148, 106};
constexpr Ink kDark {74, 80, 56};
constexpr Ink kDeep {26, 30, 20};

struct Pocket {
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  int cue = 3;            // the live cue, 1-based, of six
  Uint64 flashUntil = 0;  // GO / BACK light up briefly when pressed
  bool lastWasGo = true;
  std::string lastInput = "none yet";
  SDL_FRect goRect {}, backRect {};
};

void fill(SDL_Renderer* r, Ink c, const SDL_FRect& rect) {
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
  SDL_RenderFillRect(r, &rect);
}

// SDL's built-in 8x8 font, scaled; good enough until SDL_ttf comes across.
void text(SDL_Renderer* r, Ink c, float x, float y, float scale, const std::string& s) {
  SDL_SetRenderScale(r, scale, scale);
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
  SDL_RenderDebugText(r, x / scale, y / scale, s.c_str());
  SDL_SetRenderScale(r, 1.0f, 1.0f);
}

void go(Pocket& p, const char* how) {
  if (p.cue < 6) ++p.cue;
  p.lastWasGo = true;
  p.flashUntil = SDL_GetTicks() + 180;
  p.lastInput = std::string("GO from ") + how;
}

void back(Pocket& p, const char* how) {
  if (p.cue > 1) --p.cue;
  p.lastWasGo = false;
  p.flashUntil = SDL_GetTicks() + 180;
  p.lastInput = std::string("BACK from ") + how;
}

bool inside(const SDL_FRect& r, float x, float y) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

}  // namespace

SDL_AppResult SDL_AppInit(void** state, int, char**) {
  SDL_SetAppMetadata("Deckboy Pocket", "0.1", "io.github.utopian_academy.deckboy.pocket");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  auto* p = new Pocket;
  *state = p;
  if (!SDL_CreateWindowAndRenderer("Deckboy Pocket", 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                   &p->window, &p->renderer)) {
    SDL_Log("window failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  SDL_SetRenderVSync(p->renderer, 1);
  SDL_Log("Deckboy Pocket: renderer %s", SDL_GetRendererName(p->renderer));
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* state, SDL_Event* e) {
  auto& p = *static_cast<Pocket*>(state);
  switch (e->type) {
    case SDL_EVENT_QUIT:
      return SDL_APP_SUCCESS;
    case SDL_EVENT_KEY_DOWN:
      // Clickers send Right/Left (or Page Down/Up), exactly as on the desk.
      if (e->key.key == SDLK_RIGHT || e->key.key == SDLK_PAGEDOWN) go(p, "a key");
      else if (e->key.key == SDLK_LEFT || e->key.key == SDLK_PAGEUP) back(p, "a key");
      break;
    case SDL_EVENT_FINGER_DOWN: {
      int w = 0, h = 0;
      SDL_GetRenderOutputSize(p.renderer, &w, &h);
      const float x = e->tfinger.x * static_cast<float>(w), y = e->tfinger.y * static_cast<float>(h);
      if (inside(p.goRect, x, y)) go(p, "touch");
      else if (inside(p.backRect, x, y)) back(p, "touch");
      break;
    }
    default:
      break;
  }
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* state) {
  auto& p = *static_cast<Pocket*>(state);
  SDL_Renderer* r = p.renderer;
  int w = 0, h = 0;
  SDL_GetRenderOutputSize(r, &w, &h);
  const float W = static_cast<float>(w), H = static_cast<float>(h);
  const float u = W / 72.0f;  // one "millimetre" of the 72-wide layout

  fill(r, kMid, {0, 0, W, H});
  // the program panel
  const SDL_FRect prog {3 * u, 12 * u, 66 * u, 50 * u};
  fill(r, kDeep, prog);
  fill(r, kLight, {prog.x + u, prog.y + u, prog.w - 2 * u, prog.h - 2 * u});
  text(r, kDeep, prog.x + 3 * u, prog.y + 3 * u, u * 0.5f, "PROGRAM");
  char line[96];
  std::snprintf(line, sizeof line, "CUE %d OF 6", p.cue);
  text(r, kDeep, prog.x + 3 * u, prog.y + 14 * u, u * 0.9f, line);
  text(r, kDark, prog.x + 3 * u, prog.y + 30 * u, u * 0.35f, std::string("renderer: ") + SDL_GetRendererName(r));
  std::snprintf(line, sizeof line, "screen: %d x %d", w, h);
  text(r, kDark, prog.x + 3 * u, prog.y + 35 * u, u * 0.35f, line);
  text(r, kDark, prog.x + 3 * u, prog.y + 40 * u, u * 0.35f, "last: " + p.lastInput);

  // the cue list
  const SDL_FRect list {3 * u, 66 * u, 66 * u, 44 * u};
  fill(r, kDeep, list);
  fill(r, kLight, {list.x + u, list.y + u, list.w - 2 * u, list.h - 2 * u});
  static const char* kNames[6] = {"Walk-in loop", "House to half", "Welcome film",
                                  "Lower Third: host", "Walk-up music", "Sponsor reel"};
  for (int i = 0; i < 6; ++i) {
    const float y = list.y + 3 * u + static_cast<float>(i) * 6.5f * u;
    const bool live = (i + 1 == p.cue);
    if (live) fill(r, kDeep, {list.x + 2 * u, y - u, list.w - 4 * u, 6 * u});
    std::snprintf(line, sizeof line, "%d  %s", i + 1, kNames[i]);
    text(r, live ? kLight : kDeep, list.x + 4 * u, y + 0.5f * u, u * 0.45f, line);
  }

  // the button strip
  const bool flash = SDL_GetTicks() < p.flashUntil;
  p.goRect = {40 * u, H - 48 * u, 28 * u, 28 * u};
  p.backRect = {6 * u, H - 40 * u, 24 * u, 24 * u};
  fill(r, (flash && p.lastWasGo) ? kLight : kDeep, p.goRect);
  fill(r, (flash && !p.lastWasGo) ? kLight : kDark, p.backRect);
  text(r, (flash && p.lastWasGo) ? kDeep : kLight, p.goRect.x + 7 * u, p.goRect.y + 10 * u, u, "GO");
  text(r, (flash && !p.lastWasGo) ? kDeep : kLight, p.backRect.x + 4 * u, p.backRect.y + 10 * u, u * 0.55f, "BACK");
  text(r, kDeep, 3 * u, 4 * u, u * 0.55f, "DECKBOY POCKET");

  SDL_RenderPresent(r);
  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* state, SDL_AppResult) {
  auto* p = static_cast<Pocket*>(state);
  if (p) {
    SDL_DestroyRenderer(p->renderer);
    SDL_DestroyWindow(p->window);
    delete p;
  }
}
