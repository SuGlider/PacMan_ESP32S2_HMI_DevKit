/******************************************************************************/
/*                                                                            */
/*  PACMAN GAME FOR ARDUINO DUE                                               */
/*                                                                            */
/******************************************************************************/
/*  Copyright (c) 2014  Dr. NCX (mirracle.mxx@gmail.com)                      */
/*                                                                            */
/* THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL              */
/* WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED              */
/* WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR    */
/* BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES      */
/* OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,     */
/* WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,     */
/* ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS        */
/* SOFTWARE.                                                                  */
/*                                                                            */
/*  MIT license, all text above must be included in any redistribution.       */


/*
    Controller configuration:
    Buttons UP, RIGHT, DOWN, LEFT, PAUSE and RESTART are each assigned on characters '8', '6', '2', '4', 'x', 'z' in the both case of SerialPort and WiFi UDP.
*/

/******************************************************************************/
/*   MAIN GAME VARIABLES                                                      */
/******************************************************************************/

#include "Arduino.h"

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "bsp_i2c.h"
#include "ft5x06.h"
#include "bsp_lcd.h"
#include "ws2812.h"

#include "Game_Audio.h"
#include "SoundData.h"
#include "TFT_16bits.h"

#if !CONFIG_IDF_TARGET_ESP32S2
#error This Sketch is inteded to run on ESP32-S2 HMI Devkit - Select "ESP32S2 Dev Module" board.
#endif

#if !CONFIG_SPIRAM_SUPPORT && !CONFIG_SPIRAM
#error This sketch MUST use PSRAM, please enable it in the IDE Menu
#endif

#define SCR_WIDTH     800
#define SCR_HEIGHT    480

#define LED_INDEX   (0)
void drawIndexedmap(uint8_t* indexmap, int16_t x, uint16_t y);
void ClearKeys();
void drawButtonFace(uint8_t btId);

// creates a GFX object for drawing and allocating PSRAM for Screen Buffer
TFT_16bits tft16bits(SCR_WIDTH, SCR_HEIGHT);

Game_Audio_Class GameAudio(GPIO_DAC_OUT, 0); // GPIO25/26 ESP32 || GPIO17/18 ESP32-S2 -- TIMER number 1
Game_Audio_Wav_Class pmDeath(pacmanDeath); // pacman dyingsound
Game_Audio_Wav_Class pmWav(pacman); // pacman theme
Game_Audio_Wav_Class pmChomp(chomp); // pacman chomp
Game_Audio_Wav_Class pmEatGhost(pacman_eatghost); // pacman theme

#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF

#define C16(_rr,_gg,_bb) ((ushort)(((_rr & 0xF8) << 8) | ((_gg & 0xFC) << 3) | ((_bb & 0xF8) >> 3)))
uint16_t _paletteW[] =
{
  C16(0, 0, 0),
  C16(255, 0, 0),      // 1 red
  C16(222, 151, 81),   // 2 brown
  C16(255, 0, 255),    // 3 pink

  C16(0, 0, 0),
  C16(0, 255, 255),    // 5 cyan
  C16(71, 84, 255),    // 6 mid blue
  C16(255, 184, 81),   // 7 lt brown

  C16(0, 0, 0),
  C16(255, 255, 0),    // 9 yellow
  C16(0, 0, 0),
  C16(33, 33, 255),    // 11 blue

  C16(0, 255, 0),      // 12 green
  C16(71, 84, 174),    // 13 aqua
  C16(255, 184, 174),  // 14 lt pink
  C16(222, 222, 255),  // 15 whiteish
};

uint8_t colors[] = {
  0,   0,   0,
  255, 0,   0,
  222, 151, 81,
  255, 0,   255,
  0,   0,   0,
  0,   255, 255,
  71,  84,  255,
  255, 184, 81,
  0,   0,   0,
  255, 255, 0,
  0,   0,   0,
  33,  33,  255,
  0,   255, 0,
  71,  84,  174,
  255, 184, 174,
  222, 222, 255,
};

byte SPEED = 2; // 1=SLOW 2=NORMAL 4=FAST //do not try  other values!!!


#define BONUS_INACTIVE_TIME 600
#define BONUS_ACTIVE_TIME 300

#define START_LIFES 2
#define START_LEVEL 1

byte MAXLIFES = 5;
byte LIFES = START_LIFES;
byte GAMEWIN = 0;
byte GAMEOVER = 0;
byte DEMO = 1;
byte LEVEL = START_LEVEL;
byte ACTUALBONUS = 0;  //actual bonus icon
byte ACTIVEBONUS = 0;  //status of bonus
byte GAMEPAUSED = 0;

byte PACMANFALLBACK = 0;

/******************************************************************************/
/*   Controll KEYPAD LOOP                                                     */
/******************************************************************************/

boolean but_A = false;        // START | PAUSE
boolean but_B = false;        // NOT USED HERE... would be for reseting the game
boolean but_UP = false;       // Indicates to go UP
boolean but_DOWN = false;     // Indicates to go DOWN
boolean but_LEFT = false;     // Indicates to go LEFT
boolean but_RIGHT = false;    // Indicates to go RIGHT

/******************************************************************************/
/*   GAME VARIABLES AND DEFINITIONS                                           */
/******************************************************************************/

#include "pacmanTiles.h"

enum GameState {
  ReadyState,
  PlayState,
  DeadGhostState, // Player got a ghost, show score sprite and only move eyes
  DeadPlayerState,
  EndLevelState
};

enum SpriteState
{
  PenState,
  RunState,
  FrightenedState,
  DeadNumberState,
  DeadEyesState,
  AteDotState,    // pacman
  DeadPacmanState
};

enum {
  MStopped = 0,
  MRight = 1,
  MDown = 2,
  MLeft = 3,
  MUp = 4,
};

#define ushort uint16_t

#define BINKY 0
#define PINKY 1
#define INKY  2
#define CLYDE 3
#define PACMAN 4
#define BONUS 5

const byte _initSprites[] =
{
  BINKY,  14,     17 - 3,  31, MLeft,
  PINKY,  14 - 2, 17,      79, MLeft,
  INKY,   14,     17,     137, MLeft,
  CLYDE,  14 + 2, 17,     203, MRight,
  PACMAN, 14,     17 + 9,   0, MLeft,
  BONUS,  14,     17 + 3,   0, MLeft,
};

//  Ghost colors
const byte _palette2[] =
{
  0, 11, 1, 15,  // BINKY red
  0, 11, 3, 15,  // PINKY pink
  0, 11, 5, 15,  // INKY cyan
  0, 11, 7, 15,  // CLYDE brown
  0, 11, 9, 9,   // PACMAN yellow
  0, 11, 15, 15, // FRIGHTENED
  0, 11, 0, 15,  // DEADEYES

  0, 1, 15, 2,   // cherry
  0, 1, 15, 12,  // strawberry
  0, 7, 2, 12,   // peach
  0, 9, 15, 0,   // bell

  0, 15, 1, 2,   // apple
  0, 12, 15, 5,  // grape
  0, 11, 9, 1,   // galaxian
  0, 5, 15, 15,  // key

};

const byte _paletteIcon2[] =
{
  0, 9, 9, 9, // PACMAN

  0, 2,  15, 1,  // cherry
  0, 12, 15, 1,  // strawberry
  0, 12, 2,  7,  // peach
  0, 0,  15, 9,  // bell

  0, 2,  15, 1,  // apple
  0, 12, 15, 5,  // grape
  0, 1,  9,  11, // galaxian
  0, 5,  15, 15, // key
};

