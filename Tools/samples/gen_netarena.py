#!/usr/bin/env python3
"""NetArena (Net Arena): the networking sample - a 2D arena where 2+ players collect coins (docs/Networking.md).

Writes the project, the arena scene, the prefabs (Player: predicted movement and the C++ NetPlayer script with its
replicated score and its Collect / Burst RPCs; Coin; Spark: the local burst effect), the ArenaRules graph (it spawns a
player for every client that connects - Net.Spawn - and has a replicated Joins counter with an Event.Rep, a server RPC
Horn any client may call and a multicast Honk), the game icon, the .zmeta files and the module's CMakeLists.txt. The
C++ scripts (Projects/NetArena/Source) are written by hand. Deterministic: the same files every run.

    python3 Tools/samples/gen_netarena.py
    ZAMIN_SAMPLES_OUT=/tmp/out python3 Tools/samples/gen_netarena.py
"""
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import graphkit  # noqa: E402
import samplekit as k  # noqa: E402

NAME = "NetArena"
PROJECT = k.project_dir(NAME)
ASSETS = os.path.join(PROJECT, "Assets")
ARENA = (11.0, 6.0)  # half extents of the playing field
PLAYER_SPEED = 6.0


def entity_json(eid, tag, pos=(0, 0, 0), scale=(1, 1, 1), **components):
    e = {"Entity": eid, "Tag": tag, "Transform": {"Translation": k.v(*pos), "Rotation": k.v(0, 0, 0), "Scale": k.v(*scale)}}
    e.update(components)
    return e


def prefab(rel, entities):
    k.write_json(os.path.join(ASSETS, rel), {"ZaminEntities": 1, "Entities": entities})
    k.write_zmeta(ASSETS, rel, "Prefab", {}, NAME)


def prefabs():
    prefab("Prefabs/Player.zprefab", [entity_json(
        1, "Player", scale=(0.9, 0.9, 1),
        SpriteRenderer=k.sprite(col=(1, 1, 1, 1), order=10),
        NetworkIdentity={"AlwaysRelevant": False, "RelevanceRadius": 0.0},
        NetworkTransform={"Mode": "Predict", "MoveAction": "Move", "MoveSpeed": PLAYER_SPEED, "Plane": "XY", "ClampToBounds": True,
                          "BoundsMin": k.v(-ARENA[0], -ARENA[1]), "BoundsMax": k.v(ARENA[0], ARENA[1]), "InterpolationDelay": 0.1},
        NativeScript=k.script("NetPlayer", Score=0, Tint=("Vec4", (1, 1, 1, 1))))])
    prefab("Prefabs/Coin.zprefab", [entity_json(
        1, "Coin", scale=(0.6, 0.6, 1),
        CircleRenderer=k.circle((1.0, 0.82, 0.2, 1), 0.35, 0.02, order=5),
        NetworkIdentity={"AlwaysRelevant": False, "RelevanceRadius": 0.0},
        NetworkTransform={"Mode": "Interpolate", "InterpolationDelay": 0.1})])
    prefab("Prefabs/Spark.zprefab", [entity_json(
        1, "Spark", scale=(0.4, 0.4, 1),
        CircleRenderer=k.circle((1.0, 0.95, 0.6, 1), 0.25, 0.05, order=20),
        NativeScript=k.script("NetSpark", Lifetime=0.45))])


def rules_graph():
    g = graphkit.Graph("ArenaRules")
    g.data["Version"] = 7
    g.var("Joins", "Number", 0)
    g.var("Horns", "Number", 0)
    g.var("LastHorn", "String", "")
    g.data["Variables"]["Joins"]["Replicated"] = True
    g.data["Variables"]["Horns"]["Replicated"] = True
    g.data["Rpcs"] = {
        "Horn": {"Kind": "Server", "Reliable": True, "AnyClient": True, "Params": []},
        "Honk": {"Kind": "Multicast", "Reliable": True, "AnyClient": False,
                 "Params": [{"Name": "Text", "Type": "String", "Default": ""}]},
    }
    # Server: every client that connects (the host counts as client 0) gets a player of its own.
    g.lane("join")
    joined = g.n("Event.ClientConnected")
    server = g.br((joined, "Then"), (g.n("Net.IsServer"), "Value"))
    x = g.num("Subtract", g.num("Multiply", (joined, "Client"), 2.5), 5)
    at = g.n("MakeVec3", Y=0, Z=0)
    g.l(x[0], x[1], at, "X")
    spawn = g.ex((server, "True"), "Net.Spawn", Path="Prefabs/Player.zprefab")
    g.l(at, "Value", spawn, "Position")
    g.l(joined, "Client", spawn, "Owner")
    count = g.setv((spawn, "Then"), "Joins", data=g.num("Add", (g.get("Joins"), "Value"), 1))
    g.print_(count[0], g.text(["NETARENA player spawned for client ", (joined, "Client", "number")]))
    # Clients: the replicated counter arrives (Event.Rep).
    g.lane("rep")
    rep = g.n("Event.Rep", Name="Joins")
    g.print_(rep, g.text(["BLUEPRINT_NET joins=", (g.get("Joins"), "Value", "number")]))
    # Anyone: H calls the server RPC Horn; the server counts it and multicasts Honk to everyone.
    g.lane("horn")
    pressed = g.when("Event.Update", (g.n("ActionJustPressed", Name="Horn"), "Value"))
    g.ex((pressed, "True"), "Net.CallServerRpc", Name="Horn")
    horn = g.n("Event.Rpc", Name="Horn")
    counted = g.setv((horn, "Then"), "Horns", data=g.num("Add", (g.get("Horns"), "Value"), 1))
    honk = g.ex(counted, "Net.CallMulticastRpc", Name="Honk")
    g.l(*g.text(["horn from client ", (horn, "Sender", "number")]), honk, "Text")
    g.lane("honk")
    heard = g.n("Event.Rpc", Name="Honk")
    stored = g.setv((heard, "Then"), "LastHorn", data=(heard, "Text"))
    g.print_(stored[0], g.text(["BLUEPRINT_NET honk: ", (heard, "Text")]))
    return g


