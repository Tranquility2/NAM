#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <iostream>
#include <string>
#include <vector>

const int SCREEN_WIDTH = 720;
const int SCREEN_HIGHT = 480;

bool Initialized();
void Close();
SDL_Texture* LoadTexture(std::string file);
SDL_Texture* TextTexture(std::string text);

SDL_Window* window = NULL;
//SDL_Surface* screen = NULL;
SDL_Renderer* renderer = NULL;
SDL_Texture* texture = NULL;

int main(int argc, char* args[])
{
	if (!Initialized())
	{
		std::cout << "Could not initialize!" << std::endl;
		return 1;
	}

	std::cout << "Initialized..." << std::endl;

	std::string image_file_name = "Sdl-logo.png";
	texture = LoadTexture(image_file_name.c_str());
	//texture = TextTexture("SDL Test");
	if (texture == NULL)
	{
		return 1;
	}

	int w, h;
	SDL_QueryTexture(texture, NULL, NULL, &w, &h);

	bool exit_flag = false;
	SDL_Event event;

	SDL_Rect screenRectangle = { 0, 0, SCREEN_WIDTH, SCREEN_HIGHT };
	SDL_Rect imageRectangle = { 0, 0 , w / 2 ,h / 2 };

	std::vector<int> vector_2d {1,1};

	while (!exit_flag)
	{
		while (SDL_PollEvent(&event) != 0)
		{
			if (event.type == SDL_QUIT || event.key.keysym.sym == SDLK_ESCAPE)
			{
				exit_flag = true;
			}
		}

		SDL_RenderClear(renderer);
		SDL_RenderCopy(renderer, texture, &screenRectangle, &imageRectangle);
		SDL_RenderPresent(renderer);

		imageRectangle.x += vector_2d[0];
		imageRectangle.y += vector_2d[1];

		if (imageRectangle.x >= SCREEN_WIDTH - w / 2 || imageRectangle.x <= 0)
		{
			vector_2d[0] *= -1;
		}
		else if (imageRectangle.y >= SCREEN_HIGHT - h / 2 || imageRectangle.y <= 0)
		{
			vector_2d[1] *= -1;
		}
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

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

	if (renderer == NULL)
	{
		std::cout << "Renderer could not be created! SDL_Error: " << SDL_GetError() << std::endl;
		return false;
	}

	SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
	
	if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG))
	{
		std::cout << "SDL_Image could not be  initilized! SDL_Image_Error: " << IMG_GetError() << std::endl;
		return false;
	}

	if (TTF_Init() < 0)
	{
		std::cout << "SDL_TTF could not be initilized! SDL_TTF_Error: " << TTF_GetError() << std::endl;
		return false;
	}

	return true;
}

void Close()
{
	SDL_DestroyWindow(window);
	window = NULL;

	SDL_Quit();
}

SDL_Texture *LoadTexture(std::string file)
{
	SDL_Texture* newTexture = NULL;
	SDL_Surface* loadedSurface = IMG_Load(file.c_str());

	if (loadedSurface == NULL)
	{
		std::cout << "Unable to load the image " << file.c_str() << "! SDL_Image_Error: " << IMG_GetError() << std::endl;
	}
	else
	{
		std::cout << "Loaded the image " << file.c_str() << std::endl;

		newTexture = SDL_CreateTextureFromSurface(renderer, loadedSurface);
		
		if (newTexture == NULL)
		{
			std::cout << "Unable to create the texture from " << file.c_str() << "! SDL_Image_Error: " << IMG_GetError() << std::endl;
		}

		SDL_FreeSurface(loadedSurface);
	}

	return newTexture;
}

SDL_Texture *TextTexture(std::string text)
{
	SDL_Texture* newTexture = NULL;
	TTF_Font* Sans = NULL;

	Sans = TTF_OpenFont("Sans.ttf", 12);
	SDL_Color Black = { 0, 0, 0 };

	SDL_Surface* surfaceText = TTF_RenderText_Solid(Sans, text.c_str(), Black);

	if (surfaceText == NULL)
	{
		std::cout << "Unable to render the text " << text.c_str() << "! SDL_TTF_Error: " << TTF_GetError() << std::endl;
	}
	else
	{
		std::cout << "Rendered the text " << text.c_str() << std::endl;

		newTexture = SDL_CreateTextureFromSurface(renderer, surfaceText);

		if (newTexture == NULL)
		{
			std::cout << "Unable to create the texture from " << text.c_str() << "! SDL_TTF_Error: " << TTF_GetError() << std::endl;
		}

		SDL_FreeSurface(surfaceText);
	}

	return newTexture;
}
