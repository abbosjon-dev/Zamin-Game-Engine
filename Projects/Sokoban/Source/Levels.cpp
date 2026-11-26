#include "Board.h"

// Levels: from simple to hard. Each can be solved (Tools/samples/gen_sokoban.py and the demo bot - Solve - check it).
// The names are English; the HUD shows the game language's name (levels.names.<n> in Assets/Localization).
// Notation: '#' wall, ' ' floor, '.' goal, '$' crate, '*' crate on a goal, '@' player, '+' player on a goal.

namespace Sokoban {

    const std::vector<Level>& GetLevels()
    {
        static const std::vector<Level> levels = {
            { "First step", R"(
#######
#     #
# @$ .#
#     #
#######
)" },
            { "Two crates", R"(
######
#    #
# $$ #
# .. #
#  @ #
######
)" },
            { "Narrow aisle", R"(
  ####
###  #
# $  #
# #. ##
# .$  #
##@#  #
 #    #
 ######
)" },
            { "Warehouse", R"(
########
#  .   #
# $$$  #
#  .#  #
## .  @#
 #######
)" },
            { "Staircase", R"(
  #####
###   #
# $ # ##
# #  . #
#  $.$ #
##@#.  #
 #  ####
 ####
)" },
            { "Pillars", R"(
########
#  .   #
# .##  #
#      #
##$# $ #
#  $ # #
#   @. #
########
)" },
            { "Four corners", R"(
#########
#.  #  .#
# $   $ #
##  @  ##
# $   $ #
#.  #  .#
#########
)" },
            { "Two rooms", R"(
##########
#    #   #
#       .#
###. #$###
#      $@#
#   #. $ #
##########
)" },
            { "Crossroads", R"(
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
            { "Big warehouse", R"(
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
