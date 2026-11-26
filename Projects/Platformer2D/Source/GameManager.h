#pragma once

#include <Zamin/Zamin.h>

// The game state: the score, the number of coins and the text on the screen.
class GameManager : public Zamin::ScriptableEntity
{
public:
    std::string Title = "Coins";
    // A key in the language files: when it exists the title is in the current language (Localize), otherwise Title.
    std::string TitleKey = "hud.coins_title";
    glm::vec2 ScreenOffset = { -8.5f, 5.2f }; // the position relative to the camera

    void OnEnable() override;
    void OnDisable() override;
    void OnStart() override;
    void OnLateUpdate(Zamin::Timestep ts) override;
    // The "PlayerFell" message (arrives through SendMessage).
    void OnMessage(const Zamin::ScriptMessage& message) override;
    // The language changed (the pause menu): the score text is written again in the new language.
    void OnLanguageChanged(const std::string& language) override;

    void AddCoin();
    void OnPlayerFell();

private:
    void RefreshText();

    int m_Coins = 0;
    int m_TotalCoins = 0;
    int m_Falls = 0;
    Zamin::SubscriptionID m_CoinSubscription = 0;
};
