@echo off
rem Launch the Claude Code CLI as the wave builder on Claude Sonnet 5.5, through the
rem operator's own Claude login (the CLI's stored credentials; no API key).  The
rem DeepSeek twin is deepseek-claude.cmd; run-wave.ps1 picks one with -Builder.
rem Per-process only: nothing here touches the user's global environment or the
rem Claude desktop app.  This file never reads %LOCALAPPDATA%\deepseek.
rem Usage: tools\deepseek\sonnet-claude.cmd [claude args]
setlocal
rem An inherited endpoint or token must not route the builder anywhere else.
set "ANTHROPIC_BASE_URL="
set "ANTHROPIC_AUTH_TOKEN="
set "ANTHROPIC_API_KEY="
set "DEEPSEEK_API_KEY="
rem A builder launched from a desktop-app session inherits that session's host
rem wiring; cleared so the headless builder is its own session and refreshes its
rem own login instead of waiting on a host that is not there.
set "CLAUDE_CODE_CHILD_SESSION="
set "CLAUDE_CODE_SESSION_ID="
set "CLAUDE_CODE_HOST_SESSION_ID="
set "CLAUDE_CODE_MESSAGING_SOCKET="
set "CLAUDE_CODE_MESSAGING_TOKEN="
set "CLAUDE_CODE_SDK_HAS_HOST_AUTH_REFRESH="
set "CLAUDE_CODE_ENTRYPOINT="
set "ANTHROPIC_MODEL=claude-sonnet-5-5"
set "ANTHROPIC_DEFAULT_OPUS_MODEL="
set "ANTHROPIC_DEFAULT_SONNET_MODEL="
set "ANTHROPIC_DEFAULT_HAIKU_MODEL="
set "CLAUDE_CODE_SUBAGENT_MODEL=claude-sonnet-5-5"
set "CLAUDE_CODE_EFFORT_LEVEL=high"
rem Compact at ~200k tokens: a small working context per turn (standing rule 2).
set "CLAUDE_CODE_AUTO_COMPACT_WINDOW=200000"
cd /d "%~dp0..\.."
claude --model claude-sonnet-5-5 %*
