#include <SDL.h>
#include <iostream>

const int SCREEN_WIDTH = 720;
const int SCREEN_HIGHT = 480;

bool Initialized();
void Close();

SDL_Window* window = NULL;
SDL_Surface* screen = NULL;

int main(int argc, char* args[])
{
	if (!Initialized())
	{
		std::cout << "Could not initilized!" << std::endl;
		return 1;
	}

	std::cout << "initilized..." << std::endl;
	bool exit_flag = false;
	SDL_Event event;

	Uint32 bgColor = SDL_MapRGB(screen->format, 0, 0, 0);

	while (!exit_flag)
	{
		while (SDL_PollEvent(&event) != 0)
		{
			if (event.type == SDL_QUIT || event.key.keysym.sym == SDLK_ESCAPE)
			{
				exit_flag = true;
			}
		}

		SDL_FillRect(screen, NULL, bgColor);
		SDL_UpdateWindowSurface(window);
	}

	Close();
	return 0;
}

bool Initialized()
{
	if (SDL_Init(SDL_INIT_EVERYTHING) < 0)
	{
		std::cout << "SDL could not be initilized! SDL_Error: " <<  SDL_GetError() << std::endl;
		return false;
	}

	window = SDL_CreateWindow("SDL Test",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		SCREEN_WIDTH, SCREEN_HIGHT, SDL_WINDOW_SHOWN);

	if (window == NULL)
	{
		std::cout <<  "Window could not be created! SDL_Error: " <<  SDL_GetError() << std::endl;
		return false;
	}

	screen = SDL_GetWindowSurface(window);
	return true;
}

void Close()
{
	SDL_DestroyWindow(window);
	window = NULL;

	SDL_Quit();
}