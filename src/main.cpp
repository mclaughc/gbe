#include "system.h"
#include "cartridge.h"
#include "display.h"
#include "YBaseLib/ByteStream.h"
#include "YBaseLib/AutoReleasePtr.h"
#include "YBaseLib/Error.h"
#include "YBaseLib/Log.h"
#include "YBaseLib/FileSystem.h"
#include "YBaseLib/CString.h"
#include "YBaseLib/Thread.h"
#include "YBaseLib/Math.h"
#include <SDL/SDL.h>
#include <cstdio>
Log_SetChannel(Main);

struct ProgramArgs
{
    const char *bios_filename;
    const char *cart_filename;
    bool disable_bios;
};

struct State : public System::CallbackInterface
{
    Cartridge *cart;
    const byte *bios;

    System *system;

    SDL_Window *window;
    SDL_Surface *surface;
    SDL_Surface *offscreen_surface;

    bool running;

//     virtual void Sleep(uint32 duration_ms) override
//     {
//         throw std::logic_error("The method or operation is not implemented.");
//     }
// 
//     virtual void PreExecuteIteration() override
//     {
// 
//     }
// 
//     virtual void PostExecuteIteration() override
//     {
// 
//     }

    void SetScale(uint32 scale)
    {
        scale = Max(scale, (uint32)1);

        if (offscreen_surface != nullptr)
        {
            SDL_FreeSurface(offscreen_surface);
            offscreen_surface = nullptr;
        }

        if (scale > 1)
        {
            offscreen_surface = SDL_CreateRGBSurface(0, 160, 144, 32, 0xff, 0xff00, 0xff0000, 0);
            DebugAssert(offscreen_surface != nullptr);
        }

        SDL_SetWindowSize(window, 160 * scale, 144 * scale);
        surface = SDL_GetWindowSurface(window);
    }

    // Callback to present a frame
    virtual void PresentDisplayBuffer(const void *pixels, uint32 row_stride) override final
    {
        const byte *pBytePixels = reinterpret_cast<const byte *>(pixels);
        SDL_Surface *write_surface = (offscreen_surface != nullptr) ? offscreen_surface : surface;
        if (SDL_MUSTLOCK(write_surface))
            SDL_LockSurface(write_surface);

        for (uint32 y = 0; y < Display::SCREEN_HEIGHT; y++)
        {
            const byte *inLine = pBytePixels + (y * row_stride);
            byte *outLine = (byte *)write_surface->pixels + (y * (uint32)write_surface->pitch);

            for (uint32 x = 0; x < Display::SCREEN_WIDTH; x++)
            {
                outLine[0] = inLine[2];
                outLine[1] = inLine[1];
                outLine[2] = inLine[0];

                inLine += 4;
                outLine += 4;
            }
        }

        if (offscreen_surface != nullptr)
        {
            if (SDL_MUSTLOCK(surface))
                SDL_LockSurface(surface);

            SDL_BlitScaled(offscreen_surface, nullptr, surface, nullptr);

            if (SDL_MUSTLOCK(surface))
                SDL_UnlockSurface(surface);
        }

        if (SDL_MUSTLOCK(write_surface))
            SDL_UnlockSurface(write_surface);

        SDL_UpdateWindowSurface(window);
    }
};

static bool LoadBIOS(const char *filename, bool specified, State *state)
{
    AutoReleasePtr<ByteStream> pStream = FileSystem::OpenFile(filename, BYTESTREAM_OPEN_READ | BYTESTREAM_OPEN_STREAMED);
    if (pStream == nullptr)
    {
        if (specified)
            Log_ErrorPrintf("Failed to load bios file '%s'", filename);

        return false;
    }

    if (pStream->GetSize() != GB_BIOS_LENGTH)
    {
        Log_ErrorPrintf("Bios file '%s' is incorrect length (%u bytes, should be %u bytes)", filename, (uint32)pStream->GetSize(), GB_BIOS_LENGTH);
        return false;
    }

    state->bios = new byte[GB_BIOS_LENGTH];
    if (!pStream->Read2((byte *)state->bios, GB_BIOS_LENGTH))
    {
        Log_ErrorPrintf("Failed to read bios file '%s'", filename);
        return false;
    }

    Log_InfoPrintf("Loaded bios file '%s'.", filename);
    return true;
}