#define PACMANICON 1
#define BONUSICON 2

#define FRIGHTENEDPALETTE 5
#define DEADEYESPALETTE 6

#define BONUSPALETTE 7

#define FPS 60
#define CHASE 0
#define SCATTER 1

#define DOT 7
#define PILL 14
#define PENGATE 0x1B

const byte _opposite[] = { MStopped, MLeft, MUp, MRight, MDown };
#define OppositeDirection(_x) *(_opposite + _x)

const byte _scatterChase[] = { 7, 20, 7, 20, 5, 20, 5, 0 };
const byte _scatterTargets[] = { 2, 0, 25, 0, 0, 35, 27, 35 }; // inky/clyde scatter targets are backwards
const char _pinkyTargetOffset[] = { 4, 0, 0, 4, (byte) - 4, 0, (byte) - 4, 4 }; // Includes pinky target bug

#define FRIGHTENEDGHOSTSPRITE 0
#define GHOSTSPRITE 2
#define NUMBERSPRITE 10
#define PACMANSPRITE 14

const byte _pacLeftAnim[] = { 5, 6, 5, 4 };
const byte _pacRightAnim[] = { 2, 0, 2, 4 };
const byte _pacVAnim[] = { 4, 3, 1, 3 };

word _BonusInactiveTimmer = BONUS_INACTIVE_TIME;
word _BonusActiveTimmer = 0;

/******************************************************************************/
/*   GAME - Sprite Class                                                      */
/******************************************************************************/

class Sprite
{
  public:
    int16_t _x, _y;
    int16_t lastx, lasty;
    byte cx, cy;        // cell x and y
    byte tx, ty;        // target x and y

    SpriteState state;
    byte  pentimer;     // could be the same

    byte who;
    byte _speed;
    byte dir;
    byte phase;

    // Sprite bits
    byte palette2;  // 4->16 color map index
    byte bits;      // index of sprite bits
    signed char sy;

    void Init(const byte* s)
    {
      ClearKeys();
      who = *s++;
      cx =  *s++;
      cy =  *s++;
      pentimer = *s++;
      dir = *s;
      _x = lastx = (int16_t)cx * 8 - 4;
      _y = lasty = (int16_t)cy * 8;
      state = PenState;
      _speed = 0;
      Target(rand() % 20, rand() % 20);
    }


    void Target(byte x, byte y)
    {
      tx = x;
      ty = y;
    }

    int16_t Distance(byte x, byte y)
    {
      int16_t dx = cx - x;
      int16_t dy = cy - y;
      return dx * dx + dy * dy; // Distance to target
    }

    //  once per sprite, not 9 times
    void SetupDraw(GameState gameState, byte deadGhostIndex)
    {
      sy = 1;
      palette2 = who;
      byte p = phase >> 3;

      if (who == BONUS) {
        //BONUS ICONS
        bits = 21 + ACTUALBONUS;
        palette2 = BONUSPALETTE + ACTUALBONUS;
        return;
      }

      if (who != PACMAN)
      {
        bits = GHOSTSPRITE + ((dir - 1) << 1) + (p & 1); // Ghosts
        switch (state)
        {
          case FrightenedState:
            bits = FRIGHTENEDGHOSTSPRITE + (p & 1); // frightened
            palette2 = FRIGHTENEDPALETTE;
            break;
          case DeadNumberState:
            palette2 = FRIGHTENEDPALETTE;
            bits = NUMBERSPRITE + deadGhostIndex;
            break;
          case DeadEyesState:
            palette2 = DEADEYESPALETTE;
            break;
          default:
            ;
        }
        return;
      }

      //  PACMAN animation
      byte f = (phase >> 1) & 3;
      if (dir == MLeft)
        f = _pacLeftAnim[f];
      else if (dir == MRight)
        f = _pacRightAnim[f];
      else
        f = _pacVAnim[f];
      if (dir == MUp)
        sy = -1;
      bits = f + PACMANSPRITE;
    }

    //  Draw this sprite into the tile at x,y
    void Draw8(int16_t x, int16_t y, byte* tile)
    {
      int16_t px = x - (_x - 4);
      if (px <= -8 || px >= 16) return;
      int16_t py = y - (_y - 4);
      if (py <= -8 || py >= 16) return;

      // Clip y
      int16_t lines = py + 8;
      if (lines > 16)
        lines = 16;
      if (py < 0)
      {
        tile -= py * 8;
        py = 0;
      }
      lines -= py;

      //  Clip in X
      byte right = 16 - px;
      if (right > 8)
        right = 8;
      byte left = 0;
      if (px < 0)
      {
        left = -px;
        px = 0;
      }

      //  Get bitmap
      signed char dy = sy;
      if (dy < 0)
        py = 15 - py;  // VFlip
      byte* data = (byte*)(pacman16x16 + bits * 64);
      data += py << 2;
      dy <<= 2;
      data += px >> 2;
      px &= 3;

      const byte* palette = _palette2 + (palette2 << 2);
      while (lines)
      {
        const byte *src = data;
        byte d = *src++;
        d >>= px << 1;
        byte sx = 4 - px;
        byte x = left;
        do
        {
          byte p = d & 3;
          if (p)
          {
            p = palette[p];
            if (p)
              tile[x] = p;
          }
          d >>= 2;    // Next pixel
          if (!--sx)
          {
            d = *src++;
            sx = 4;
          }
        } while (++x < right);

        tile += 8;
        data += dy;
        lines--;
      }
    }
};

/******************************************************************************/
/*   GAME - Playfield Class                                                   */
/******************************************************************************/

class Playfield
{

    Sprite _sprites[5];

    Sprite _BonusSprite; //Bonus

    byte _dotMap[(32 / 4) * (36 - 6)];

    GameState _state;
    long    _score;             // 7 digits of score
    long    _hiscore;             // 7 digits of score
    long    _lifescore;
    signed char    _scoreStr[8];
    signed char    _hiscoreStr[8];
    byte    _icons[14];         // Along bottom of screen

    ushort  _stateTimer;
    ushort  _frightenedTimer;
    byte    _frightenedCount;
    byte    _scIndex;           //
    ushort  _scTimer;           // next change of sc status

    bool _inited;
    byte* _dirty;
  public:
    Playfield() : _inited(false)
    {
      //  Swizzle palette TODO just fix in place
      //      byte * p = (byte*)_paletteW;
      //      for (int16_t i = 0; i < 16; i++)
      //      {
      //        ushort w = _paletteW[i];    // Swizzle
      //        *p++ = w >> 8;
      //        *p++ = w;
      //      }
    }

    // Draw 2 bit BG into 8 bit icon tiles at bottom
    void DrawBG2(byte cx, byte cy, byte* tile)
    {
      byte index = 0;
      signed char b = 0;

      index = _icons[cx >> 1];   // 13 icons across bottom
      if (index == 0)
      {
        memset(tile, 0, 64);
        return;
      }
      index--;
      index <<= 2;                        // 4 tiles per icon

      b = (1 - (cx & 1)) + ((cy & 1) << 1); // Index of tile

      const byte* bg = pacman8x8x2 + ((b + index) << 4);
      const byte* palette = _paletteIcon2 + index;


      byte x = 16;
      while (x--)
      {
        byte bits = (signed char) * bg++;
        byte i = 4;
        while (i--)
        {
          tile[i] = palette[bits & 3];
          bits >>= 2;
        }
        tile += 4;
      }
    }

