#pragma once

// Omborchi (Sokoban) - o'yin mantiqi, dvigateldan mustaqil: daraja matnini o'qish, yurish va qutini surish,
// orqaga qaytarish (undo) tarixi, yechilganini tekshirish va yechuvchi (BFS - surishlar soni bo'yicha eng qisqa;
// "o'lik" kataklar - qutini maqsadga hech qachon olib bo'lmaydigan joylar - oldindan chiqarib tashlanadi).
//
// Daraja matni (Sokoban'ning odatiy yozuvi): '#' devor, ' ' pol, '.' maqsad, '$' quti, '*' maqsaddagi quti,
// '@' o'yinchi, '+' maqsaddagi o'yinchi. O'yinchi yeta oladigan pol - darajaning ichi, qolgani tashqarisi.

#include <cstdint>
#include <string>
#include <vector>

namespace Sokoban {

    enum Dir : int { Up = 0, Down, Left, Right };

    struct Level
    {
        const char* Name;     // Uzbek name, shown in the HUD
        const char* NameEn;
        const char* Map;
    };
    const std::vector<Level>& GetLevels();

    class Board
    {
    public:
        enum class Cell : uint8_t { Outside = 0, Wall, Floor };

        struct Move
        {
            bool Moved = false;
            int Dir = 0;
            int From = 0, To = 0;           // the player's cells
            int Box = -1;                   // the box pushed (index into GetBoxes()), -1: none
            int BoxFrom = 0, BoxTo = 0;
        };

        bool Load(const std::string& map);

        int GetWidth() const { return m_Width; }
        int GetHeight() const { return m_Height; }
        int Index(int x, int y) const { return y * m_Width + x; }
        int X(int cell) const { return cell % m_Width; }
        int Y(int cell) const { return cell / m_Width; }
        Cell GetCell(int cell) const { return m_Cells[(size_t)cell]; }
        bool IsWall(int cell) const { return GetCell(cell) == Cell::Wall; }
        bool IsFloor(int cell) const { return GetCell(cell) == Cell::Floor; }
        bool IsGoal(int cell) const { return m_Goals[(size_t)cell]; }
        int GetCellCount() const { return m_Width * m_Height; }
        // a wall that touches the level's floor (the ones worth drawing)
        bool IsVisibleWall(int cell) const;

        int GetPlayer() const { return m_Player; }
        const std::vector<int>& GetBoxes() const { return m_Boxes; }
        int BoxAt(int cell) const;
        int GetBoxesOnGoals() const;
        bool IsSolved() const { return GetBoxesOnGoals() == (int)m_Boxes.size(); }
        int Neighbour(int cell, int dir) const;

        // A step (pushing a box when one is in the way); Moved false: blocked (nothing changes).
        Move Step(int dir);
        // Takes the last step back; false when there is none.
        bool Undo(Move& undone);
        void Reset();                       // back to the start of the level

        int GetMoves() const { return (int)m_History.size(); }
        int GetPushes() const { return m_Pushes; }
        bool CanUndo() const { return !m_History.empty(); }

    private:
        int m_Width = 0, m_Height = 0;
        std::vector<Cell> m_Cells;
        std::vector<bool> m_Goals;
        std::vector<int> m_Boxes, m_StartBoxes;
        int m_Player = 0, m_StartPlayer = 0;
        int m_Pushes = 0;
        std::vector<Move> m_History;
    };

    // The shortest solution by pushes (then walking) as letters u d l r (U D L R: a push); "" when there is none
    // within maxStates (or the board is solved already: check IsSolved first). `states`: how many it looked at.
    std::string Solve(const Board& board, size_t maxStates = 500000, size_t* states = nullptr);
    int DirFromLetter(char letter); // -1 for anything else

}
