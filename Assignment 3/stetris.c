#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <linux/input.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <dirent.h>

// The game state can be used to detect what happens on the playfield
#define GAMEOVER 0
#define ACTIVE (1 << 0)
#define ROW_CLEAR (1 << 1)
#define TILE_ADDED (1 << 2)

// If you extend this structure, either avoid pointers or adjust
// the game logic allocate/deallocate and reset the memory
typedef struct
{
	bool occupied;
	u_int16_t color;
} tile;

typedef struct
{
	unsigned int x;
	unsigned int y;
} coord;

typedef struct
{
	coord const grid;					  // playfield bounds
	unsigned long const uSecTickTime;	  // tick rate
	unsigned long const rowsPerLevel;	  // speed up after clearing rows
	unsigned long const initNextGameTick; // initial value of nextGameTick

	unsigned int tiles; // number of tiles played
	unsigned int rows;	// number of rows cleared
	unsigned int score; // game score
	unsigned int level; // game level

	tile *rawPlayfield; // pointer to raw memory of the playfield
	tile **playfield;	// This is the play field array
	unsigned int state;
	coord activeTile; // current tile

	unsigned long tick;			// incremeted at tickrate, wraps at nextGameTick
								// when reached 0, next game state calculated
	unsigned long nextGameTick; // sets when tick is wrapping back to zero
								// lowers with increasing level, never reaches 0
} gameConfig;

gameConfig game = {
	.grid = {8, 8},
	.uSecTickTime = 10000,
	.rowsPerLevel = 2,
	.initNextGameTick = 50,
};

int fbfd; // File descriptor for framebuffer
struct fb_fix_screeninfo fixed_screen_info;
u_int16_t *fbmapping;

int jsfd; // File descritor for joystick

// This function is called on the start of your application
// Here you can initialize what ever you need for your task
// return false if something fails, else true
bool initializeSenseHat()
{
	bool fb = false; // Was filebuffer initialized?
	bool js = false; // Was joystick initialized?
	DIR *directory;
	struct dirent *entry;

	directory = opendir("/dev/");
	if (directory)
	{
		char path[256];
		while ((entry = readdir(directory)) != NULL)
		{
			snprintf(path, 256, "/dev/%s", entry->d_name);
			fbfd = open(path, O_RDWR);
			if (ioctl(fbfd, FBIOGET_FSCREENINFO, &fixed_screen_info) == -1) // If ioctl fails, close the file descriptor and continue
			{
				close(fbfd);
				continue;
			}
			if (strcmp(fixed_screen_info.id, "RPi-Sense FB") == 0) // Check that the framebuffer is the Sense HAT framebuffer
			{
				size_t fb_size = 8 * 8 * 2; // 8x8 pixels, 2 bytes per pixel
				fbmapping = mmap(NULL, fb_size,
								 PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, (off_t)0);
				fb = true;
				break;
			}
			close(fbfd);
		}
		closedir(directory);
	}

	directory = opendir("/dev/input");
	if (directory)
	{
		char path[256];
		char devicename[256];
		while ((entry = readdir(directory)) != NULL)
		{
			snprintf(path, 256, "/dev/input/%s", entry->d_name);
			jsfd = open(path, O_RDONLY | O_NONBLOCK);						   // Open the joystick in non-blocking mode
			if (ioctl(jsfd, EVIOCGNAME(sizeof(devicename)), devicename) == -1) // If ioctl fails, close the file descriptor and continue
			{
				close(jsfd);
				continue;
			}
			if (strcmp(devicename, "Raspberry Pi Sense HAT Joystick") == 0) // Check that the joystick is the Sense HAT joystick
			{
				js = true;
				break;
			}
			close(jsfd);
		}
		closedir(directory);
	}

	return fb && js; // Return false if either joystick or framebuffer was not initialized
}

// This function is called when the application exits
// Here you can free up everything that you might have opened/allocated
void freeSenseHat()
{
	memset(fbmapping, 0, 128);
	munmap(fbmapping, 128);
	close(fbfd);
	close(jsfd);
}