    byte GetTile(int16_t cx, int16_t ty)
    {

      if (_state != ReadyState && ty == 20 && cx > 10 && cx < 17) return (0); //READY TEXT ZONE

      if (LEVEL % 5 == 1) return playMap1[ty * 28 + cx];
      if (LEVEL % 5 == 2) return playMap2[ty * 28 + cx];
      if (LEVEL % 5 == 3) return playMap3[ty * 28 + cx];
      if (LEVEL % 5 == 4) return playMap4[ty * 28 + cx];
      if (LEVEL % 5 == 0) return playMap5[ty * 28 + cx];
      return 0;
    }

    // Draw 1 bit BG into 8 bit tile
    void DrawBG(byte cx, byte cy, byte* tile)
    {
      if (cy >= 34) //DRAW ICONS BELLOW MAZE
      {
        DrawBG2(cx, cy, tile);
        return;
      }

      byte c = 11;
      if (LEVEL % 8 == 1) c = 11; // Blue
      if (LEVEL % 8 == 2) c = 12; // Green
      if (LEVEL % 8 == 3) c = 1; // Red
      if (LEVEL % 8 == 4) c = 9; // Yellow
      if (LEVEL % 8 == 5) c = 2; // Brown
      if (LEVEL % 8 == 6) c = 5; // Cyan
      if (LEVEL % 8 == 7) c = 3; // Pink
      if (LEVEL % 8 == 0) c = 15; // White

      byte b = GetTile(cx, cy);
      const byte* bg;

      //  This is a little messy
      memset(tile, 0, 64);
      if (cy == 20 && cx >= 11 && cx < 17)
      {
        if (DEMO == 1 && ACTIVEBONUS == 1) return;

        if ((_state != ReadyState && GAMEPAUSED != 1 && DEMO != 1) || ACTIVEBONUS == 1) b = 0; // hide 'READY!'
        else if (DEMO == 1 && cx == 11) b = 0;
        else if (DEMO == 1 && cx == 12) b = 'D';
        else if (DEMO == 1 && cx == 13) b = 'E';
        else if (DEMO == 1 && cx == 14) b = 'M';
        else if (DEMO == 1 && cx == 15) b = 'O';
        else if (DEMO == 1 && cx == 16) b = 0;
        else if (GAMEPAUSED == 1 && cx == 11) b = 'P';
        else if (GAMEPAUSED == 1 && cx == 12) b = 'A';
        else if (GAMEPAUSED == 1 && cx == 13) b = 'U';
        else if (GAMEPAUSED == 1 && cx == 14) b = 'S';
        else if (GAMEPAUSED == 1 && cx == 15) b = 'E';
        else if (GAMEPAUSED == 1 && cx == 16) b = 'D';
      }
      else if (cy == 1)
      {
        if (cx < 7)
          b = _scoreStr[cx];
        else if (cx >= 10 && cx < 17)
          b = _hiscoreStr[cx - 10]; // HiScore
      } else {
        if (b == DOT || b == PILL)  // DOT==7 or PILL==16
        {
          if (!GetDot(cx, cy))
            return;
          c = 14;
        }
        if (b == PENGATE)
          c = 14;
      }

      bg = playTiles + (b << 3);
      if (b >= '0')
        c = 15; // text is white

      for (byte y = 0; y < 8; y++)
      {
        signed char bits = (signed char) * bg++; ///WARNING CHAR MUST BE signed !!!
        byte x = 0;
        while (bits)
        {
          if (bits < 0)
            tile[x] = c;
          bits <<= 1;
          x++;
        }
        tile += 8;
      }
    }

    // Draw BG then all sprites in this cell
    void Draw(uint16_t x, uint16_t y, bool sprites)
    {

      byte tile[8 * 8];

      //      Fill with BG
      if (y == 20 && x >= 11 && x < 17 && DEMO == 1 && ACTIVEBONUS == 1)  return;
      DrawBG(x, y, tile);

      //      Overlay sprites
      x <<= 3;
      y <<= 3;
      if (sprites)
      {
        for (byte i = 0; i < 5; i++)
          _sprites[i].Draw8(x, y, tile);

        //AND BONUS
        if (ACTIVEBONUS) _BonusSprite.Draw8(x, y, tile);

      }

      //      Show sprite block
#if 0
      for (byte i = 0; i < 5; i++)
      {
        Sprite* s = _sprites + i;
        if (s->cx == (x >> 3) && s->cy == (y >> 3))
        {
          memset(tile, 0, 8);
          for (byte j = 1; j < 7; j++)
            tile[j * 8] = tile[j * 8 + 7] = 0;
          memset(tile + 56, 0, 8);
        }
      }
#endif

      x += (240 - 224) / 2;
      y += (320 - 288) / 2;


      //      Should be a direct Graphics call

      //byte n = tile[0];
      //byte i = 0;
      //word color = (word)_paletteW[n];

      drawIndexedmap(tile, x, y);
    }

    boolean updateMap [36][28];

    //  Mark tile as dirty (should not need range checking here)
    void Mark(int16_t x, int16_t y, byte* m)
    {
      x -= 4;
      y -= 4;

      updateMap[(y >> 3)][(x >> 3)] = true;
      updateMap[(y >> 3)][(x >> 3) + 1] = true;
      updateMap[(y >> 3)][(x >> 3) + 2] = true;
      updateMap[(y >> 3) + 1][(x >> 3)] = true;
      updateMap[(y >> 3) + 1][(x >> 3) + 1] = true;
      updateMap[(y >> 3) + 1][(x >> 3) + 2] = true;
      updateMap[(y >> 3) + 2][(x >> 3)] = true;
      updateMap[(y >> 3) + 2][(x >> 3) + 1] = true;
      updateMap[(y >> 3) + 2][(x >> 3) + 2] = true;

    }

    void DrawAllBG()
    {
      for (byte y = 0; y < 36; y++)
        for (byte x = 0; x < 28; x++) {
          Draw(x, y, false);
        }
    }

    //  Draw sprites overlayed on cells
    void DrawAll()
    {
      byte* m = _dirty;

      //  Mark sprite old/new positions as dirty
      for (byte i = 0; i < 5; i++)
      {
        Sprite* s = _sprites + i;
        Mark(s->lastx, s->lasty, m);
        Mark(s->_x, s->_y, m);

      }

      // Mark BONUS sprite old/new positions as dirty
      Sprite* _s = &_BonusSprite;
      Mark(_s->lastx, _s->lasty, m);
      Mark(_s->_x, _s->_y, m);


      //  Animation
      for (byte i = 0; i < 5; i++)
        _sprites[i].SetupDraw(_state, _frightenedCount - 1);

      _BonusSprite.SetupDraw(_state, _frightenedCount - 1);


      for (byte tmpY = 0; tmpY < 36; tmpY++) {
        for (byte tmpX = 0; tmpX < 28; tmpX++) {
          if (updateMap[tmpY][tmpX] == true) Draw(tmpX, tmpY, true);
          updateMap[tmpY][tmpX] = false;
        }

      }
    }