static bool LoadCart(const char *filename, State *state)
{
    AutoReleasePtr<ByteStream> pStream = FileSystem::OpenFile(filename, BYTESTREAM_OPEN_READ | BYTESTREAM_OPEN_STREAMED);
    if (pStream == nullptr)
    {
        Log_ErrorPrintf("Failed to open cartridge file '%s'", filename);
        return false;
    }

    state->cart = new Cartridge();
    Error error;
    if (!state->cart->Load(pStream, &error))
    {
        Log_ErrorPrintf("Failed to load cartridge file '%s': %s", filename, error.GetErrorDescription().GetCharArray());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Cart load error", error.GetErrorCodeAndDescription(), nullptr);
        return false;
    }

    return true;
}

static void ShowUsage(const char *progname)
{
    fprintf(stderr, "gbe\n");
    fprintf(stderr, "usage: %s [-h] [-b <bios file>] [-db] [cart file]\n", progname);

}

static bool ParseArguments(int argc, char *argv[], ProgramArgs *out_args)
{
#define CHECK_ARG(str) !Y_strcmp(argv[i], str)
#define CHECK_ARG_PARAM(str) !Y_strcmp(argv[i], str) && ((i + 1) < argc)

    out_args->bios_filename = nullptr;
    out_args->cart_filename = nullptr;
    out_args->disable_bios = false;

    for (int i = 1; i < argc; i++)
    {
        if (CHECK_ARG("-h") || CHECK_ARG("-?"))
        {
            ShowUsage(argv[0]);
            return false;
        }
        else if (CHECK_ARG_PARAM("-b"))
        {
            out_args->bios_filename = argv[++i];
        }
        else if (CHECK_ARG("-db"))
        {
            out_args->disable_bios = true;
        }
        else
        {
            out_args->cart_filename = argv[i];
        }
    }

    return true;

#undef CHECK_ARG
#undef CHECK_ARG_PARAM
}

static bool InitializeState(const ProgramArgs *args, State *state)
{
    state->bios = nullptr;
    state->cart = nullptr;
    state->system = nullptr;
    state->window = nullptr;
    state->surface = nullptr;
    state->offscreen_surface = nullptr;
    state->running = true;

    // load bios
    bool bios_specified = (args->bios_filename != nullptr);
    const char *bios_filename = (args->bios_filename != nullptr) ? args->bios_filename : "bios.bin";
    if (!args->disable_bios && !LoadBIOS(bios_filename, bios_specified, state) && bios_specified)
        return false;

    // load cart
    if (args->cart_filename != nullptr && !LoadCart(args->cart_filename, state))
        return false;

    // create render window
    SmallString window_title;
    window_title.Format("gbe - %s", state->cart->GetName().GetCharArray());
    state->window = SDL_CreateWindow(window_title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, Display::SCREEN_WIDTH, Display::SCREEN_HEIGHT, 0);
    if (state->window == nullptr)
    {
        Log_ErrorPrintf("Failed to crate SDL window: %s", SDL_GetError());
        return false;
    }

    // get surface to draw to
    state->surface = SDL_GetWindowSurface(state->window);
    if (state->surface == nullptr)
        return false;

    // init system
    state->system = new System();
    if (!state->system->Init(state, state->bios, state->cart))
    {
        Log_ErrorPrintf("Failed to initialize system");
        return false;
    }

    // reset system
    state->system->Reset();
    return true;
}

static void CleanupState(State *state)
{
    delete[] state->bios;
    delete state->cart;
    delete state->system;
    if (state->window != nullptr)
        SDL_DestroyWindow(state->window);
}


