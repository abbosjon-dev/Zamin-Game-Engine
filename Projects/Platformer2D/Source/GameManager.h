#pragma once

#include <Zamin/Zamin.h>

// O'yin holati: hisob, tangalar soni va ekrandagi matn.
class GameManager : public Zamin::ScriptableEntity
{
public:
    std::string Title = "Tangalar";
    // Til fayllaridagi kalit: bor bo'lsa sarlavha joriy tilda (Localize), aks holda Title.
    std::string TitleKey = "hud.coins_title";
    glm::vec2 ScreenOffset = { -8.5f, 5.2f }; // kameraga nisbatan joylashuv

    void OnEnable() override;
    void OnDisable() override;
    void OnStart() override;
    void OnLateUpdate(Zamin::Timestep ts) override;
    // "PlayerFell" xabari (SendMessage orqali keladi).
    void OnMessage(const Zamin::ScriptMessage& message) override;
    // Til o'zgardi (pauza menyusi): hisob matni yangi tilda qayta yoziladi.
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