    int16_t Chase(Sprite* s, int16_t cx, int16_t cy)
    {
      while (cx < 0)      //  Tunneling
        cx += 28;
      while (cx >= 28)
        cx -= 28;

      byte t = GetTile(cx, cy);
      if (!(t == 0 || t == DOT || t == PILL || t == PENGATE))
        return 0x7FFF;

      if (t == PENGATE)
      {
        if (s->who == PACMAN)
          return 0x7FFF;  // Pacman can't cross this to enter pen
        if (!(InPen(s->cx, s->cy) || s->state == DeadEyesState))
          return 0x7FFF;  // Can cross if dead or in pen trying to get out
      }

      int16_t dx = s->tx - cx;
      int16_t dy = s->ty - cy;
      return (dx * dx + dy * dy); // Distance to target

    }

    void UpdateTimers()
    {
      // Update scatter/chase selector, low bit of index indicates scatter
      if (_scIndex < 8)
      {
        if (_scTimer-- == 0)
        {
          byte duration = _scatterChase[_scIndex++];
          _scTimer = duration * FPS;
        }
      }

      // BONUS timmer
      if (ACTIVEBONUS == 0 && _BonusInactiveTimmer-- == 0) {
        _BonusActiveTimmer = BONUS_ACTIVE_TIME; //5*FPS;
        ACTIVEBONUS = 1;
      }
      if (ACTIVEBONUS == 1 && _BonusActiveTimmer-- == 0) {
        _BonusInactiveTimmer = BONUS_INACTIVE_TIME; //10*FPS;
        ACTIVEBONUS = 0;
      }


      //  Release frightened ghosts
      if (_frightenedTimer && !--_frightenedTimer)
      {
        for (byte i = 0; i < 4; i++)
        {
          Sprite* s = _sprites + i;
          if (s->state == FrightenedState)
          {
            s->state = RunState;
            s->dir = OppositeDirection(s->dir);
          }
        }
      }
    }

    //  Target closes pill, run from ghosts?
    void PacmanAI()
    {
      Sprite* pacman;
      pacman = _sprites + PACMAN;

      //  Chase frightened ghosts
      //Sprite* closestGhost = NULL;
      Sprite* frightenedGhost = NULL;
      Sprite* closestAttackingGhost = NULL;
      Sprite* DeadEyesStateGhost = NULL;
      int16_t dist = 0x7FFF;
      int16_t closestfrightenedDist = 0x7FFF;
      int16_t closestAttackingDist = 0x7FFF;
      for (byte i = 0; i < 4; i++)
      {
        Sprite* s = _sprites + i;
        int16_t d = s->Distance(pacman->cx, pacman->cy);
        if (d < dist)
        {

          dist = d;
          if (s->state == FrightenedState ) {
            frightenedGhost = s;
            closestfrightenedDist = d;
          }
          else {
            closestAttackingGhost = s;
            closestAttackingDist = d;
          }
          //closestGhost = s;

          if ( s->state == DeadEyesState ) DeadEyesStateGhost = s;

        }
      }

      PACMANFALLBACK = 0;

      if (DEMO == 1 && !DeadEyesStateGhost && frightenedGhost )
      {
        pacman->Target(frightenedGhost->cx, frightenedGhost->cy);
        return;
      }



      // Under threat; just avoid closest ghost
      if (DEMO == 1 && !DeadEyesStateGhost && dist <= 32  && closestAttackingDist < closestfrightenedDist )
      {
        if (dist <= 16) {
          pacman->Target( pacman->cx * 2 - closestAttackingGhost->cx, pacman->cy * 2 - closestAttackingGhost->cy);
          PACMANFALLBACK = 1;
        } else {
          pacman->Target( pacman->cx * 2 - closestAttackingGhost->cx, pacman->cy * 2 - closestAttackingGhost->cy);
        }
        return;
      }

      if (ACTIVEBONUS == 1) {
        pacman->Target(13, 20);
        return;
      }


      //  Go for the pill
      if (GetDot(1, 6))
        pacman->Target(1, 6);
      else if (GetDot(26, 6))
        pacman->Target(26, 6);
      else if (GetDot(1, 26))
        pacman->Target(1, 26);
      else if (GetDot(26, 26))
        pacman->Target(26, 26);
      else
      {
        // closest dot
        int16_t dist = 0x7FFF;
        for (byte y = 4; y < 32; y++)
        {
          for (byte x = 1; x < 26; x++)
          {
            if (GetDot(x, y))
            {
              int16_t d = pacman->Distance(x, y);
              if (d < dist)
              {
                dist = d;
                pacman->Target(x, y);
              }
            }
          }
        }

        if (dist == 0x7FFF) {
          GAMEWIN = 1; // No dots, GAME WIN!
        }

      }
    }

    void Scatter(Sprite* s)
    {
      const byte* st = _scatterTargets + (s->who << 1);
      s->Target(*st, *(st + 1));
    }

    void UpdateTargets()
    {
      if (_state == ReadyState)
        return;
      PacmanAI();
      Sprite* pacman = _sprites + PACMAN;

      //  Ghost AI
      bool scatter = _scIndex & 1;
      for (byte i = 0; i < 4; i++)
      {
        Sprite* s = _sprites + i;

        //  Deal with returning ghost to pen
        if (s->state == DeadEyesState)
        {
          if (s->cx == 14 && s->cy == 17) // returned to pen
          {
            s->state = PenState;        // Revived in pen
            s->pentimer = 80;
          }
          else
            s->Target(14, 17);          // target pen
          continue;           //
        }

        //  Release ghost from pen when timer expires
        if (s->pentimer)
        {
          if (--s->pentimer)  // stay in pen for awhile
            continue;
          s->state = RunState;
        }

        if (InPen(s->cx, s->cy))
        {
          s->Target(14, 14 - 2); // Get out of pen first
        } else {
          if (scatter || s->state == FrightenedState)
            Scatter(s);
          else
          {
            // Chase mode targeting
            signed char tx = pacman->cx;
            signed char ty = pacman->cy;
            switch (s->who)
            {
              case PINKY:
                {
                  const char* pto = _pinkyTargetOffset + ((pacman->dir - 1) << 1);
                  tx += *pto;
                  ty += *(pto + 1);
                }
                break;
              case INKY:
                {
                  const char* pto = _pinkyTargetOffset + ((pacman->dir - 1) << 1);
                  Sprite* binky = _sprites + BINKY;
                  tx += *pto >> 1;
                  ty += *(pto + 1) >> 1;
                  tx += tx - binky->cx;
                  ty += ty - binky->cy;
                }
                break;
              case CLYDE:
                {
                  if (s->Distance(pacman->cx, pacman->cy) < 64)
                  {
                    const byte* st = _scatterTargets + CLYDE * 2;
                    tx = *st;
                    ty = *(st + 1);
                  }
                }
                break;
            }
            s->Target(tx, ty);
          }
        }
      }
    }