// This function should return the key that corresponds to the joystick press
// KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, with the respective direction
// and KEY_ENTER, when the the joystick is pressed
// !!! when nothing was pressed you MUST return 0 !!!
int readSenseHatJoystick()
{
	struct input_event input;
	struct pollfd poll_joystick = {// Create a pollfd struct for the joystick. POLLIN says we are interested in any new data
								   .fd = jsfd,
								   .events = POLLIN,
								   .revents = 0};

	if (poll(&poll_joystick, 1, 0) > 0) // If there is an event
	{
		read(jsfd, &input, sizeof(struct input_event));
		if (input.type == EV_KEY && (input.value == 1 || input.value == 2)) // If the event is a key press or hold
		{
			return input.code; // Return the key code
		}
	}

	return 0;
}

// This function should render the gamefield on the LED matrix. It is called
// every game tick. The parameter playfieldChanged signals whether the game logic
// has changed the playfield
void renderSenseHatMatrix(bool const playfieldChanged)
{
	if (playfieldChanged)
	{
		memset(fbmapping, 0, 128); // Clear the screen
	}
	else
	{
		return; // If nothing changed we don't need to do anything
	}

	for (size_t i = 0; i < game.grid.y; i++) // row
	{
		for (size_t j = 0; j < game.grid.x; j++) // column
		{
			if (game.playfield[i][j].occupied)
			{
				fbmapping[i * 8 + j] = game.playfield[i][j].color; // Each row is 16 bytes long, while each column is 2 bytes long.
			}
		}
	}
}

// The game logic uses only the following functions to interact with the playfield.
// if you choose to change the playfield or the tile structure, you might need to
// adjust this game logic <> playfield interface

static inline void newTile(coord const target)
{
	u_int16_t color;
	switch (game.tiles % 7) // Cycle through the 7 colors depending on the number of tiles played
	{
	case 0:
		color = 2036;
		break;
	case 1:
		color = 63488;
		break;
	case 2:
		color = 65472;
		break;
	case 3:
		color = 10271;
		break;
	case 4:
		color = 63518;
		break;
	case 5:
		color = 12256;
		break;
	case 6:
		color = 64640;
		break;

	default:
		break;
	}
	game.playfield[target.y][target.x].occupied = true;
	game.playfield[target.y][target.x].color = color;
}

static inline void copyTile(coord const to, coord const from)
{
	memcpy((void *)&game.playfield[to.y][to.x], (void *)&game.playfield[from.y][from.x], sizeof(tile));
}

static inline void copyRow(unsigned int const to, unsigned int const from)
{
	memcpy((void *)&game.playfield[to][0], (void *)&game.playfield[from][0], sizeof(tile) * game.grid.x);
}

static inline void resetTile(coord const target)
{
	memset((void *)&game.playfield[target.y][target.x], 0, sizeof(tile));
}

static inline void resetRow(unsigned int const target)
{
	memset((void *)&game.playfield[target][0], 0, sizeof(tile) * game.grid.x);
}

static inline bool tileOccupied(coord const target)
{
	return game.playfield[target.y][target.x].occupied;
}

static inline bool rowOccupied(unsigned int const target)
{
	for (unsigned int x = 0; x < game.grid.x; x++)
	{
		coord const checkTile = {x, target};
		if (!tileOccupied(checkTile))
		{
			return false;
		}
	}
	return true;
}

static inline void resetPlayfield()
{
	for (unsigned int y = 0; y < game.grid.y; y++)
	{
		resetRow(y);
	}
}

// Below here comes the game logic. Keep in mind: You are not allowed to change how the game works!
// that means no changes are necessary below this line! And if you choose to change something
// keep it compatible with what was provided to you!

bool addNewTile()
{
	game.activeTile.y = 0;
	game.activeTile.x = (game.grid.x - 1) / 2;
	if (tileOccupied(game.activeTile))
		return false;
	newTile(game.activeTile);
	return true;
}

