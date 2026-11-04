#include "GameManager.h"

using namespace Zamin;

void GameManager::OnCreate()
{
    for (auto e : GetScene().GetAllEntitiesWith<TagComponent>())
        if (GetScene().GetRegistry().get<TagComponent>(e).Tag == "Coin")
            m_TotalCoins++;
    RefreshText();
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
        if (m_Falls > 0)
            text->Text += std::format("\nYiqilishlar: {}", m_Falls);
        if (m_TotalCoins > 0 && m_Coins == m_TotalCoins)
            text->Text += "\nBarakalla! G'alaba!";
    }
}

ZM_REGISTER_SCRIPT(GameManager, ZM_FIELD(GameManager, Title), ZM_FIELD(GameManager, ScreenOffset))