    //  Default to current direction
    byte ChooseDir(int16_t dir, Sprite* s)
    {
      int16_t choice[4];
      choice[0] = Chase(s, s->cx, s->cy - 1); // Up
      choice[1] = Chase(s, s->cx - 1, s->cy); // Left
      choice[2] = Chase(s, s->cx, s->cy + 1); // Down
      choice[3] = Chase(s, s->cx + 1, s->cy); // Right


      if (DEMO == 0 && s->who == PACMAN && choice[0] < 0x7FFF && but_UP) dir = MUp;
      else if (DEMO == 0 && s->who == PACMAN && choice[1] < 0x7FFF && but_LEFT) dir = MLeft;
      else if (DEMO == 0 && s->who == PACMAN && choice[2] < 0x7FFF && but_DOWN) dir = MDown;
      else if (DEMO == 0 && s->who == PACMAN && choice[3] < 0x7FFF && but_RIGHT) dir = MRight;

      else if (DEMO == 0 && choice[0] < 0x7FFF && s->who == PACMAN && dir == MUp) dir = MUp;
      else if (DEMO == 0 && choice[1] < 0x7FFF && s->who == PACMAN && dir == MLeft) dir = MLeft;
      else if (DEMO == 0 && choice[2] < 0x7FFF && s->who == PACMAN && dir == MDown) dir = MDown;
      else if (DEMO == 0 && choice[3] < 0x7FFF && s->who == PACMAN && dir == MRight) dir = MRight;
      else if ((DEMO == 0 && s->who != PACMAN) || DEMO == 1 ) {

        // Don't choose opposite of current direction?

        int16_t dist = choice[4 - dir]; // favor current direction
        byte opposite = OppositeDirection(dir);
        for (byte i = 0; i < 4; i++)
        {
          byte d = 4 - i;
          if ((d != opposite && choice[i] < dist) || (s->who == PACMAN && PACMANFALLBACK &&  choice[i] < dist))
          {
            if (s->who == PACMAN && PACMANFALLBACK) PACMANFALLBACK = 0;
            dist = choice[i];
            dir = d;
          }

        }

      } else  {
        dir = MStopped;
      }

      return dir;
    }

    bool InPen(byte cx, byte cy)
    {
      if (cx <= 10 || cx >= 18) return false;
      if (cy <= 14 || cy >= 18) return false;
      return true;
    }

    byte GetSpeed(Sprite* s)
    {
      if (s->who == PACMAN)
        return _frightenedTimer ? 90 : 80;
      if (s->state == FrightenedState)
        return 40;
      if (s->state == DeadEyesState)
        return 100;
      if (s->cy == 17 && (s->cx <= 5 || s->cx > 20))
        return 40;  // tunnel
      return 75;
    }

    void PackmanDied() {  // Noooo... PACMAN DIED :(

      if (DEMO == 0) {
        GameAudio.PlayWav(&pmDeath, true, 1.0);
        // wait until done
        while (GameAudio.IsPlaying());
      }

      if (LIFES <= 0) {
        GAMEOVER = 1;
        LEVEL = START_LEVEL;
        LIFES = START_LIFES;
        DEMO = 1;
        Init();
      } else {
        LIFES--;

        _inited = true;
        _state = ReadyState;
        _stateTimer = FPS / 2;
        _frightenedCount = 0;
        _frightenedTimer = 0;

        const byte* s = _initSprites;
        for (int16_t i = 0; i < 5; i++)
          _sprites[i].Init(s + i * 5);

        _scIndex = 0;
        _scTimer = 1;

        memset(_icons, 0, sizeof(_icons));

        //AND BONUS
        _BonusSprite.Init(s + 5 * 5);
        _BonusInactiveTimmer = BONUS_INACTIVE_TIME;
        _BonusActiveTimmer = 0;

        for (byte i = 0; i < ACTUALBONUS; i++) {
          _icons[13 - i] = BONUSICON + i;
        }

        for (byte i = 0; i < LIFES; i++) {
          _icons[0 + i] = PACMANICON;
        }

        //Draw LIFE and BONUS Icons
        for (byte y = 34; y < 36; y++)
          for (byte x = 0; x < 28; x++) {
            Draw(x, y, false);
          }

        DrawAllBG();
      }
    }


    void MoveAll()
    {
      UpdateTimers();
      UpdateTargets();


      //  Update game state
      if (_stateTimer)
      {
        if (--_stateTimer <= 0)
        {
          switch (_state)
          {
            case ReadyState:
              _state = PlayState;
              _dirty[20 * 4 + 1] |= 0x1F; // Clear 'READY!'
              _dirty[20 * 4 + 2] |= 0x80;

              for (byte tmpX = 11; tmpX < 17; tmpX++) Draw(tmpX, 20, false); // ReDraw (clear) 'READY' position

              break;
            case DeadGhostState:
              _state = PlayState;
              for (byte i = 0; i < 4; i++)
              {
                Sprite* s = _sprites + i;
                if (s->state == DeadNumberState)
                  s->state = DeadEyesState;
              }
              break;
            default:
              ;
          }
        } else {
          if (_state == ReadyState)
            return;
        }
      }

      for (byte i = 0; i < 5; i++)
      {
        Sprite* s = _sprites + i;

        //  In DeadGhostState, only eyes move
        if (_state == DeadGhostState && s->state != DeadEyesState)
          continue;

        //  Calculate speed
        s->_speed += GetSpeed(s);
        if (s->_speed < 100)
          continue;
        s->_speed -= 100;

        s->lastx = s->_x;
        s->lasty = s->_y;
        s->phase++;

        int16_t x = s->_x;
        int16_t y = s->_y;


        if ((x & 0x7) == 0 && (y & 0x7) == 0)   // cell aligned
          s->dir = ChooseDir(s->dir, s);      // time to choose another direction


        switch (s->dir)
        {
          case MLeft:     x -= SPEED; break;
          case MRight:    x += SPEED; break;
          case MUp:       y -= SPEED; break;
          case MDown:     y += SPEED; break;
          case MStopped:  break;
        }

        //  Wrap x because of tunnels
        while (x < 0)
          x += 224;
        while (x >= 224)
          x -= 224;

        s->_x = x;
        s->_y = y;
        s->cx = (x + 4) >> 3;
        s->cy = (y + 4) >> 3;

        if (s->who == PACMAN)
          EatDot(s->cx, s->cy);
      }

      //  Collide
      Sprite* pacman = _sprites + PACMAN;

      //  Collide with BONUS
      Sprite* _s = &_BonusSprite;
      if (ACTIVEBONUS == 1 && _s->cx == pacman->cx && _s->cy == pacman->cy)
      {
        Score(ACTUALBONUS * 50);
        ACTUALBONUS++;
        if (ACTUALBONUS > 7) {
          ACTUALBONUS = 0;
          if (LIFES < MAXLIFES) LIFES++;

          //reset all icons
          memset(_icons, 0, sizeof(_icons));

          for (byte i = 0; i < LIFES; i++) {
            _icons[0 + i] = PACMANICON;
          }

        }

        for (byte i = 0; i < ACTUALBONUS; i++) {
          _icons[13 - i] = BONUSICON + i;
        }

        //REDRAW LIFE and BONUS icons
        for (byte y = 34; y < 36; y++)
          for (byte x = 0; x < 28; x++) {
            Draw(x, y, false);
          }

        ACTIVEBONUS = 0;
        _BonusInactiveTimmer = BONUS_INACTIVE_TIME;
      }

      for (byte i = 0; i < 4; i++)
      {
        Sprite* s = _sprites + i;
        //if (s->cx == pacman->cx && s->cy == pacman->cy)
        if (s->_x + SPEED >= pacman->_x && s->_x - SPEED <= pacman->_x && s->_y + SPEED >= pacman->_y && s->_y - SPEED <= pacman->_y)



        {
          if (s->state == FrightenedState)
          {
            if (DEMO == 0) {
              GameAudio.PlayWav(&pmEatGhost, true, 1.0);
            }

            s->state = DeadNumberState;     // Killed a ghost
            _frightenedCount++;
            _state = DeadGhostState;
            _stateTimer = 10;
            Score((1 << _frightenedCount) * 100);
          }
          else {               // pacman died
            if (s->state == DeadNumberState || s->state == FrightenedState || s->state == DeadEyesState) {
            } else {
              PackmanDied();
            }
          }
        }
      }
    }

