#include "Board.h"

// Darajalar: oddiydan murakkabga. Har biri yechiladi (Tools/samples/gen_sokoban.py va demo bot - Solve - tekshiradi).
// Yozuv: '#' devor, ' ' pol, '.' maqsad, '$' quti, '*' maqsaddagi quti, '@' o'yinchi, '+' maqsaddagi o'yinchi.

namespace Sokoban {

    const std::vector<Level>& GetLevels()
    {
        static const std::vector<Level> levels = {
            { "Birinchi qadam", "First step", R"(
#######
#     #
# @$ .#
#     #
#######
)" },
            { "Ikki quti", "Two crates", R"(
######
#    #
# $$ #
# .. #
#  @ #
######
)" },
            { "Tor yo'lak", "Narrow aisle", R"(
  ####
###  #
# $  #
# #. ##
# .$  #
##@#  #
 #    #
 ######
)" },
            { "Ombor", "Warehouse", R"(
########
#  .   #
# $$$  #
#  .#  #
## .  @#
 #######
)" },
            { "Zinapoya", "Staircase", R"(
  #####
###   #
# $ # ##
# #  . #
#  $.$ #
##@#.  #
 #  ####
 ####
)" },
            { "Ustunlar", "Pillars", R"(
########
#  .   #
# .##  #
#      #
##$# $ #
#  $ # #
#   @. #
########
)" },
            { "To'rt burchak", "Four corners", R"(
#########
#.  #  .#
# $   $ #
##  @  ##
# $   $ #
#.  #  .#
#########
)" },
            { "Ikki xona", "Two rooms", R"(
##########
#    #   #
#       .#
###. #$###
#      $@#
#   #. $ #
##########
)" },
            { "Chorraha", "Crossroads", R"(
#########
#   #   #
# $ . $ #
#  ###  #
#.  @  .#
#  ###  #
# $ . $ #
#   #   #
#########
)" },
            { "Katta ombor", "Big warehouse", R"(
##########
# .  #   #
# .  $   #
###  # ###
# $$$   .#
#@ .#    #
##########
)" },
        };
        return levels;
    }

}
