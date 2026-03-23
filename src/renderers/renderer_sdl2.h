// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: SDL2 renderer

#pragma once

#include <qdebug.h>
#include <qlogging.h>

#include <SDL.h>

#include "emulator/renderer.h"
#include "emulator/utils.h"

class SDL2Renderer: public VideoRenderer
{
private:
    SDL_Window * SDLWindowRef = nullptr;
    SDL_Renderer * SDLRendererRef = nullptr;
    SDL_Texture * SDLTexture = nullptr;
    SDL_Surface * device_surface = nullptr;
    SDL_Texture * black_box = nullptr;
    SDL_Rect render_rect;
    int cached_window_w = 0;
    int cached_window_h = 0;

    void create_black_box()
    {
        black_box = SDL_CreateTexture(SDLRendererRef, SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_STREAMING, 100, 100);
        if (black_box == nullptr) return;
        unsigned char* black_bytes = nullptr;
        int pitch = 0;
        SDL_LockTexture(black_box, nullptr, reinterpret_cast<void**>(&black_bytes), &pitch);
        memset(black_bytes, 0, 100*100*4);
        SDL_UnlockTexture(black_box);
    }

    void recreate_renderer()
    {
        if (black_box != nullptr) { SDL_DestroyTexture(black_box); black_box = nullptr; }
        if (SDLRendererRef != nullptr) SDL_DestroyRenderer(SDLRendererRef);
        SDLRendererRef = SDL_CreateRenderer(SDLWindowRef, -1, SDL_RENDERER_ACCELERATED);
        if (SDLRendererRef != nullptr) {
            create_black_box();
            SDL_RenderClear(SDLRendererRef);
        }
    }

public:
    SDL2Renderer():
        VideoRenderer()
    {};

    std::string get_name() override
    {
        return "SDL2";
    }

    virtual ~SDL2Renderer() override
    {
        if (black_box != nullptr) SDL_DestroyTexture(black_box);
        if (device_surface != nullptr) SDL_FreeSurface(device_surface);
        if (SDLRendererRef != nullptr) SDL_DestroyRenderer(SDLRendererRef);
        if (SDLWindowRef != nullptr) SDL_DestroyWindow(SDLWindowRef);
    };

    void init_screen(void *p, int sx, int sy, double ss, double ps) override
    {
        VideoRenderer::init_screen(p, sx, sy, ss, ps);

        if (SDLWindowRef == nullptr)
            SDLWindowRef = SDL_CreateWindowFrom(p);
        if (!SDLWindowRef) {
            qWarning() << "SDL error:" << SDL_GetError();
            return;
        }

        recreate_renderer();
        if (!SDLRendererRef) return;

        SDL_RenderCopy(SDLRendererRef, black_box, NULL, NULL);
        render_rect.w = sx * ss * ps;
        render_rect.h = sy * ss;

        SDL_GetWindowSize(SDLWindowRef, &cached_window_w, &cached_window_h);

        device_surface = SDL_CreateRGBSurfaceWithFormat(0, screen_x, screen_y, 32, SDL_PIXELFORMAT_RGBA8888);
    }

    void stop() override
    {
        if (black_box != nullptr) SDL_DestroyTexture(black_box);
        if (device_surface != nullptr) SDL_FreeSurface(device_surface);
        if (SDLRendererRef != nullptr) SDL_DestroyRenderer(SDLRendererRef);

        black_box = nullptr;
        device_surface = nullptr;
        SDLRendererRef = nullptr;
        cached_window_w = 0;
        cached_window_h = 0;
    }

    void set_filtering(int value) override
    {
        std::string s = std::to_string(value);
        char const *pchar = s.c_str();
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, pchar);
    }

    uint8_t * get_buffer() override
    {
        return reinterpret_cast<uint8_t *>(device_surface->pixels);
    }

    int get_line_bytes() override
    {
        return screen_x * 4;
    }

    void fill(uint32_t c) override
    {
        SDL_FillRect(device_surface, NULL, c);
    }

    void resize(int sx, int sy, double ss, double ps) override
    {
        SDL_FreeSurface(device_surface);
        screen_x = sx;
        screen_y = sy;
        screen_ss = ss;
        screen_ps = ps;
        device_surface = SDL_CreateRGBSurfaceWithFormat(0, screen_x, screen_y, 32, SDL_PIXELFORMAT_RGBA8888);
        SDL_RenderCopy(SDLRendererRef, black_box, NULL, NULL);
    }

    void render() override
    {
        if (SDLRendererRef == nullptr || device_surface == nullptr) return;

        int rx, ry;
        SDL_GetWindowSize(SDLWindowRef, &rx, &ry);
        if (rx <= 0 || ry <= 0) return;

        // Recreate the renderer when the window has been resized externally.
        // SDL_CreateWindowFrom windows don't pump events, so the renderer's
        // internal backbuffer and D3D device state become stale after resize.
        if (rx != cached_window_w || ry != cached_window_h) {
            cached_window_w = rx;
            cached_window_h = ry;
            recreate_renderer();
            if (SDLRendererRef == nullptr) return;
        }

        if (screen_ss == 0) {
            int border = 20;
            int rx2 = rx - 2*border;
            int ry2 = ry - 2*border;
            if (rx2 <= 0 || ry2 <= 0) return;
            float screen_aspect = (float)rx2 / ry2;
            float image_aspect = (float)screen_x / screen_y * screen_ps;

            if (screen_aspect > image_aspect) {
                render_rect.w = (float)screen_x * ry2 / screen_y * screen_ps;
                render_rect.h = ry2;
            } else {
                render_rect.w = (float)rx2 * screen_ps;
                render_rect.h = (float)screen_y * rx2 / screen_x;
            }
        } else {
            render_rect.w = screen_x * screen_ss * screen_ps;
            render_rect.h = screen_y * screen_ss;
        }
        render_rect.x = (rx - render_rect.w) / 2;
        render_rect.y = (ry - render_rect.h) / 2;

        SDLTexture = SDL_CreateTextureFromSurface(SDLRendererRef, device_surface);
        if (SDLTexture == nullptr) {
            // Renderer may be in a bad state (e.g. D3D device lost), try recovery
            recreate_renderer();
            if (SDLRendererRef == nullptr) return;
            SDLTexture = SDL_CreateTextureFromSurface(SDLRendererRef, device_surface);
            if (SDLTexture == nullptr) return;
        }

        SDL_RenderClear(SDLRendererRef);
        SDL_RenderCopy(SDLRendererRef, SDLTexture, NULL, &render_rect);
        SDL_RenderPresent(SDLRendererRef);

        SDL_DestroyTexture(SDLTexture);
    }

    uint32_t MapRGB(uint8_t R, uint8_t G, uint8_t B) override
    {
        return SDL_MapRGB(device_surface->format, R, G, B);
    }

    std::vector<uint8_t> get_screenshot() override
    {
        std::vector<uint8_t> image;

        unsigned int bitmap_size = screen_x * screen_y * 4 + 200;

        std::vector<uint8_t> bmp;
        bmp.resize(bitmap_size);

        SDL_RWops *rw;
        rw = SDL_RWFromMem(bmp.data(), bitmap_size);
        SDL_SaveBMP_RW(device_surface, rw, 0);
        unsigned error = decodeBMP(image, (unsigned&)screen_x, (unsigned&)screen_y, bmp);

        return image;
    }


};