    //  Mark a position dirty
    void Mark(int16_t pos)
    {
      for (byte tmp = 0; tmp < 28; tmp++)
        updateMap[1][tmp] = true;

    }

    void SetScoreChar(byte i, signed char c)
    {
      if (_scoreStr[i] == c)
        return;
      _scoreStr[i] = c;
      Mark(i + 32);  //Score
      //Mark(i+32+10); //HiScore

    }

    void SetHiScoreChar(byte i, signed char c)
    {
      if (_hiscoreStr[i] == c)
        return;
      _hiscoreStr[i] = c;
      //Mark(i+32);    //Score
      Mark(i + 32 + 10); //HiScore
    }

    void Score(int16_t delta)
    {
      char str[8];
      _score += delta;
      if (DEMO == 0 && _score > _hiscore) _hiscore = _score;

      if (_score > _lifescore && _score % 10000 > 0) {
        _lifescore = (_score / 10000 + 1) * 10000;

        LIFES++; // EVERY 10000 points = 1UP

        for (byte i = 0; i < LIFES; i++) {
          _icons[0 + i] = PACMANICON;
        }

        //REDRAW LIFE and BONUS icons
        for (byte y = 34; y < 36; y++)
          for (byte x = 0; x < 28; x++) {
            Draw(x, y, false);
          }
        _score = _score + 100;
      }

      sprintf(str, "%ld", _score);
      byte i = 7 - strlen(str);
      byte j = 0;
      while (i < 7)
        SetScoreChar(i++, str[j++]);
      sprintf(str, "%ld", _hiscore);
      i = 7 - strlen(str);
      j = 0;
      while (i < 7)
        SetHiScoreChar(i++, str[j++]);
    }

    bool GetDot(byte cx, byte cy)
    {
      return _dotMap[(cy - 3) * 4 + (cx >> 3)] & (0x80 >> (cx & 7));
    }

    void EatDot(byte cx, byte cy)
    {
      if (!GetDot(cx, cy))
        return;
      byte mask = 0x80 >> (cx & 7);
      _dotMap[(cy - 3) * 4 + (cx >> 3)] &= ~mask;
      if (DEMO == 0) {
        GameAudio.PlayWav(&pmChomp, false, 1.0);
      }

      byte t = GetTile(cx, cy);
      if (t == PILL)
      {
        _frightenedTimer = 10 * FPS;
        _frightenedCount = 0;
        for (byte i = 0; i < 4; i++)
        {
          Sprite* s = _sprites + i;
          if (s->state == RunState)
          {
            s->state = FrightenedState;
            s->dir = OppositeDirection(s->dir);
          }
        }
        Score(50);
      }
      else
        Score(10);
    }

    void Init()
    {
      drawButtonFace(4);  // START / PAUSE

      if (GAMEWIN == 1) {
        GAMEWIN = 0;
      } else {
        LEVEL = START_LEVEL;
        LIFES = START_LIFES;
        ACTUALBONUS = 0; //actual bonus icon
        ACTIVEBONUS = 0; //status of bonus

        _score = 0;
        _lifescore = 10000;

        memset(_scoreStr, 0, sizeof(_scoreStr));
        _scoreStr[5] = _scoreStr[6] = '0';
      }


      _inited = true;
      _state = ReadyState;
      _stateTimer = FPS / 2;
      _frightenedCount = 0;
      _frightenedTimer = 0;

      const byte* s = _initSprites;
      for (int16_t i = 0; i < 5; i++)
        _sprites[i].Init(s + i * 5);

      //AND BONUS
      _BonusSprite.Init(s + 5 * 5);
      _BonusInactiveTimmer = BONUS_INACTIVE_TIME;
      _BonusActiveTimmer = 0;

      _scIndex = 0;
      _scTimer = 1;

      memset(_icons, 0, sizeof(_icons));

      // SET BONUS icons
      for (byte i = 0; i < ACTUALBONUS; i++) {
        _icons[13 - i] = BONUSICON + i;
      }

      // SET Lifes icons
      for (byte i = 0; i < LIFES; i++) {
        _icons[0 + i] = PACMANICON;
      }

      //Draw LIFE and BONUS Icons
      for (byte y = 34; y < 36; y++)
        for (byte x = 0; x < 28; x++) {
          Draw(x, y, false);
        }

      //  Init dots from rom
      memset(_dotMap, 0, sizeof(_dotMap));
      byte* map = _dotMap;
      for (byte y = 3; y < 36 - 3; y++) // 30 interior lines
      {
        for (byte x = 0; x < 28; x++)
        {
          byte t = GetTile(x, y);
          if (t == 7 || t == 14)
          {
            byte s = x & 7;
            map[x >> 3] |= (0x80 >> s);
          }
        }
        map += 4;
      }
      DrawAllBG();
    }

    void Step()
    {
      //int16_t keys = 0;

      if (GAMEWIN == 1) {
        LEVEL++;
        Init();
      }

      // Start GAME
      if (but_A && DEMO == 1 && GAMEPAUSED == 0) {
        but_A = false;
        DEMO = 0;
        GameAudio.PlayWav(&pmWav, false, 1.0);
        // wait until done
        //while(GameAudio.IsPlaying()){ }
        Init();
      } else if (but_A && DEMO == 0 && GAMEPAUSED == 0) { // Or PAUSE GAME
        but_A = false;
        GAMEPAUSED = 1;
        drawButtonFace(4);  // START / PAUSE
      }

      if (GAMEPAUSED && but_A && DEMO == 0) {
        but_A = false;
        GAMEPAUSED = 0;
        drawButtonFace(4);  // START / PAUSE
        for (byte tmpX = 11; tmpX < 17; tmpX++) Draw(tmpX, 20, false);
      }

      // Reset / Start GAME
      if (but_B) {
        but_B = false;
        DEMO = 0;
        Init();
      } else if (!_inited) {
        but_B = false;
        DEMO = 1;
        Init();
      }

      // Create a bitmap of dirty tiles
      byte m[(32 / 8) * 36]; // 144 bytes
      memset(m, 0, sizeof(m));
      _dirty = m;


      if (!GAMEPAUSED) MoveAll(); // IF GAME is PAUSED STOP ALL

      if ((ACTIVEBONUS == 0 && DEMO == 1) || GAMEPAUSED == 1 ) for (byte tmpX = 11; tmpX < 17; tmpX++) Draw(tmpX, 20, false); // Draw 'PAUSED' or 'DEMO' text

      DrawAll();
    }
};

/******************************************************************************/
/*   SETUP                                                                    */
/******************************************************************************/

#define BLACK 0x0000 // 16bit BLACK Color

/******************************************************************************/
/*   LOOP                                                                     */
/******************************************************************************/

void ClearKeys() {
  but_A = false;
  but_B = false;
  but_UP = false;
  but_DOWN = false;
  but_LEFT = false;
  but_RIGHT = false;
}

