#pragma once

/// @file
/// @brief The public API umbrella header (for games and tools): includes every engine header a game uses.

/**
 * @mainpage Zamin Engine API reference
 *
 * Zamin Engine is a C++20 2D / 3D game engine for Linux: the engine library (`libZamin.so`), the editor
 * (ZaminEditor), the player that runs exported games (ZaminPlayer) and games whose logic is C++ script
 * modules (`lib<Game>.so`). This reference is generated from the engine's public headers
 * (`Zamin/src/Zamin`); the guides - in Uzbek - are the Markdown files of `docs/` (start with
 * `docs/README.md`; the internal structure is `docs/Architecture.md`).
 *
 * Where to start:
 * - **Scripts**: Zamin::ScriptableEntity (lifecycle, callbacks, the script API), ZM_REGISTER_SCRIPT and
 *   ZM_FIELD (Zamin/Scripting/ScriptEngine.h), Zamin::Input, Zamin::Time.
 * - **Scenes**: Zamin::Scene, Zamin::Entity, the components (Zamin/Scene/Components.h),
 *   Zamin::SceneManager, Zamin::SceneSerializer.
 * - **Systems**: Zamin::Renderer2D, Zamin::Renderer3D, Zamin::PhysicsWorld2D, Zamin::PhysicsWorld3D,
 *   Zamin::AudioEngine and Zamin::Audio, Zamin::Project.
 * - **Programs**: Zamin::Application, Zamin::Layer (Zamin/Core/EntryPoint.h supplies `main`).
 *
 * A script:
 * @code
 * #include <Zamin/Zamin.h>
 *
 * class Spinner : public Zamin::ScriptableEntity
 * {
 * public:
 *     float Speed = 1.5f;
 *     void OnUpdate(Zamin::Timestep ts) override { Transform().Rotation.y += Speed * ts; }
 * };
 * ZM_REGISTER_SCRIPT(Spinner, ZM_FIELD(Spinner, Speed))
 * @endcode
 */

#include "Zamin/Core/Base.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Core/LogSettings.h"
#include "Zamin/Core/LogFile.h"
#include "Zamin/Core/Assert.h"
#include "Zamin/Core/CrashHandler.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Core/JobSystem.h"
#include "Zamin/Core/MemoryStats.h"
#include "Zamin/Core/Application.h"
#include "Zamin/Core/BuildInfo.h"
#include "Zamin/Core/Layer.h"
#include "Zamin/Core/Input.h"
#include "Zamin/Core/Cursor.h"
#include "Zamin/Core/InputActions.h"
#include "Zamin/Core/SaveData.h"
#include "Zamin/Core/GameState.h"
#include "Zamin/Core/Services.h"
#include "Zamin/Core/Display.h"
#include "Zamin/Core/FramePacer.h"
#include "Zamin/Core/Version.h"
#include "Zamin/Core/KeyCodes.h"
#include "Zamin/Core/Timestep.h"
#include "Zamin/Core/Time.h"
#include "Zamin/Core/Stopwatch.h"
#include "Zamin/Core/Timer.h"
#include "Zamin/Core/UUID.h"
#include "Zamin/Events/Event.h"
#include "Zamin/Localization/Localization.h"

#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Renderer/Renderer2D.h"
#include "Zamin/Renderer/Renderer3D.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Renderer/Framebuffer.h"
#include "Zamin/Renderer/Texture.h"
#include "Zamin/Renderer/Shader.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Renderer/ShaderCache.h"
#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/Frustum.h"
#include "Zamin/Renderer/EditorCamera.h"
#include "Zamin/Renderer/EditorViewSettings.h"
#include "Zamin/Editor/EditorPreferences.h"
#include "Zamin/Editor/ConsoleFilter.h"
#include "Zamin/Editor/GameViewResolution.h"
#include "Zamin/Editor/EditorAutosave.h"
#include "Zamin/Editor/RecentFiles.h"
#include "Zamin/Editor/ShortcutRegistry.h"
#include "Zamin/Editor/UndoHistory.h"
#include "Zamin/Editor/PrefabFieldLabels.h"
#include "Zamin/Editor/PrefabPlacement.h"
#include "Zamin/Editor/EditorLayouts.h"
#include "Zamin/Editor/SceneViews.h"
#include "Zamin/Editor/SceneStatistics.h"
#include "Zamin/Editor/ExternalEditor.h"
#include "Zamin/Editor/CompilerDiagnostics.h"
#include "Zamin/Editor/IdleThrottle.h"
#include "Zamin/Editor/ProjectSettingsIndex.h"
#include "Zamin/Renderer/PostProcess.h"
#include "Zamin/Renderer/DisplayPass.h"
#include "Zamin/Renderer/SplashScreen.h"
#include "Zamin/Renderer/Quality.h"
#include "Zamin/Renderer/GraphicsSettings.h"
#include "Zamin/Renderer/GraphicsContext.h"
#include "Zamin/Renderer/SelectionOutline.h"
#include "Zamin/Renderer/Environment.h"
#include "Zamin/Renderer/MaterialAsset.h"
#include "Zamin/Renderer/MaterialPreview.h"
#include "Zamin/Renderer/MaterialPresets.h"
#include "Zamin/Renderer/SpriteSheet.h"
#include "Zamin/Renderer/Lighting2D.h"

