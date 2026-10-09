/*
MACOS_SPIKE.C

The native half of the macOS port feasibility test.  It intentionally does
not load the game: that must wait for the separate ILP32 guest ABI and
renderer work described in docs/macos-port-audit.md.
*/

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

static int run_spike(int automatic_close)
{
	SDL_Window *window = NULL;
	int running = 1;
	unsigned int frames = 0;

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD))
	{
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}
	window = SDL_CreateWindow("Halo CE macOS platform spike", 960, 540,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	if (!window)
	{
		fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}
	while (running)
	{
		SDL_Event event;

		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
				(event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE))
				running = 0;
		}
		if (automatic_close && ++frames == 30)
			running = 0;
		SDL_Delay(16);
	}
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}

int main(int argc, char **argv)
{
	int automatic_close = argc == 2 && strcmp(argv[1], "--smoke") == 0;

	if (argc > 1 && !automatic_close)
	{
		fprintf(stderr, "usage: %s [--smoke]\n", argv[0]);
		return 2;
	}
	return run_spike(automatic_close);
}
