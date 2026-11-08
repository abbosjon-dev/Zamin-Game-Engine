#include "Board.h"

#include <algorithm>
#include <deque>
#include <sstream>
#include <unordered_map>

namespace Sokoban {

    static const int s_DX[4] = { 0, 0, -1, 1 };
    static const int s_DY[4] = { -1, 1, 0, 0 };

    bool Board::Load(const std::string& map)
    {
        std::vector<std::string> rows;
        std::stringstream in(map);
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (rows.empty() && line.find_first_not_of(' ') == std::string::npos)
                continue; // leading blank lines
            rows.push_back(line);
        }
        while (!rows.empty() && rows.back().find_first_not_of(' ') == std::string::npos)
            rows.pop_back();
        if (rows.empty())
            return false;
        m_Height = (int)rows.size();
        m_Width = 0;
        for (const std::string& r : rows)
            m_Width = std::max(m_Width, (int)r.size());
        m_Cells.assign((size_t)(m_Width * m_Height), Cell::Outside);
        m_Goals.assign(m_Cells.size(), false);
        m_Boxes.clear();
        m_Player = -1;
        for (int y = 0; y < m_Height; y++)
            for (int x = 0; x < (int)rows[(size_t)y].size(); x++)
            {
                const char ch = rows[(size_t)y][(size_t)x];
                const int cell = Index(x, y);
                if (ch == '#')
                    m_Cells[(size_t)cell] = Cell::Wall;
                if (ch == '.' || ch == '*' || ch == '+')
                    m_Goals[(size_t)cell] = true;
                if (ch == '$' || ch == '*')
                    m_Boxes.push_back(cell);
                if (ch == '@' || ch == '+')
                    m_Player = cell;
            }
        if (m_Player < 0 || m_Boxes.empty())
            return false;
        // the floor: what the player can walk to (boxes do not stop the flood)
        std::vector<int> stack{ m_Player };
        m_Cells[(size_t)m_Player] = Cell::Floor;
        while (!stack.empty())
        {
            const int c = stack.back();
            stack.pop_back();
            for (int d = 0; d < 4; d++)
            {
                const int n = Neighbour(c, d);
                if (n >= 0 && m_Cells[(size_t)n] == Cell::Outside)
                {
                    m_Cells[(size_t)n] = Cell::Floor;
                    stack.push_back(n);
                }
            }
        }
        int goals = 0;
        for (size_t i = 0; i < m_Goals.size(); i++)
            if (m_Goals[i] && m_Cells[i] == Cell::Floor)
                goals++;
        m_StartBoxes = m_Boxes;
        m_StartPlayer = m_Player;
        m_Pushes = 0;
        m_History.clear();
        return goals == (int)m_Boxes.size();
    }

    int Board::Neighbour(int cell, int dir) const
    {
        const int x = X(cell) + s_DX[dir], y = Y(cell) + s_DY[dir];
        if (x < 0 || y < 0 || x >= m_Width || y >= m_Height)
            return -1;
        return Index(x, y);
    }

    bool Board::IsVisibleWall(int cell) const
    {
        if (!IsWall(cell))
            return false;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
            {
                const int x = X(cell) + dx, y = Y(cell) + dy;
                if (x >= 0 && y >= 0 && x < m_Width && y < m_Height && IsFloor(Index(x, y)))
                    return true;
            }
        return false;
    }

    int Board::BoxAt(int cell) const
    {
        for (size_t i = 0; i < m_Boxes.size(); i++)
            if (m_Boxes[i] == cell)
                return (int)i;
        return -1;
    }

    int Board::GetBoxesOnGoals() const
    {
        int n = 0;
        for (int b : m_Boxes)
            if (IsGoal(b))
                n++;
        return n;
    }

    Board::Move Board::Step(int dir)
    {
        Move m;
        m.Dir = dir;
        m.From = m_Player;
        const int to = Neighbour(m_Player, dir);
        if (to < 0 || !IsFloor(to))
            return m;
        if (const int box = BoxAt(to); box >= 0)
        {
            const int beyond = Neighbour(to, dir);
            if (beyond < 0 || !IsFloor(beyond) || BoxAt(beyond) >= 0)
                return m;
            m.Box = box;
            m.BoxFrom = to;
            m.BoxTo = beyond;
            m_Boxes[(size_t)box] = beyond;
            m_Pushes++;
        }
        m.To = to;
        m.Moved = true;
        m_Player = to;
        m_History.push_back(m);
        return m;
    }

    bool Board::Undo(Move& undone)
    {
        if (m_History.empty())
            return false;
        undone = m_History.back();
        m_History.pop_back();
        m_Player = undone.From;
        if (undone.Box >= 0)
        {
            m_Boxes[(size_t)undone.Box] = undone.BoxFrom;
            m_Pushes--;
        }
        return true;
    }

    void Board::Reset()
    {
        m_Boxes = m_StartBoxes;
        m_Player = m_StartPlayer;
        m_Pushes = 0;
        m_History.clear();
    }

    int DirFromLetter(char letter)
    {
        switch (letter)
        {
            case 'u': case 'U': return Up;
            case 'd': case 'D': return Down;
            case 'l': case 'L': return Left;
            case 'r': case 'R': return Right;
            default: return -1;
        }
    }

    // ------------------------------------------------------------------------------------------ solver

    namespace {

        struct Node
        {
            std::string Parent;   // the key of the state before the push ("" for the start)
            int BoxCell = 0;      // where the pushed box was
            int Dir = 0;
        };

        // The cells the player reaches from `from` without moving a box; `cameFrom` for walking back a path.
        void Reach(const Board& board, int from, const std::vector<uint8_t>& boxes, std::vector<int>& cameFrom)
        {
            cameFrom.assign((size_t)board.GetCellCount(), -2);
            std::deque<int> queue{ from };
            cameFrom[(size_t)from] = -1;
            while (!queue.empty())
            {
                const int c = queue.front();
                queue.pop_front();
                for (int d = 0; d < 4; d++)
                {
                    const int n = board.Neighbour(c, d);
                    if (n >= 0 && board.IsFloor(n) && !boxes[(size_t)n] && cameFrom[(size_t)n] == -2)
                    {
                        cameFrom[(size_t)n] = c;
                        queue.push_back(n);
                    }
                }
            }
        }

        // A state's key: the box cells (sorted) and the smallest cell the player can reach (all player positions
        // in one region are the same state for pushing).
        std::string MakeKey(int normalizedPlayer, std::vector<int> boxes)
        {
            std::sort(boxes.begin(), boxes.end());
            std::string key;
            key.reserve(boxes.size() * 2 + 2);
            auto put = [&](int v) { key.push_back((char)(v & 0xff)); key.push_back((char)(v >> 8)); };
            put(normalizedPlayer);
            for (int b : boxes)
                put(b);
            return key;
        }

    }

    std::string Solve(const Board& start, size_t maxStates, size_t* statesOut)
    {
        const int cells = start.GetCellCount();
        // live cells: where a box can still be brought to a goal (pulled backwards from every goal)
        std::vector<uint8_t> live((size_t)cells, 0);
        std::vector<int> stack;
        for (int c = 0; c < cells; c++)
            if (start.IsGoal(c) && start.IsFloor(c))
            {
                live[(size_t)c] = 1;
                stack.push_back(c);
            }
        while (!stack.empty())
        {
            const int g = stack.back();
            stack.pop_back();
            for (int d = 0; d < 4; d++)
            {
                const int a = start.Neighbour(g, d);
                const int b = a >= 0 ? start.Neighbour(a, d) : -1;
                if (a >= 0 && b >= 0 && start.IsFloor(a) && start.IsFloor(b) && !live[(size_t)a])
                {
                    live[(size_t)a] = 1;
                    stack.push_back(a);
                }
            }
        }

        struct Item
        {
            std::string Key;
            int Player;
            std::vector<int> Boxes;
        };
        std::unordered_map<std::string, Node> visited;
        std::deque<Item> queue;
        std::vector<uint8_t> occupied((size_t)cells, 0), scratch((size_t)cells, 0);
        std::vector<int> cameFrom;

        // (its own occupancy buffer: the search loop's `occupied` must stay the state being expanded)
        auto normalize = [&](int player, const std::vector<int>& boxes) {
            std::fill(scratch.begin(), scratch.end(), 0);
            for (int b : boxes)
                scratch[(size_t)b] = 1;
            Reach(start, player, scratch, cameFrom);
            for (int c = 0; c < cells; c++)
                if (cameFrom[(size_t)c] != -2)
                    return c;
            return player;
        };

        const std::string startKey = MakeKey(normalize(start.GetPlayer(), start.GetBoxes()), start.GetBoxes());
        visited[startKey] = Node{};
        queue.push_back({ startKey, start.GetPlayer(), start.GetBoxes() });
        std::string goalKey;
        size_t states = 0;
        while (!queue.empty() && goalKey.empty())
        {
            Item item = std::move(queue.front());
            queue.pop_front();
            if (++states > maxStates)
                break;
            std::fill(occupied.begin(), occupied.end(), 0);
            for (int b : item.Boxes)
                occupied[(size_t)b] = 1;
            std::vector<int> reach;
            Reach(start, item.Player, occupied, reach);
            for (size_t bi = 0; bi < item.Boxes.size() && goalKey.empty(); bi++)
            {
                const int box = item.Boxes[bi];
                for (int d = 0; d < 4; d++)
                {
                    const int behind = start.Neighbour(box, d ^ 1); // Up<->Down, Left<->Right
                    const int to = start.Neighbour(box, d);
                    if (behind < 0 || to < 0 || reach[(size_t)behind] == -2 || !live[(size_t)to] || occupied[(size_t)to])
                        continue;
                    std::vector<int> boxes = item.Boxes;
                    boxes[bi] = to;
                    const std::string key = MakeKey(normalize(box, boxes), boxes);
                    if (visited.contains(key))
                        continue;
                    visited[key] = Node{ item.Key, box, d };
                    bool solved = true;
                    for (int b : boxes)
                        solved = solved && start.IsGoal(b);
                    if (solved)
                    {
                        goalKey = key;
                        break;
                    }
                    queue.push_back({ key, box, std::move(boxes) });
                }
            }
        }
        if (statesOut)
            *statesOut = states;
        if (goalKey.empty())
            return {};

        // the pushes, first to last; then the walks between them
        std::vector<std::pair<int, int>> pushes; // (box cell, dir)
        for (std::string key = goalKey; key != startKey;)
        {
            const Node& node = visited[key];
            pushes.emplace_back(node.BoxCell, node.Dir);
            key = node.Parent;
        }
        std::reverse(pushes.begin(), pushes.end());
        static const char letters[4] = { 'u', 'd', 'l', 'r' };
        std::string path;
        int player = start.GetPlayer();
        std::vector<int> boxes = start.GetBoxes();
        for (auto [boxCell, dir] : pushes)
        {
            std::fill(occupied.begin(), occupied.end(), 0);
            for (int b : boxes)
                occupied[(size_t)b] = 1;
            Reach(start, player, occupied, cameFrom);
            const int stand = start.Neighbour(boxCell, dir ^ 1);
            if (stand < 0 || cameFrom[(size_t)stand] == -2)
                return {}; // cannot happen for a path the search found
            std::string walk;
            for (int c = stand; c != player && c >= 0;)
            {
                const int prev = cameFrom[(size_t)c];
                for (int d = 0; d < 4; d++)
                    if (start.Neighbour(prev, d) == c)
                        walk.push_back(letters[d]);
                c = prev;
            }
            std::reverse(walk.begin(), walk.end());
            path += walk;
            path.push_back((char)(letters[dir] - 'a' + 'A'));
            for (int& b : boxes)
                if (b == boxCell)
                    b = start.Neighbour(boxCell, dir);
            player = boxCell;
        }
        return path;
    }

}