bool moveRight()
{
	coord const newTile = {game.activeTile.x + 1, game.activeTile.y};
	if (game.activeTile.x < (game.grid.x - 1) && !tileOccupied(newTile))
	{
		copyTile(newTile, game.activeTile);
		resetTile(game.activeTile);
		game.activeTile = newTile;
		return true;
	}
	return false;
}

bool moveLeft()
{
	coord const newTile = {game.activeTile.x - 1, game.activeTile.y};
	if (game.activeTile.x > 0 && !tileOccupied(newTile))
	{
		copyTile(newTile, game.activeTile);
		resetTile(game.activeTile);
		game.activeTile = newTile;
		return true;
	}
	return false;
}

bool moveDown()
{
	coord const newTile = {game.activeTile.x, game.activeTile.y + 1};
	if (game.activeTile.y < (game.grid.y - 1) && !tileOccupied(newTile))
	{
		copyTile(newTile, game.activeTile);
		resetTile(game.activeTile);
		game.activeTile = newTile;
		return true;
	}
	return false;
}

bool clearRow()
{
	if (rowOccupied(game.grid.y - 1))
	{
		for (unsigned int y = game.grid.y - 1; y > 0; y--)
		{
			copyRow(y, y - 1);
		}
		resetRow(0);
		return true;
	}
	return false;
}

void advanceLevel()
{
	game.level++;
	switch (game.nextGameTick)
	{
	case 1:
		break;
	case 2 ... 10:
		game.nextGameTick--;
		break;
	case 11 ... 20:
		game.nextGameTick -= 2;
		break;
	default:
		game.nextGameTick -= 10;
	}
}

void newGame()
{
	game.state = ACTIVE;
	game.tiles = 0;
	game.rows = 0;
	game.score = 0;
	game.tick = 0;
	game.level = 0;
	resetPlayfield();
}

void gameOver()
{
	game.state = GAMEOVER;
	game.nextGameTick = game.initNextGameTick;
}

bool sTetris(int const key)
{
	bool playfieldChanged = false;

	if (game.state & ACTIVE)
	{
		// Move the current tile
		if (key)
		{
			playfieldChanged = true;
			switch (key)
			{
			case KEY_LEFT:
				moveLeft();
				break;
			case KEY_RIGHT:
				moveRight();
				break;
			case KEY_DOWN:
				while (moveDown())
				{
				};
				game.tick = 0;
				break;
			default:
				playfieldChanged = false;
			}
		}

		// If we have reached a tick to update the game
		if (game.tick == 0)
		{
			// We communicate the row clear and tile add over the game state
			// clear these bits if they were set before
			game.state &= ~(ROW_CLEAR | TILE_ADDED);

			playfieldChanged = true;
			// Clear row if possible
			if (clearRow())
			{
				game.state |= ROW_CLEAR;
				game.rows++;
				game.score += game.level + 1;
				if ((game.rows % game.rowsPerLevel) == 0)
				{
					advanceLevel();
				}
			}

			// if there is no current tile or we cannot move it down,
			// add a new one. If not possible, game over.
			if (!tileOccupied(game.activeTile) || !moveDown())
			{
				if (addNewTile())
				{
					game.state |= TILE_ADDED;
					game.tiles++;
				}
				else
				{
					gameOver();
				}
			}
		}
	}

	// Press any key to start a new game
	if ((game.state == GAMEOVER) && key)
	{
		playfieldChanged = true;
		newGame();
		addNewTile();
		game.state |= TILE_ADDED;
		game.tiles++;
	}

	return playfieldChanged;
}

int readKeyboard()
{
	struct pollfd pollStdin = {
		.fd = STDIN_FILENO,
		.events = POLLIN};
	int lkey = 0;

	if (poll(&pollStdin, 1, 0))
	{
		lkey = fgetc(stdin);
		if (lkey != 27)
			goto exit;
		lkey = fgetc(stdin);
		if (lkey != 91)
			goto exit;
		lkey = fgetc(stdin);
	}
exit:
	switch (lkey)
	{
	case 10:
		return KEY_ENTER;
	case 65:
		return KEY_UP;
	case 66:
		return KEY_DOWN;
	case 67:
		return KEY_RIGHT;
	case 68:
		return KEY_LEFT;
	}
	return 0;
}

