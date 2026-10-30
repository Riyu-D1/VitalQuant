"""Minimal KiCad S-expression parser -> nested python lists."""
import re, sys

def tokenize(text):
    # strip comments? kicad files have no comments. tokens: ( ) "..." atoms
    tokens = re.findall(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()"]+', text)
    return tokens

def parse(text):
    toks = tokenize(text)
    pos = [0]
    def _parse():
        out = []
        while pos[0] < len(toks):
            t = toks[pos[0]]
            pos[0] += 1
            if t == '(':
                out.append(_parse())
            elif t == ')':
                return out
            else:
                if t.startswith('"'):
                    out.append(t[1:-1].replace('\\"','"'))
                else:
                    out.append(t)
        return out
    # top-level may be a single list
    return _parse()

def head(node):
    return node[0] if isinstance(node, list) and node else None

def find_all(tree, name):
    """yield all sublists whose head == name"""
    if isinstance(tree, list):
        if tree and tree[0] == name:
            yield tree
        for n in tree:
            if isinstance(n, list):
                yield from find_all(n, name)

def child(node, name):
    for n in node:
        if isinstance(n, list) and n and n[0] == name:
            return n
    return None

def children(node, name):
    return [n for n in node if isinstance(n, list) and n and n[0] == name]

def num(x, default=None):
    try:
        return float(x)
    except (TypeError, ValueError):
        return default
