#include "Board.h"

// Levels: from simple to hard. Each can be solved (Tools/samples/gen_sokoban.py and the demo bot - Solve - check it).
// Notation: '#' wall, ' ' floor, '.' goal, '$' crate, '*' crate on a goal, '@' player, '+' player on a goal.

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
