"""Relay messages to DeepSeek's chat API (OpenAI-compatible), keeping a
conversation on disk so a session can be continued turn by turn.

    python -I tools/deepseek/relay.py --session audit "message text"
    python -I tools/deepseek/relay.py --session audit --file path/to/brief.md
    python -I tools/deepseek/relay.py --session audit --system path/to/system.md "message"
    python -I tools/deepseek/relay.py --session audit --show      # print the transcript

Key: DEEPSEEK_API_KEY, or the first line of %LOCALAPPDATA%\\deepseek\\api_key.txt.
Sessions live in build/deepseek-sessions/<name>.json (build/ is git-ignored).
No SDK: urllib only.  ASCII-safe output on Windows consoles.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request

API_URL = "https://api.deepseek.com/chat/completions"
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SESSION_DIR = os.path.join(ROOT, "build", "deepseek-sessions")


def load_key():
    key = os.environ.get("DEEPSEEK_API_KEY", "").strip()
    if key:
        return key
    path = os.path.join(os.environ.get("LOCALAPPDATA", ""), "deepseek", "api_key.txt")
    try:
        with open(path, "r", encoding="utf-8-sig") as fh:
            key = fh.readline().strip()
    except OSError:
        key = ""
    if not key:
        sys.exit("relay: no key. Set DEEPSEEK_API_KEY or write it to " + path)
    return key


def session_path(name):
    os.makedirs(SESSION_DIR, exist_ok=True)
    return os.path.join(SESSION_DIR, name + ".json")


def load_session(name):
    try:
        with open(session_path(name), "r", encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return {"messages": [], "usage": []}


def save_session(name, data):
    tmp = session_path(name) + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(data, fh, indent=1, ensure_ascii=True)
    os.replace(tmp, session_path(name))


def call(key, model, messages, thinking, max_tokens, timeout):
    body = {"model": model, "messages": messages, "max_tokens": max_tokens, "stream": False}
    # deepseek-flash thinks by default and the hidden reasoning counts against
    # max_tokens (a 4000-token budget came back empty, finish=length), so thinking
    # is off unless --thinking is passed.
    body["thinking"] = {"type": "enabled" if thinking else "disabled"}
    req = urllib.request.Request(
        API_URL,
        data=json.dumps(body).encode("utf-8"),
        headers={"Content-Type": "application/json", "Authorization": "Bearer " + key},
        method="POST",
    )
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except urllib.error.HTTPError as err:
            detail = err.read().decode("utf-8", "replace")
            if err.code in (429, 500, 502, 503) and attempt < 2:
                time.sleep(5 * (attempt + 1))
                continue
            sys.exit("relay: HTTP %d: %s" % (err.code, detail[:2000]))
        except (urllib.error.URLError, TimeoutError) as err:
            if attempt < 2:
                time.sleep(5 * (attempt + 1))
                continue
            sys.exit("relay: network error: %s" % err)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("text", nargs="?", help="message text")
    ap.add_argument("--session", default="default")
    ap.add_argument("--file", help="send this file's contents as the message (text, if given, is prepended)")
    ap.add_argument("--system", help="path of a system prompt; applied when the session is new")
    ap.add_argument("--model", default="deepseek-flash")
    ap.add_argument("--thinking", action="store_true", help="enable DeepSeek thinking mode")
    ap.add_argument("--max-tokens", type=int, default=8192)
    ap.add_argument("--timeout", type=int, default=600)
    ap.add_argument("--show", action="store_true", help="print the session transcript and exit")
    ap.add_argument("--reset", action="store_true", help="start the session afresh")
    ap.add_argument("--out", help="also write the reply to this file")
    args = ap.parse_args()

    data = {"messages": [], "usage": []} if args.reset else load_session(args.session)

    if args.show:
        for m in data["messages"]:
            print("=== %s\n%s\n" % (m["role"], m["content"]))
        return

    content = args.text or ""
    if args.file:
        with open(args.file, "r", encoding="utf-8") as fh:
            content = (content + "\n\n" if content else "") + fh.read()
    if not content.strip():
        sys.exit("relay: nothing to send (give text or --file)")

    if args.system and not data["messages"]:
        with open(args.system, "r", encoding="utf-8") as fh:
            data["messages"].append({"role": "system", "content": fh.read()})

    data["messages"].append({"role": "user", "content": content})
    reply = call(load_key(), args.model, data["messages"], args.thinking, args.max_tokens, args.timeout)
    choice = reply["choices"][0]["message"]
    text = choice.get("content") or ""
    data["messages"].append({"role": "assistant", "content": text})
    data["usage"].append({"model": reply.get("model"), "usage": reply.get("usage"),
                          "finish": reply["choices"][0].get("finish_reason"), "at": time.strftime("%Y-%m-%d %H:%M:%S")})
    save_session(args.session, data)

    out = text.encode("ascii", "replace").decode("ascii")
    print(out)
    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)
    u = reply.get("usage") or {}
    print("\n[relay] model=%s prompt=%s completion=%s finish=%s" % (
        reply.get("model"), u.get("prompt_tokens"), u.get("completion_tokens"),
        reply["choices"][0].get("finish_reason")), file=sys.stderr)


if __name__ == "__main__":
    main()
