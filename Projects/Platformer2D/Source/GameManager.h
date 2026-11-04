#pragma once

#include <Zamin/Zamin.h>

// O'yin holati: hisob, tangalar soni va ekrandagi matn.
class GameManager : public Zamin::ScriptableEntity
{
public:
    std::string Title = "Tangalar";
    glm::vec2 ScreenOffset = { -8.5f, 5.2f }; // kameraga nisbatan joylashuv

    void OnCreate() override;
    void OnLateUpdate(Zamin::Timestep ts) override;
    // "PlayerFell" xabari (SendMessage orqali keladi).
    void OnMessage(const Zamin::ScriptMessage& message) override;

    void AddCoin();
    void OnPlayerFell();

private:
    void RefreshText();

    int m_Coins = 0;
    int m_TotalCoins = 0;
    int m_Falls = 0;
};