/*
  static IRAM_ATTR bool lvgl_read_cb(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
  {
    static uint8_t tp_num = 0;
    static uint16_t x = 0, y = 0;

    ft5x06_read_pos(&tp_num, &x, &y);

    if (0 == tp_num) {
        data->state = LV_INDEV_STATE_REL;
    } else {
        // Swap xy order and invert y direction
        data->point.x = y;
        data->point.y = LV_VER_RES_MAX - x - 1;
        data->state = LV_INDEV_STATE_PR;
    }

    return false;
  }
*/

/*
  void KeyPadLoop(){
  if (Serial.available()) {
    char r = Serial.read();
    if (r == 'z') { ClearKeys();  but_A=true; delay(300); } //else but_A=false;
    if (r == 'x') { ClearKeys();  but_B=true; delay(300); }  else but_B=false;
    if (r == '8') { ClearKeys();  but_UP=true; }  //else but_UP=false;
    if (r == '2') { ClearKeys();  but_DOWN=true; }  //else but_DOWN=false;
    if (r == '4') { ClearKeys();  but_LEFT=true; }  // else but_LEFT=false;
    if (r == '6') { ClearKeys();  but_RIGHT=true; }  //else but_RIGHT=false;
  }
  if (udp.parsePacket()) {
    char r = udp.read();
    if (r == 'z') { ClearKeys();  but_A=true; delay(300); } //else but_A=false;
    if (r == 'x') { ClearKeys();  but_B=true; delay(300); }  else but_B=false;
    if (r == '8') { ClearKeys();  but_UP=true; }  //else but_UP=false;
    if (r == '2') { ClearKeys();  but_DOWN=true; }  //else but_DOWN=false;
    if (r == '4') { ClearKeys();  but_LEFT=true; }  // else but_LEFT=false;
    if (r == '6') { ClearKeys();  but_RIGHT=true; }  //else but_RIGHT=false;
  }
  }
*/

uint16_t *screenBuffer;
Playfield _game;

void drawIndexedmap(uint8_t* indexmap, int16_t x, uint16_t y) {

  byte i = 0;
  for (byte tmpY = 0; tmpY < 8; tmpY++) {
    for (byte tmpX = 0; tmpX < 8; tmpX++) {
      // rotate the screen 90 counterclockwise considering image duplication
      int16_t xt = 2 * (y + tmpY);
      int16_t yt = (SCR_HEIGHT - 1) - 2 * (x + tmpX);
      // duplicate width and height of the game screen (240x320 ==> 480x640)
      screenBuffer[xt + yt * SCR_WIDTH] = (word)_paletteW[indexmap[i]];
      screenBuffer[xt + 1 + yt * SCR_WIDTH] = (word)_paletteW[indexmap[i]];
      screenBuffer[xt + (yt + 1) * SCR_WIDTH] = (word)_paletteW[indexmap[i]];
      screenBuffer[xt + 1 + (yt + 1) * SCR_WIDTH] = (word)_paletteW[indexmap[i]];
      i++;
    }
  }

  /*
    // Marks in the screen corners
    for (byte tmpY = 0; tmpY < 8; tmpY++) {
      for (byte tmpX = 0; tmpX < 8; tmpX++) {
        screenBuffer[0 + 0 * SCR_WIDTH + tmpX + tmpY * SCR_WIDTH] = (word)_paletteW[1]; // red
        screenBuffer[0 + (SCR_HEIGHT - 9) * SCR_WIDTH + tmpX + tmpY * SCR_WIDTH] = (word)_paletteW[2];  // brown
        screenBuffer[SCR_WIDTH - 9 + 0 * SCR_WIDTH + tmpX + tmpY * SCR_WIDTH] = (word)_paletteW[3];    // pink
        screenBuffer[SCR_WIDTH - 9 + (SCR_HEIGHT - 9) * SCR_WIDTH + tmpX + tmpY * SCR_WIDTH] = (word)_paletteW[5]; //cyan
      }
    }
  */
}

// Rotated dimensions based on a SCR_w = 480 and SCR_H = 800
//#define BUT_W       100
//#define BUT_H       55
#define BUT_NUM     5      // removed BUT B = reset
#define BUT_X       0
#define BUT_Y       1
#define BUT_COLOR   2
#define BUT_W       3
#define BUT_H       4

uint32_t debounceTimeStart = 0;

uint16_t buttons[BUT_NUM][5] = {
  // Using rotated coordintes the same way that the touch sensor does
  // X0 is 0 .. SCR_HEIGHT -1
  // Y0 is 0 .. SCR_WIDTH - 1
  //X0   Y0    color          W    H
  { 255, 610, _paletteW[15], 100, 55},  // UP
  { 370, 675, _paletteW[15], 100, 55},  // LEFT
  { 140, 675, _paletteW[15], 100, 55},  // RIGHT
  { 255, 740, _paletteW[15], 100, 55},  // DOWN
  { 0,   620, _paletteW[6],  60,  60},   // A
  /*
    { 610, 255, _paletteW[15], 100, 55},  // UP
    { 675, 370, _paletteW[15], 100, 55},  // LEFT
    { 675, 140, _paletteW[15], 100, 55},  // RIGHT
    { 740, 255, _paletteW[15], 100, 55},  // DOWN
    { 620,   0, _paletteW[6],  60,  60},   // A
  */
  //  { 10,  640, _paletteW[6]},   // A
  //  { 10,  740, _paletteW[1]},   // B
};

int8_t getTouchedButton(uint16_t x, uint16_t y) {
  // ft5x06 driver inverts X coordinate and also rotates it
  uint8_t delta = 15;
  x = SCR_HEIGHT - 1 - x;
  int8_t pressedButton = -1;
  for (uint8_t b = 0; b < BUT_NUM; b++) {
    if (b > BUT_NUM - 1) delta = 0; // Buttons A and B are not "expanded" in the touch area
    if (x >= buttons[b][BUT_X] - delta && x < buttons[b][BUT_X] + buttons[b][BUT_W] + delta
        && y >= buttons[b][BUT_Y] - delta && y < buttons[b][BUT_Y] + buttons[b][BUT_H] + delta) {
      pressedButton = b;
      break;
    }
  }
  return pressedButton;
}

