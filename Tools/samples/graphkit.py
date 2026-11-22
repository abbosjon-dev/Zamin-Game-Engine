"""Deterministic Visual Script (.zgraph) builder for the compiler-free vertical slices.

Nodes are placed on named lanes (one row band per lane, left to right in creation order) so a generated graph reads
well in the editor. Helpers cover the patterns every slice graph repeats: exec chains, branches, And/Or trees,
comparisons, text joins with NumberToText, configuration steps whose Success values feed one failure print, typed
variables. `budget()` asserts the per-graph node/link limits of the slice design (docs/VerticalSlices.md).
"""
import json
import os

import samplekit as k

MAX_NODES = 2500
MAX_LINKS = 5000
TYPES = ("Bool", "Number", "String", "Vec2", "Vec3", "Vec4", "Entity", "ArrayBool", "ArrayNumber", "ArrayString", "ArrayVec3", "ArrayEntity")


class Graph:
    def __init__(self, name):
        self.name = name
        self.data = {"Format": "Zamin.VisualGraph", "Version": 1, "Nodes": [], "Links": [], "Variables": {}}
        self.lanes = {}
        self.lane_name = "main"
        self.lane("main")

    # ---- layout
    def lane(self, name):
        """Following nodes go on lane `name` (created below the previous lanes)."""
        if name not in self.lanes:
            self.lanes[name] = [len(self.lanes), 0]
        self.lane_name = name
        return self

    def _place(self):
        row, column = self.lanes[self.lane_name]
        self.lanes[self.lane_name][1] += 1
        return (column % 24) * 270, row * 900 + (column // 24) * 180

    # ---- nodes and links
    def n(self, kind, **inputs):
        node_id = len(self.data["Nodes"]) + 1
        x, y = self._place()
        self.data["Nodes"].append({"ID": node_id, "Kind": kind, "X": x, "Y": y, "Inputs": inputs})
        return node_id

    def l(self, a, out, b, pin):
        self.data["Links"].append({"From": a, "Output": out, "To": b, "Input": pin})

    def chain(self, *nodes):
        for a, b in zip(nodes, nodes[1:]):
            self.l(a, "Then", b, "In")
        return nodes[-1]

    def after(self, source, kind, out="Then", **inputs):
        """A new exec node run after `source` (its `out` exec pin)."""
        node = self.n(kind, **inputs)
        self.l(source, out, node, "In")
        return node

    def wire(self, node, **pins):
        """Data links into `node`: pin=(source, output)."""
        for pin, (source, output) in pins.items():
            self.l(source, output, node, pin)
        return node

    # ---- variables
    def var(self, name, kind, default):
        assert kind in TYPES, kind
        self.data["Variables"][name] = {"Type": kind, "Default": default}
        return name

    def get(self, name):
        kind = self.data["Variables"][name]["Type"]
        return self.n("Get." + kind, Name=name)

    def set(self, source, name, value=None, out="Then", src=None):
        """Set variable `name` after exec `source`: a literal `value` or the data pin `src` = (node, output)."""
        kind = self.data["Variables"][name]["Type"]
        node = self.n("Set." + kind, Name=name, **({} if value is None else {"Value": value}))
        self.l(source, out, node, "In")
        if src:
            self.l(src[0], src[1], node, "Value")
        return node

    # ---- logic
    def cmp(self, kind, a, b):
        """Equal/Greater/Less of data pin `a` = (node, output) against a literal or a data pin `b`."""
        node = self.n(kind, **({} if isinstance(b, tuple) else {"B": b}))
        self.l(a[0], a[1], node, "A")
        if isinstance(b, tuple):
            self.l(b[0], b[1], node, "B")
        return node

    def text_is(self, a, literal):
        node = self.n("TextEqual", B=literal)
        self.l(a[0], a[1], node, "A")
        return node

    def not_(self, a):
        node = self.n("Not")
        self.l(a[0], a[1], node, "A")
        return node

    def all_of(self, terms, kind="And"):
        """An And (or Or) tree of Bool data pins (node, output); returns (node, 'Value')."""
        terms = list(terms)
        assert terms
        if len(terms) == 1:
            return terms[0]
        current = terms[0]
        for term in terms[1:]:
            node = self.n(kind)
            self.l(current[0], current[1], node, "A")
            self.l(term[0], term[1], node, "B")
            current = (node, "Value")
        return current

    def any_of(self, terms):
        return self.all_of(terms, "Or")

    def branch(self, source, condition, out="Then"):
        """A Branch after exec `source` on Bool pin `condition`; returns the branch node (True/False outputs)."""
        node = self.n("Branch")
        self.l(source, out, node, "In")
        self.l(condition[0], condition[1], node, "Condition")
        return node

    def when(self, event, condition, **event_inputs):
        """Event `event` (e.g. 'Event.Update') gated by `condition`: returns the Branch (continue from 'True')."""
        start = self.n(event, **event_inputs)
        return self.branch(start, condition)

    def num(self, kind, a, b=None):
        """Math node `kind` (Add, Subtract, Multiply, Min, Max...) over data pins/literals; returns (node, 'Value')."""
        inputs = {}
        if not isinstance(a, tuple):
            inputs["A"] = a
        if b is not None and not isinstance(b, tuple):
            inputs["B"] = b
        node = self.n(kind, **inputs)
        if isinstance(a, tuple):
            self.l(a[0], a[1], node, "A")
        if isinstance(b, tuple):
            self.l(b[0], b[1], node, "B")
        return (node, "Value")

    def text(self, parts):
        """Concatenation of literals and data pins (Number pins go through NumberToText); returns (node, 'Value')."""
        pieces = []
        for part in parts:
            if isinstance(part, tuple):
                source, output, *kind = part
                if kind and kind[0] in ("number", "time"):
                    t = self.n("FormatTime") if kind[0] == "time" else self.n("FormatNumber", Decimals=kind[1] if len(kind) > 1 else 0)
                    self.l(source, output, t, "Seconds" if kind[0] == "time" else "Number")
                    pieces.append((t, "Value"))
                else:
                    pieces.append((source, output))
            else:
                pieces.append(str(part))
        current = None
        for piece in pieces:
            if current is None:
                if isinstance(piece, tuple):
                    node = self.n("Concat", A="")
                    self.l(piece[0], piece[1], node, "B")
                else:
                    node = self.n("Concat", A=piece, B="")
                current = (node, "Value")
                continue
            node = self.n("Concat", **({"B": piece} if not isinstance(piece, tuple) else {}))
            self.l(current[0], current[1], node, "A")
            if isinstance(piece, tuple):
                self.l(piece[0], piece[1], node, "B")
            current = (node, "Value")
        return current

    def loc(self, key, fallback):
        """The project's language string `key` (Localization.Text: the current language's, else the default one's),
        `fallback` when no language has it; a data pin `key` = (node, output) looks the key up at runtime (an
        interaction prompt key). Returns (node, 'Value')."""
        if isinstance(key, tuple):
            node = self.n("Localization.Text", Fallback=fallback if isinstance(fallback, str) else "")
            self.l(key[0], key[1], node, "Key")
            if isinstance(fallback, tuple):
                self.l(fallback[0], fallback[1], node, "Fallback")
            return (node, "Value")
        return (self.n("Localization.Text", Key=key, Fallback=fallback), "Value")

    def print_(self, source, text, out="Then"):
        node = self.n("Print", **({"Text": text} if isinstance(text, str) else {}))
        self.l(source, out, node, "In")
        if isinstance(text, tuple):
            self.l(text[0], text[1], node, "Text")
        return node

    def steps(self, start, ops, failure):
        """Configuration: exec chain of (kind, inputs) after `start`; every Success is And-ed and a false result prints
        `failure`. Returns the last exec node (after the check)."""
        last, out = (start, "Then") if not isinstance(start, tuple) else start
        success = []
        for op in ops:
            kind, inputs = op[0], op[1]
            node = self.n(kind, **inputs)
            self.l(last, out, node, "In")
            out = "Then"
            for pin, (source, output) in (op[2] if len(op) > 2 else {}).items():
                self.l(source, output, node, pin)
            success.append((node, "Success"))
            last = node
        check = self.branch(last, self.all_of(success), out=out)
        bad = self.n("Print", Text=failure)
        self.l(check, "False", bad, "In")
        tail = self.n("Sequence")
        self.l(check, "True", tail, "In")
        self.l(bad, "Then", tail, "In")
        return tail, "First"

    # ---- exec handles: (node, output) pairs. One exec output feeds exactly one node (the VM follows one link).
    def ex(self, src, kind, **inputs):
        """An exec node after handle `src`; returns the node id (continue with (node, 'Then'))."""
        node = self.n(kind, **inputs)
        if src is not None:
            self.l(src[0], src[1], node, "In")
        return node

    def setv(self, src, name, value=None, data=None):
        """Set variable `name` after handle `src` to a literal or data pin; returns (node, 'Then')."""
        kind = self.data["Variables"][name]["Type"]
        node = self.ex(src, "Set." + kind, Name=name, **({} if value is None else {"Value": value}))
        if data:
            self.l(data[0], data[1], node, "Value")
        return (node, "Then")

    def br(self, src, condition):
        """Branch after handle `src`; returns the node ((node, 'True') / (node, 'False'))."""
        node = self.ex(src, "Branch")
        self.l(condition[0], condition[1], node, "Condition")
        return node

    def join(self, *handles):
        """A Sequence every given handle flows into; returns its (node, 'First') handle (fan-in is allowed)."""
        node = self.n("Sequence")
        for h in handles:
            if h is not None:
                self.l(h[0], h[1], node, "In")
        return (node, "First")

    def fork(self, src):
        """A Sequence after `src`: three independent continuations (First, Second, Third)."""
        node = self.ex(src, "Sequence")
        return (node, "First"), (node, "Second"), (node, "Third")

    # ---- output
    def budget(self):
        nodes, links = len(self.data["Nodes"]), len(self.data["Links"])
        assert nodes <= MAX_NODES, f"{self.name}: {nodes} nodes > {MAX_NODES}"
        assert links <= MAX_LINKS, f"{self.name}: {links} links > {MAX_LINKS}"
        return nodes, links

    def save(self, assets_dir, rel_path, project):
        self.budget()
        path = os.path.join(assets_dir, rel_path)
        k.write_text(path, json.dumps(self.data, separators=(",", ":"), ensure_ascii=False) + "\n")
        return k.write_zmeta(assets_dir, rel_path, "VisualGraph", {}, project)