def scene():
    s = k.SceneBuilder("Arena", 9300000000000000)
    s.settings = k.settings_2d(bloom=(0.9, 0.5, 0.6), vignette=0.25)
    s.entity("Camera", pos=(0, 0, 10), Camera=k.camera_ortho(16.0, clear=(0.03, 0.04, 0.08, 1)))
    s.entity("Floor", pos=(0, 0, -1), scale=(ARENA[0] * 2 + 0.6, ARENA[1] * 2 + 0.6, 1), SpriteRenderer=k.sprite(col=(0.09, 0.11, 0.18, 1), order=-10))
    for i, (x, y, sx, sy) in enumerate(((0, ARENA[1] + 0.45, ARENA[0] * 2 + 1.5, 0.3), (0, -ARENA[1] - 0.45, ARENA[0] * 2 + 1.5, 0.3),
                                        (ARENA[0] + 0.45, 0, 0.3, ARENA[1] * 2 + 1.5), (-ARENA[0] - 0.45, 0, 0.3, ARENA[1] * 2 + 1.5))):
        s.entity(f"Wall {i + 1}", pos=(x, y, -0.5), scale=(sx, sy, 1), SpriteRenderer=k.sprite(col=(0.3, 0.55, 0.95, 1), order=-5))
    # The C++ director (a scene object: both sides have it): coins on the server, the scoreboard everywhere.
    s.entity("Arena", NetworkIdentity={"AlwaysRelevant": True, "RelevanceRadius": 0.0},
             NativeScript=k.script("ArenaDirector", Coins=6, Scoreboard=k.ref(s.base + 9), Status=k.ref(s.base + 10)))
    # The graph: spawns, the joins counter, the horn.
    s.entity("Rules", NetworkIdentity={"AlwaysRelevant": True, "RelevanceRadius": 0.0},
             NativeScript={"Class": "Zamin.VisualGraph", "Fields": {"Graph": {"Type": "String", "Value": "Scripts/ArenaRules.zgraph"}}})
    s.entity("Scoreboard", pos=(-ARENA[0], ARENA[1] + 0.95, 0), scale=(0.5, 0.5, 1), Text=k.world_text("", (0.95, 0.95, 1, 1), "Left"))
    s.entity("Status", pos=(ARENA[0], ARENA[1] + 0.95, 0), scale=(0.4, 0.4, 1), Text=k.world_text("", (0.6, 0.8, 1, 1), "Right"))
    s.entity("Hint", pos=(0, -ARENA[1] - 1.0, 0), scale=(0.36, 0.36, 1),
             Text=k.world_text("WASD / stick: move   Space: collect a coin   H: horn", (0.55, 0.6, 0.75, 1)))
    return s


def icon():
    img = k.image(64, 64)
    x, y = k.coords(64, 64)
    k.fill(img, k.sd_circle(x, y, 26), (0.3, 0.55, 0.95, 1.0))
    k.fill(img, k.sd_circle(x, y, 12), (1.0, 0.82, 0.2, 1.0))
    k.write_png(os.path.join(ASSETS, "Textures/GameIcon.png"), img)
    k.write_zmeta(ASSETS, "Textures/GameIcon.png", "Texture", {}, NAME)


def main():
    k.ensure_dir(ASSETS)
    icon()
    prefabs()
    rules_graph().save(ASSETS, "Scripts/ArenaRules.zgraph", NAME)
    uuid = scene().save(ASSETS, "Scenes/Arena.zscene", NAME)
    actions = [
        ("Collect", ["Key:Space", "Key:E", "Gamepad:A"]),
        ("Horn", ["Key:H", "Gamepad:Y"]),
    ]
    axes = [
        k.axis("MoveX", ["Key:A", "Key:Left", "Gamepad:DPadLeft"], ["Key:D", "Key:Right", "Gamepad:DPadRight"], "LeftX"),
        k.axis("MoveY", ["Key:S", "Key:Down", "Gamepad:DPadDown"], ["Key:W", "Key:Up", "Gamepad:DPadUp"], "LeftY", invert=True),
    ]
    vectors = [{"Name": "Move", "X": "MoveX", "Y": "MoveY", "Mode": "Clamp", "Deadzone": 0.0}]
    k.write_project(PROJECT, NAME, "Net Arena", "Scenes/Arena.zscene", uuid, "1.0.0", [("Scenes/Arena.zscene", uuid)], actions, axes, vectors,
                    description="Networking: a 2D arena for 2+ players - predicted movement, a replicated score, server RPCs and "
                                "multicast effects from C++ and a graph; ZaminPlayer --server / --connect", localization=())
    k.write_module_cmake(PROJECT, NAME, "Zamin sample game: Net Arena (networking) - the C++ script module (libNetArena.so)")
    print("NetArena: done")


if __name__ == "__main__":
    main()