static int Run(State *state)
{
    Timer time_since_last_report;

    while (state->running)
    {
        SDL_PumpEvents();
        for (;;)
        {
            SDL_Event events[16];
            int nevents = SDL_PeepEvents(events, countof(events), SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
            if (nevents == 0)
                break;

            for (int i = 0; i < nevents; i++)
            {
                const SDL_Event *event = events + i;
                switch (event->type)
                {
                case SDL_QUIT:
                    state->running = false;
                    break;

                case SDL_KEYDOWN:
                case SDL_KEYUP:
                    {
                        bool down = (event->type == SDL_KEYDOWN);
                        switch (event->key.keysym.sym)
                        {
                        case SDLK_w:
                        case SDLK_UP:
                            state->system->SetPadDirection(PAD_DIRECTION_UP, down);
                            break;

                        case SDLK_a:
                        case SDLK_LEFT:
                            state->system->SetPadDirection(PAD_DIRECTION_LEFT, down);
                            break;

                        case SDLK_s:
                        case SDLK_DOWN:
                            state->system->SetPadDirection(PAD_DIRECTION_DOWN, down);
                            break;

                        case SDLK_d:
                        case SDLK_RIGHT:
                            state->system->SetPadDirection(PAD_DIRECTION_RIGHT, down);
                            break;

                        case SDLK_z:
                            state->system->SetPadButton(PAD_BUTTON_B, down);
                            break;

                        case SDLK_x:
                            state->system->SetPadButton(PAD_BUTTON_A, down);
                            break;

                        case SDLK_c:
                            state->system->SetPadButton(PAD_BUTTON_SELECT, down);
                            break;

                        case SDLK_v:
                            state->system->SetPadButton(PAD_BUTTON_START, down);
                            break;

                        case SDLK_1:
                        case SDLK_2:
                        case SDLK_3:
                        case SDLK_4:
                        case SDLK_5:
                        case SDLK_6:
                        case SDLK_7:
                        case SDLK_8:
                        case SDLK_9:
                            {
                                if (!down)
                                    state->SetScale((event->key.keysym.sym - SDLK_1) + 1);

                                break;
                            }

                        case SDLK_KP_PLUS:
                            {
                                if (down)
                                {
                                    state->system->SetTargetSpeed(state->system->GetTargetSpeed() + 0.25f);
                                    Log_DevPrintf("Target speed set to %.2f%%", state->system->GetTargetSpeed() * 100.0f);
                                }

                                break;
                            }

                        case SDLK_KP_MINUS:
                            {
                                if (down)
                                {
                                    state->system->SetTargetSpeed(state->system->GetTargetSpeed() - 0.25f);
                                    Log_DevPrintf("Target speed set to %.2f%%", state->system->GetTargetSpeed() * 100.0f);
                                }

                                break;
                            }

                        case SDLK_KP_PERIOD:
                            {
                                if (!down)
                                {
                                    state->system->SetAccurateTiming(!state->system->GetAccurateTiming());
                                    Log_DevPrintf("Set accurate timing %s", state->system->GetAccurateTiming() ? "on" : "off");
                                }

                                break;
                            }

                        case SDLK_KP_ENTER:
                            {
                                if (!down)
                                {
                                    state->system->SetFrameLimiter(!state->system->GetFrameLimiter());
                                    Log_DevPrintf("Set framelimiter %s", state->system->GetFrameLimiter() ? "on" : "off");
                                }

                                break;
                            }
                        }

                        break;
                    }
                }
            }
        }

        // run a frame
        double sleep_time_seconds = state->system->ExecuteFrame();
        if (sleep_time_seconds >= 0.01)
        {
            // round down to the next millisecond (fix when usleep is implemented)
            //uint32 sleep_time_ms = (uint32)(Math::Truncate(Math::Floor(sleep_time_seconds * 1000.0f)));
            uint32 sleep_time_ms = (uint32)std::floor(sleep_time_seconds * 1000.0);
            if (sleep_time_ms > 1)
                Thread::Sleep(sleep_time_ms - 1);
            //Thread::Sleep(1);
        }

        // report statistics
        if (time_since_last_report.GetTimeSeconds() > 1.0)
        {
            Log_DevPrintf("Current frame: %u, emulation speed: %.3f%%, target emulation speed: %.3f%%", state->system->GetFrameCounter() + 1, state->system->GetCurrentSpeed() * 100.0f, state->system->GetTargetSpeed() * 100.0f);
            time_since_last_report.Reset();
        }

        // update window title
        {
            SmallString window_title;
            window_title.Format("gbe - %s - Frame %u - %.0f%%", state->cart->GetName().GetCharArray(), state->system->GetFrameCounter() + 1, state->system->GetCurrentSpeed() * 100.0f);
            SDL_SetWindowTitle(state->window, window_title);
        }
    }

    return 0;
}

// SDL requires the entry point declared without c++ decoration
extern "C" int main(int argc, char *argv[])
{
    // set log flags
    g_pLog->SetConsoleOutputParams(true, nullptr, LOGLEVEL_PROFILE);
    //g_pLog->SetConsoleOutputParams(true, "CPU System");
    //g_pLog->SetDebugOutputParams(true);

#if defined(__WIN32__)
    // fix up stdout/stderr on win32
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
#endif

    // init sdl
    if (SDL_Init(0) < 0)
    {
        Panic("SDL initialization failed");
        return -1;
    }

    // parse args
    ProgramArgs args;
    if (!ParseArguments(argc, argv, &args))
        return 1;

    // init state
    State state;
    if (!InitializeState(&args, &state))
    {
        CleanupState(&state);
        return 2;
    }

    // run
    int return_code = Run(&state);

    // cleanup
    CleanupState(&state);
    SDL_Quit();
    return return_code;
}