void drawButtonFace(uint8_t btId) {
  // rotate the coordinates by sawpping X & Y
  uint16_t x0 = buttons[btId][BUT_Y];
  uint16_t y0 = buttons[btId][BUT_X];

  uint16_t _w = buttons[btId][BUT_H], _h = buttons[btId][BUT_W];
  uint8_t r = min(_w, _h) / 4; // Corner radius
  int16_t _x1 = x0, _y1 = y0;

  tft16bits.fillRoundRect(_x1, _y1, _w, _h, r, buttons[btId][BUT_COLOR]);

  switch (btId) {
    case 0:   // UP
      tft16bits.drawRoundRect(_x1, _y1, _w, _h, r, RED);
      tft16bits.fillTriangle(_x1 + 10, _y1 + _h / 2, _x1 + _w - 15, _y1 + _h / 2 + 20, _x1 + _w - 15, _y1 + _h / 2 - 20, BLACK);
      //tft16bits.fillRect(_x1 + 10, _y1 + _h / 2, 4, 4, RED);
      //tft16bits.fillRect(_x1 + _w - 15, _y1 + _h / 2 + 20, 4, 4, BLUE);
      //tft16bits.fillRect(_x1 + _w - 15, _y1 + _h / 2 - 20, 4, 4, BLACK);
      break;
    case 1:   // LEFT
      tft16bits.drawRoundRect(_x1, _y1, _w, _h, r, RED);
      tft16bits.fillTriangle(_x1 + _w - 15, _y1 + _h / 2 - 15, _x1 + 10, _y1 + _h / 2 - 15, _x1 + _w / 2, _y1 + _h / 2 + 20, BLACK);
      //tft16bits.fillRect(_x1 + _w - 15, _y1 + _h / 2 - 15, 4, 4, RED);
      //tft16bits.fillRect(_x1 + 10, _y1 + _h / 2 - 15, 4, 4, BLUE);
      //tft16bits.fillRect(_x1 + _w / 2, _y1 + _h / 2 + 20, 4, 4, BLACK);
      break;
    case 2:   // RIGHT
      tft16bits.drawRoundRect(_x1, _y1, _w, _h, r, RED);
      tft16bits.fillTriangle(_x1 + _w - 15, _y1 + _h / 2 + 15, _x1 + 10, _y1 + _h / 2 + 15, _x1 + _w / 2, _y1 + _h / 2 - 20, BLACK);
      break;
    case 3:   // DOWN
      tft16bits.drawRoundRect(_x1, _y1, _w, _h, r, RED);
      tft16bits.fillTriangle(_x1 + _w - 10, _y1 + _h / 2, _x1 + 15, _y1 + _h / 2 + 20, _x1 + 15, _y1 + _h / 2 - 20, BLACK);
      break;
    case 4:   // START/PAUSE
      tft16bits.drawRoundRect(_x1, _y1, _w, _h, r, CYAN);
      if (DEMO == 1 || GAMEPAUSED == 1) {
        // Button Action is PLAY
        tft16bits.fillTriangle(_x1 + _w - 10, _y1 + _h / 2 + 15, _x1 + 10, _y1 + _h / 2 + 15, _x1 + _w / 2, _y1 + _h / 2 - 20, RED);
      } else if (GAMEPAUSED == 0) {
        // Button Action is PAUSE
        tft16bits.fillRect(_x1 + 10, _y1 + _h / 2 + 4, 40, 15, RED);
        tft16bits.fillRect(_x1 + 10, _y1 + 10, 40, 15, RED);
      }
      break;
  }
}

void drawAllButtons() {
  for (uint8_t b = 0; b < BUT_NUM; b++) {
#if 1
    drawButtonFace(b);
#else
    // just rectagles as button - testing
    for (uint16_t y1 = 0; y1 < buttons[b][BUT_W]; y1++)
      for (uint16_t x1 = 0; x1 < buttons[b][BUT_H]; x1++) {
        screenBuffer[x0 + x1 + (y0 + y1) * SCR_WIDTH] = buttons[b][BUT_COLOR];
      }
#endif
  }
}

void setup() {
  // put your setup code here, to run once:
  /* Initialize I2C bus for touch IC */
  ESP_ERROR_CHECK(bsp_i2c_init(I2C_NUM_0, 400000));

  /* Initialize LCD */
  ESP_ERROR_CHECK(bsp_lcd_init());

  /* LCD touch IC init */
  ESP_ERROR_CHECK(ft5x06_init());

  //  ESP_ERROR_CHECK(ws2812_init());

  if (!tft16bits.createPSRAMFrameBuffer()) {
    log_e("Can't allocate the screen buffer - check PSRAM.");
    while (1);
  }
  screenBuffer = tft16bits.getBuffer();
  //tft16bits.setRotation(3);

  // clear screen with black
  for (uint32_t i = 0; i < SCR_WIDTH * SCR_HEIGHT; i++)
    screenBuffer[i] = 0;
  bsp_lcd_flush(0, 0, SCR_WIDTH, SCR_HEIGHT, (void *)screenBuffer);

  // Draw touch screen buttons
  drawAllButtons();
  //  drawButton(_paletteW[15], 620, 255);  // UP
  //  drawButton(_paletteW[15], 680, 370);  // LEFT
  //  drawButton(_paletteW[15], 680, 140);  // RIGHT
  //  drawButton(_paletteW[15], 740, 255);  // DOWN

  //  drawButton(_paletteW[6], 640, 10);  // A
  //  drawButton(_paletteW[1], 720, 10);  // B

  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("Pacman DEMO -- Starting.\n\n");
  Serial.flush();

#if 0
  // Testing Audio System and Game Snd Fx
  // Game_Audio_Wav_Class pointer, Shall Interrupt Current sound, float sampleRateMultiplier
  delay(500); GameAudio.PlayWav(&pmChomp, false, 1.0);
  while (GameAudio.IsPlaying());  // wait until done
  delay(500); GameAudio.PlayWav(&pmWav, false, 1.0);
  while (GameAudio.IsPlaying());  // wait until done
  delay(500); GameAudio.PlayWav(&pmDeath, true, 1.0);
  while (GameAudio.IsPlaying());  // wait until done
  delay(500); GameAudio.PlayWav(&pmEatGhost, true, 1.0);
  while (GameAudio.IsPlaying());  // wait until done
#endif
}

void loop() {
  // calculate next game screen, considering Player interaction and game speed
  _game.Step();
  // copies ScrBuf to LCD
  bsp_lcd_flush(0, 0, SCR_WIDTH, SCR_HEIGHT, (void *)screenBuffer);
  // checks Player interaction with TouchScreen
  uint8_t numTouchedPoints = 0;
  uint16_t scrTouchX = 0, scrTouchY = 0;
  if (ft5x06_read_pos(&numTouchedPoints, &scrTouchX, &scrTouchY) == ESP_OK) {
    //    Serial.printf("-- We got %d touched points...", numTouchedPoints);
    if (numTouchedPoints > 0) {
      int8_t pressedButton = getTouchedButton(scrTouchX, scrTouchY);
      //      Serial.printf("X = %d | Y = %d -- BUTTON = %d\n", scrTouchX, scrTouchY, pressedButton);
      switch (pressedButton) {
        case 0:
          ClearKeys();
          but_UP = true;
          break;
        case 1:
          ClearKeys();
          but_LEFT = true;
          break;
        case 2:
          ClearKeys();
          but_RIGHT = true;
          break;
        case 3:
          ClearKeys();
          but_DOWN = true;
          break;
        case 4:
          if (debounceTimeStart < millis()) {
            debounceTimeStart = millis() + 250;
            but_A = true;
          }
          break;
        case 5:
          if (debounceTimeStart < millis()) {
            debounceTimeStart = millis() + 250;
            but_B = true;
          }
          break;
      }
    }
  }
  //  delay(500);  // slow down for debuging...
  
#if 0
    // Not working... ?!
    ft5x06_gesture_t gesture;
    if (fx5x06_read_gesture(&gesture) != ft5x06_gesture_none) {
      switch(gesture) {
        case ft5x06_gesture_move_up:
          Serial.printf("GESTURE UP\n");
          break;
        case ft5x06_gesture_move_left:
          Serial.printf("GESTURE LEFT\n");
          break;
        case ft5x06_gesture_move_down:
          Serial.printf("GESTURE DOWN\n");
          break;
        case ft5x06_gesture_move_right:
          Serial.printf("GESTURE RIGHT\n");
          break;
        case ft5x06_gesture_zoom_in:
          Serial.printf("GESTURE ZOOM IN\n");
          break;
        case ft5x06_gesture_zoom_out:
          Serial.printf("GESTURE ZOOM OUT\n");
          break;
      }
    } else {
        Serial.printf("NO GESTURE DETECTED\n");
    }
#endif
}
