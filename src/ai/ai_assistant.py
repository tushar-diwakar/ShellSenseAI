
import json
import sys
import urllib.request
import urllib.error

OLLAMA_URL = "http://localhost:11434/api/chat"
MODEL = "qwen2.5:0.5b"


def get_ai_response(prompt, system_prompt, max_tokens=350):
    payload = {
        "model": MODEL,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": prompt}
        ],
        "stream": False,
        "options": {
            "temperature": 0.1,
            "num_predict": max_tokens
        }
    }

    data = json.dumps(payload).encode("utf-8")

    request = urllib.request.Request(
        OLLAMA_URL,
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST"
    )

    with urllib.request.urlopen(request, timeout=120) as response:
        result = json.loads(response.read().decode("utf-8"))

    return result.get("message", {}).get("content", "").strip()


def ask_ai(prompt):
    system_prompt = (
        "You are ShellSense AI, a helpful Linux assistant. "
        "Give accurate, simple answers. "
        "Never invent Linux commands or options. "
        "Explain risks and admit uncertainty."
    )

    try:
        answer = get_ai_response(prompt, system_prompt)
        print("\n========== ShellSense AI ==========")
        print(answer)
        print("===================================\n")

    except Exception as error:
        print(f"AI error: {error}")



def generate_command(task):
    system_prompt = """
You are ShellSense AI, an expert Linux command-line assistant.

Your job is to understand the user's intent and convert it into
one correct Linux command.

LANGUAGE SUPPORT:
- Understand English.
- Understand Hindi.
- Understand Hinglish.
- Understand informal and grammatically incorrect language.
- Understand different ways of expressing the same request.

RULES:
1. Return exactly ONE Linux command.
2. Do not explain the command.
3. Do not use Markdown or code blocks.
4. Never return multiple commands.
5. Never use command chaining, pipes, semicolons or redirection.
6. Never use sudo.
7. Never generate destructive commands such as rm, rmdir,
   mkfs, dd, chmod, chown, shutdown or reboot.
8. Never generate commands that kill processes or modify
   important system files.
9. Prefer standard Linux utilities.
10. If the request is unclear or unsafe, return exactly UNSAFE.
11. Do not invent Linux command options.
12. Understand the actual intent instead of matching exact words.
13. If the user is greeting you, thanking you, saying done,
    okay, goodbye, or having normal conversation, return
    exactly NO_COMMAND.

14. Do not invent a command for conversational messages.

15. Only generate a command when the user clearly requests
    a Linux or system operation.

Examples:

User: Show hidden files
Output: ls -la

User: Mujhe hidden files dikhao
Output: ls -la

User: Abhi kis directory mein hu
Output: pwd

User: Disk space kitni bachi hai
Output: df -h

User: Show running processes
Output: ps aux

User: Find all Python files
Output: find . -type f -name '*.py'

User: Kernel version batao
Output: uname -r

User: Delete all files
Output: UNSAFE
User: Thank you
Output: NO_COMMAND

User: done
Output: NO_COMMAND

User: hello
Output: NO_COMMAND

User: okay
Output: NO_COMMAND
IMPORTANT COMMAND RULES:

- Creating a folder means use mkdir, never cd.
- Creating a file means use touch.
- Finding Python files means use:
  find src -type f -name '*.py'

Examples:

User: Mujhe ek folder banana hai test123
Output: mkdir test123

User: Create a folder named projects
Output: mkdir projects

User: Make a new directory called demo
Output: mkdir demo

User: Find all python files
Output: find src -type f -name '*.py'

User: Thank you
Output: NO_COMMAND

Return only the command.
"""

    command = get_ai_response(
        task,
        system_prompt,
        max_tokens=100
    ).strip()

    command = command.splitlines()[0].strip()
    if command.lower().startswith("output:"):
      command = command[7:].strip()

    if command.startswith("```"):
        command = "UNSAFE"

    print(command)


def main():
    if len(sys.argv) < 3:
        print("Usage: ai_assistant.py [ask|explain|suggest|diagnose|command] input")
        return

    mode = sys.argv[1].lower()
    user_input = " ".join(sys.argv[2:])

    if mode == "command":
        generate_command(user_input)
        return

    if mode == "ask":
        prompt = user_input

    elif mode == "explain":
        prompt = (
            f"Explain this Linux command: {user_input}. "
            "Describe its purpose and valid options with an example."
        )

    elif mode == "suggest":
        prompt = (
            f"Suggest a Linux command for: {user_input}. "
            "Explain the command and mention risks. Do not execute it."
        )

    elif mode == "diagnose":
        prompt = (
            f"Diagnose this Linux error: {user_input}. "
            "Explain the cause, verification steps and safe solution. "
            "Never suggest unsafe permission changes."
        )

    else:
        print("Invalid mode.")
        return

    ask_ai(prompt)


if __name__ == "__main__":
    main()
