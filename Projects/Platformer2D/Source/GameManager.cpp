#include "GameManager.h"

using namespace Zamin;

void GameManager::OnEnable()
{
    // Tanga yig'ilganini hodisalar shinasi orqali eshitamiz: tanga GameManager'ni bilishi shart emas.
    // Faqat skript yoqilgan paytda tinglaymiz (OnDisable'da obuna bekor qilinadi).
    m_CoinSubscription = Subscribe("CoinCollected", [this](const ScriptMessage&) { AddCoin(); });
}

void GameManager::OnDisable()
{
    Unsubscribe(m_CoinSubscription);
    m_CoinSubscription = 0;
}

void GameManager::OnStart()
{
    // OnStart birinchi Update'dan oldin, sahnadagi hamma skriptlar OnCreate'dan o'tgach chaqiriladi:
    // boshqa skriptlar yaratgan tangalar ham hisobga kiradi. Tangalar "Coin" tegi bilan belgilangan.
    m_TotalCoins = (int)FindEntitiesWithTag("Coin").size();
    RefreshText();
}

void GameManager::OnMessage(const ScriptMessage& message)
{
    if (message.Name == "PlayerFell")
        OnPlayerFell();
}

void GameManager::OnLateUpdate(Timestep)
{
    // HUD: keep the text pinned to the camera (only when it is not a screen-space UI element).
    if (HasComponent<UITransformComponent>())
        return;
    if (Entity camera = GetScene().GetPrimaryCameraEntity())
    {
        const auto& cam = camera.GetComponent<TransformComponent>().Translation;
        auto& t = Transform();
        t.Translation = { cam.x + ScreenOffset.x, cam.y + ScreenOffset.y, 1.0f };
    }
}

void GameManager::AddCoin()
{
    m_Coins++;
    // Kept between sessions (SaveData file in ~/.local/share/zamin/Platformer2D).
    SaveData::SetInt("TotalCoins", SaveData::GetInt("TotalCoins") + 1);
    RefreshText();
    if (m_Coins == m_TotalCoins)
        ZM_INFO("Barakalla! Hamma tangalar yig'ildi!");
}

void GameManager::OnPlayerFell()
{
    m_Falls++;
    RefreshText();
}

void GameManager::RefreshText()
{
    if (auto* text = TryGetComponent<TextComponent>())
    {
        text->Text = std::format("{}: {} / {}", Title, m_Coins, m_TotalCoins);
        if (int total = SaveData::GetInt("TotalCoins"); total > 0)
            text->Text += std::format("   (jami: {})", total);
        if (m_Falls > 0)
            text->Text += std::format("\nYiqilishlar: {}", m_Falls);
        if (m_TotalCoins > 0 && m_Coins == m_TotalCoins)
            text->Text += "\nBarakalla! G'alaba!";
    }
}

ZM_REGISTER_SCRIPT(GameManager,
    ZM_FIELD_TOOLTIP(GameManager, Title, "Ekrandagi hisob matni oldidagi so'z"),
    ZM_FIELD(GameManager, ScreenOffset).Tooltip("Matnning kameraga nisbatan joyi (UI elementi bo'lmasa)"))
