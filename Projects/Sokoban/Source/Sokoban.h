#pragma once

// Omborchi (Sokoban) - Zamin Engine namuna o'yini: qutilarni maqsad nuqtalariga itarib qo'yish.
// Skript: SokobanGame - darajani 3D bloklardan quradi (devor, pol, maqsad, quti shablonlaridan), robotni
// boshqaradi (katakma-katak silliq harakat, qutini surish, to'siqqa urilish), orqaga qaytarish (undo) va qaytadan
// boshlash, darajalar menyusi, HUD, menyular, rekordlar (SaveData), demo / bosh menyu orqasida bot (Board.h'dagi
// yechuvchi bilan yechadi).

#include "Board.h"

#include <Zamin/Zamin.h>

#include <string>
#include <unordered_map>
#include <vector>

class SokobanGame : public Zamin::ScriptableEntity
{
public:
    // ---- scene references (set by Tools/samples/gen_sokoban.py)
    Zamin::EntityRef BoardRoot, WallTemplate, FloorTemplate, GoalTemplate, BoxTemplate;
    Zamin::EntityRef Robot, RobotBody, RobotEye, CameraRig;
    Zamin::EntityRef Dust, GoalBurst, Confetti;
    Zamin::EntityRef Hud, LevelText, MovesText, PushesText, BoxesText, BestText, HintText, Banner, BannerSub, DemoBadge;
    Zamin::EntityRef MainMenu, MenuProgress, PlayButton, LanguageButton;
    Zamin::EntityRef LevelsMenu, LevelGrid, LevelsBackButton;
    Zamin::EntityRef PauseMenu, ResumeButton;
    Zamin::EntityRef CompleteMenu, CompleteTitle, CompleteStats, CompleteRecord, NextButton;
    float StepTime = 0.13f;       // seconds a step takes
    float CameraPitch = 56.0f;    // degrees down
    bool AutoDemo = false;        // the bot plays (also env ZAMIN_DEMO=1; ZAMIN_START_LEVEL=N starts there)

    void OnCreate() override;
    void OnStart() override;
    void OnUpdate(Zamin::Timestep ts) override;
    void OnDestroy() override;
    void OnLanguageChanged(const std::string& language) override;

private:
    enum class State { Menu, Playing, Paused, Complete };

    struct Tile
    {
        Zamin::Entity E;
        glm::vec3 Target{ 0.0f };
        float Delay = 0.0f;
    };
    struct Anim
    {
        bool Active = false;
        float Time = 0.0f, Duration = 0.13f;
        glm::vec3 PlayerFrom{ 0.0f }, PlayerTo{ 0.0f };
        int Box = -1;
        glm::vec3 BoxFrom{ 0.0f }, BoxTo{ 0.0f };
        bool Bump = false;            // walked into something: there and back
        bool Many = false;            // a restart: every box glides back
    };

    // ---- flow
    void ShowMenu();
    void StartLevel(int index, bool bot);
    void BuildBoard();
    void OpenPause();
    void ClosePause();
    void LevelComplete();
    void ShowLevels(bool show);

    // ---- play
    void HandleInput(float dt);
    void TryStep(int dir);
    void DoUndo();
    void DoRestart();
    void UpdateAnimation(float dt);
    void UpdateBot(float dt);
    void PlanBot();
    void UpdateCamera(float dt, bool snap);
    void StyleBoxesAndGoals(bool celebrate);

    // ---- presentation
    glm::vec3 CellPosition(int cell) const;   // on the floor
    glm::vec3 BoxPosition(int cell) const;    // a crate's centre there
    void UpdateHud();
    void RefreshTexts();
    void RefreshLevelButtons();
    void ShowBanner(const std::string& title, const std::string& subtitle, float seconds);
    void Burst(const Zamin::EntityRef& ref, const glm::vec3& at, int count);
    void SetText(const Zamin::EntityRef& ref, const std::string& text);
    void SetActiveRef(const Zamin::EntityRef& ref, bool active);
    std::string LevelName(int index) const;
    int GetBest(int index) const;
    int CountSolved() const;

    State m_State = State::Menu;
    Sokoban::Board m_Board;
    int m_Level = 0;
    bool m_Bot = true;
    bool m_DemoRun = false;
    std::vector<Tile> m_Tiles;
    std::vector<Zamin::Entity> m_BoxEntities;
    std::unordered_map<int, Zamin::Entity> m_GoalEntities;
    float m_IntroTimer = 0.0f;
    float m_BoxLift = 0.42f;          // a crate's centre above the floor (the template's height)
    Anim m_Anim;
    int m_QueuedDir = -1;
    bool m_QueuedUndo = false;
    int m_HeldDir = -1;
    float m_HoldTimer = 0.0f;
    float m_RobotYaw = 0.0f, m_RobotYawGoal = 0.0f;
    float m_Bob = 0.0f, m_Lean = 0.0f;
    float m_Spin = 0.0f;              // the happy spin after a solved level
    int m_LastOnGoals = 0;
    // bot
    std::string m_BotPath;
    size_t m_BotIndex = 0;
    float m_BotTimer = 0.0f;
    int m_SolvedInRun = 0;
    // timers
    float m_CompleteTimer = 0.0f;
    float m_BannerTimer = 0.0f;
    float m_Time = 0.0f;
    glm::vec3 m_CameraPos{ 0.0f, 10.0f, 8.0f };
};
