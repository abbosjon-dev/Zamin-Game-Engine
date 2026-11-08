#include "Breakout.h"

// Darajalar: har biri 13 ustunli qatorlar (yuqoridan pastga). Belgilar:
//   .            bo'sh
//   r o y g c b p k w   oddiy g'isht (qizil, to'q sariq, sariq, yashil, moviy, ko'k, binafsha, pushti, oq) - bir zarba
//   R O Y G C B P K      qattiq g'isht (shu ranglarda) - ikki zarba, birinchisidan keyin yoriladi
//   S            po'lat - uch zarba
//   #            oltin - sinmaydi (to'siq)
//   X            bomba - sinsa, atrofidagi g'ishtlarni ham sindiradi
// Daraja tugashi uchun sinadigan hamma g'isht sinishi kerak (oltin hisobga kirmaydi).

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
