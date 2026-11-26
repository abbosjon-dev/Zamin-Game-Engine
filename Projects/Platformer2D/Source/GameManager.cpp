#include "GameManager.h"

using namespace Zamin;

void GameManager::OnEnable()
{
    // We hear that a coin was collected through the event bus: the coin does not need to know GameManager.
    // We listen only while the script is enabled (the subscription is cancelled in OnDisable).
    m_CoinSubscription = Subscribe("CoinCollected", [this](const ScriptMessage&) { AddCoin(); });
}

void GameManager::OnDisable()
{
    Unsubscribe(m_CoinSubscription);
    m_CoinSubscription = 0;
}

void GameManager::OnStart()
{
    // OnStart is called before the first Update, after every script in the scene has gone through OnCreate:
    // coins created by other scripts are counted too. The coins are marked with the "Coin" tag.
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
    // Kept between sessions (SaveData file in ~/.local/share/Zamin Studio/Zamin Platformer: the company and
    // product name of Project settings > Player).
    SaveData::SetInt("TotalCoins", SaveData::GetInt("TotalCoins") + 1);
    RefreshText();
    if (m_Coins == m_TotalCoins)
        ZM_INFO("Well done! All coins collected!");
}

void GameManager::OnPlayerFell()
{
    m_Falls++;
    RefreshText();
}

void GameManager::OnLanguageChanged(const std::string&)
{
    RefreshText();
}

void GameManager::RefreshText()
{
    // The texts come from the language files (Assets/Localization/<til>.json): "{0}: {1} / {2}" ...
    if (auto* text = TryGetComponent<TextComponent>())
    {
        const std::string title = Localization::Has(TitleKey) ? Localize(TitleKey) : Title;
        text->Text = Localize("hud.coins", title, m_Coins, m_TotalCoins);
        if (int total = SaveData::GetInt("TotalCoins"); total > 0)
            text->Text += "   " + Localize("hud.coins_total", total);
        if (m_Falls > 0)
            text->Text += "\n" + Localize("hud.falls", m_Falls);
        if (m_TotalCoins > 0 && m_Coins == m_TotalCoins)
            text->Text += "\n" + Localize("hud.win");
    }
}

ZM_REGISTER_SCRIPT(GameManager,
    ZM_FIELD_TOOLTIP(GameManager, Title, "The word before the score text on the screen (when the language files have no TitleKey)"),
    ZM_FIELD_TOOLTIP(GameManager, TitleKey, "Localization key of the title (Assets/Localization/<language>.json)"),
    ZM_FIELD(GameManager, ScreenOffset).Tooltip("Position of the text relative to the camera (when it is not a UI element)"))