#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/ComponentRegistry.h"
#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Scene/EntityClipboard.h"
#include "Zamin/Scene/Prefab.h"
#include "Zamin/Scene/PrefabCache.h"
#include "Zamin/Scene/PrefabDatabase.h"
#include "Zamin/Scene/PrefabOverrides.h"
#include "Zamin/Scene/SceneRecovery.h"
#include "Zamin/Scene/SceneFileReferences.h"
#include "Zamin/Scene/SceneManager.h"
#include "Zamin/Scene/SceneLoadTiming.h"
#include "Zamin/Scene/ParticlePresets.h"
#include "Zamin/Scene/EntitySelection.h"
#include "Zamin/Scene/SortingLayers.h"
#include "Zamin/Scene/Parallax.h"
#include "Zamin/Scene/Camera2D.h"
#include "Zamin/Scene/TilemapTools.h"
#include "Zamin/Scene/TilesetProperties.h"
#include "Zamin/Scene/TintAnimation.h"
#include "Zamin/Scene/SpriteClip.h"
#include "Zamin/Scene/PixelPerfect.h"
#include "Zamin/UI/UIWidgets.h"
#include "Zamin/UI/TextEdit.h"
#include "Zamin/UI/TextLayout.h"
#include "Zamin/UI/UIAnimation.h"
#include "Zamin/UI/UILayout.h"
#include "Zamin/Physics/CollisionLayers.h"
#include "Zamin/Physics/PhysicsDefaults.h"
#include "Zamin/Physics/PhysicsMaterial.h"
#include "Zamin/Physics/PhysicsWorld2D.h"
#include "Zamin/Physics/Polygon2D.h"
#include "Zamin/Physics/PhysicsWorld3D.h"
#include "Zamin/Physics/CollisionShapes3D.h"
#include "Zamin/AI/NavGrid.h"
#include "Zamin/AI/NavGridBuilder.h"
#include "Zamin/AI/NavPathfinder.h"
#include "Zamin/AI/NavPathSmoothing.h"
#include "Zamin/AI/NavCarving.h"
#include "Zamin/AI/NavDebugDraw.h"
#include "Zamin/AI/Sight.h"
#include "Zamin/AI/StateMachine.h"
#include "Zamin/AI/BehaviorTree.h"
#include "Zamin/AI/Formation.h"
#include "Zamin/AI/Steering.h"
#include "Zamin/Debug/DebugDraw.h"
#include "Zamin/Debug/MeshNormals.h"

#include "Zamin/Scripting/ScriptableEntity.h"
#include "Zamin/Scripting/ScriptEngine.h"
#include "Zamin/Scripting/EventBus.h"
#include "Zamin/Scripting/ScriptTemplate.h"

#include "Zamin/Audio/AudioEngine.h"
#include "Zamin/Audio/Audio.h"
#include "Zamin/Audio/AudioMixer.h"
#include "Zamin/Audio/AudioClip.h"
#include "Zamin/Audio/AudioFilter.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Project/PlayerSettings.h"
#include "Zamin/Project/PlayerLaunch.h"
#include "Zamin/Project/ProjectMigration.h"
#include "Zamin/Project/AssetFiles.h"
#include "Zamin/Project/ExportAssets.h"
#include "Zamin/Project/ExportBuild.h"
#include "Zamin/Project/PrefabExport.h"
#include "Zamin/Asset/AssetTypes.h"
#include "Zamin/Asset/AssetMetadata.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Asset/FileWatcher.h"
#include "Zamin/Asset/AssetHotReload.h"
#include "Zamin/Asset/AssetImporter.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Asset/AsyncLoader.h"
#include "Zamin/Asset/AssetThumbnails.h"
#include "Zamin/Asset/AssetInfo.h"
#include "Zamin/Asset/AssetReferences.h"
#include "Zamin/Math/Math.h"
#include "Zamin/Math/MathUtils.h"
#include "Zamin/Math/Quaternion.h"
#include "Zamin/Math/Random.h"
#include "Zamin/Math/Easing.h"
#include "Zamin/Math/Noise.h"
#include "Zamin/Math/CurveKey.h"
#include "Zamin/Math/AnimationCurve.h"
#include "Zamin/Math/Spline.h"
#include "Zamin/Math/Color.h"
#include "Zamin/Math/Geometry.h"
#include "Zamin/Utils/PlatformUtils.h"
#include "Zamin/Utils/FileSystem.h"
#include "Zamin/Utils/StringUtils.h"
#include "Zamin/Utils/Image.h"
