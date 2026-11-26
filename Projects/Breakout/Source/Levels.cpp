#include "Breakout.h"

// Levels: each is rows of 13 columns (top to bottom). Characters:
//   .            empty
//   r o y g c b p k w   a plain brick (red, orange, yellow, green, cyan, blue, purple, pink, white) - one hit
//   R O Y G C B P K      a hard brick (in the same colours) - two hits, it cracks after the first
//   S            steel - three hits
//   #            gold - unbreakable (an obstacle)
//   X            a bomb - when it breaks it breaks the bricks around it too
// A level is cleared when every breakable brick is broken (gold does not count).

const std::vector<LevelLayout>& GetLevelLayouts()
{
    static const std::vector<LevelLayout> levels = {
        { "level.1",
          {
              ".............",
              "rrrrrrrrrrrrr",
              "ooooooooooooo",
              "yyyyyyyyyyyyy",
              "ggggggggggggg",
              "ccccccccccccc",
              "bbbbbbbbbbbbb",
          } },
        { "level.2",
          {
              "......S......",
              ".....PPP.....",
              "....ppXpp....",
              "...bbbbbbb...",
              "..ccccXcccc..",
              ".ggggggggggg.",
              "yyyyyyyyyyyyy",
              "OOOOOOOOOOOOO",
          } },
        { "level.3",
          {
              "#.#.#...#.#.#",
              "SSSSS...SSSSS",
              "SrrrS.y.SrrrS",
              "SrXrS.Y.SrXrS",
              "SSSSSOOOSSSSS",
              "..ooooXoooo..",
              "..YYYYYYYYY..",
              "..ggggggggg..",
          } },
        { "level.4",
          {
              "PyPyPyPyPyPyP",
              "yPyPyPyPyPyPy",
              "PyPyPSSSPyPyP",
              "yPyPySXSyPyPy",
              "PyPyPSSSPyPyP",
              "yPyPyPyPyPyPy",
              "cgcgcgcgcgcgc",
          } },
        { "level.5",
          {
              "..KKK...KKK..",
              ".kkkkk.kkkkk.",
              "kkkkkkkkkkkkk",
              "kkkXkkkkkXkkk",
              ".rrrrrrrrrrr.",
              "..rrrrXrrrr..",
              "...rrrrrrr...",
              "....RRRRR....",
              ".....RRR.....",
              "......R......",
          } },
        { "level.6",
          {
              "pppp#####pppp",
              "p..p#yyy#p..p",
              "pXpp#yXy#ppXp",
              "pppp#...#pppp",
              "###...c...###",
              "cccc.CCC.cccc",
              "ggggggggggggg",
              "GGGGGGGGGGGGG",
          } },
    };
    return levels;
}