void renderConsole(bool const playfieldChanged)
{
	if (!playfieldChanged)
		return;

	// Goto beginning of console
	fprintf(stdout, "\033[%d;%dH", 0, 0);
	for (unsigned int x = 0; x < game.grid.x + 2; x++)
	{
		fprintf(stdout, "-");
	}
	fprintf(stdout, "\n");
	for (unsigned int y = 0; y < game.grid.y; y++)
	{
		fprintf(stdout, "|");
		for (unsigned int x = 0; x < game.grid.x; x++)
		{
			coord const checkTile = {x, y};
			fprintf(stdout, "%c", (tileOccupied(checkTile)) ? '#' : ' ');
		}
		switch (y)
		{
		case 0:
			fprintf(stdout, "| Tiles: %10u\n", game.tiles);
			break;
		case 1:
			fprintf(stdout, "| Rows:  %10u\n", game.rows);
			break;
		case 2:
			fprintf(stdout, "| Score: %10u\n", game.score);
			break;
		case 4:
			fprintf(stdout, "| Level: %10u\n", game.level);
			break;
		case 7:
			fprintf(stdout, "| %17s\n", (game.state == GAMEOVER) ? "Game Over" : "");
			break;
		default:
			fprintf(stdout, "|\n");
		}
	}
	for (unsigned int x = 0; x < game.grid.x + 2; x++)
	{
		fprintf(stdout, "-");
	}
	fflush(stdout);
}

inline unsigned long uSecFromTimespec(struct timespec const ts)
{
	return ((ts.tv_sec * 1000000) + (ts.tv_nsec / 1000));
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	// This sets the stdin in a special state where each
	// keyboard press is directly flushed to the stdin and additionally
	// not outputted to the stdout
	{
		struct termios ttystate;
		tcgetattr(STDIN_FILENO, &ttystate);
		ttystate.c_lflag &= ~(ICANON | ECHO);
		ttystate.c_cc[VMIN] = 1;
		tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);
	}

	// Allocate the playing field structure
	game.rawPlayfield = (tile *)malloc(game.grid.x * game.grid.y * sizeof(tile));
	game.playfield = (tile **)malloc(game.grid.y * sizeof(tile *));
	if (!game.playfield || !game.rawPlayfield)
	{
		fprintf(stderr, "ERROR: could not allocate playfield\n");
		return 1;
	}
	for (unsigned int y = 0; y < game.grid.y; y++)
	{
		game.playfield[y] = &(game.rawPlayfield[y * game.grid.x]);
	}

	// Reset playfield to make it empty
	resetPlayfield();
	// Start with gameOver
	gameOver();

	if (!initializeSenseHat())
	{
		fprintf(stderr, "ERROR: could not initilize sense hat\n");
		return 1;
	};

	// Clear console, render first time
	fprintf(stdout, "\033[H\033[J");
	renderConsole(true);
	renderSenseHatMatrix(true);

	while (true)
	{
		struct timeval sTv, eTv;
		gettimeofday(&sTv, NULL);

		int key = readSenseHatJoystick();
		if (!key)
			key = readKeyboard();
		if (key == KEY_ENTER)
			break;

		bool playfieldChanged = sTetris(key);
		renderConsole(playfieldChanged);
		renderSenseHatMatrix(playfieldChanged);

		// Wait for next tick
		gettimeofday(&eTv, NULL);
		unsigned long const uSecProcessTime = ((eTv.tv_sec * 1000000) + eTv.tv_usec) - ((sTv.tv_sec * 1000000 + sTv.tv_usec));
		if (uSecProcessTime < game.uSecTickTime)
		{
			usleep(game.uSecTickTime - uSecProcessTime);
		}
		game.tick = (game.tick + 1) % game.nextGameTick;
	}

	freeSenseHat();
	free(game.playfield);
	free(game.rawPlayfield);

	return 0;
}