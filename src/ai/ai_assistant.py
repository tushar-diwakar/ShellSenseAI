import re
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

Convert the user's request into exactly ONE Linux command.

LANGUAGE SUPPORT:
- Understand English, Hindi and Hinglish.
- Understand informal and grammatically incorrect language.
- Understand the actual intent.

RULES:
1. Return exactly ONE Linux command.
2. Do not explain the command.
3. Do not use Markdown or code blocks.
4. Never return multiple commands.
5. Never use command chaining, pipes, semicolons or redirection.
6. Never use sudo.
7. Never generate destructive commands such as rm, rmdir,
   mkfs, dd, chmod, chown, shutdown or reboot.
8. Never generate commands that kill processes.
9. Never modify important system files.
10. If the request is unclear or unsafe, return exactly UNSAFE.
11. Do not invent Linux command options.
12. Only generate commands for clear Linux or system operations.
13. For normal conversation, return exactly NO_COMMAND.
14. Creating a folder means mkdir, never cd.
15. Creating a file means touch.
16. If a filename or folder name is missing, return UNSAFE.
17. Never guess a missing filename.

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
Output: find src -type f -name '*.py'

User: Kernel version batao
Output: uname -r

User: Mujhe ek folder banana hai test123
Output: mkdir test123

User: Create a folder named projects
Output: mkdir projects

User: Create a file named test.txt
Output: touch test.txt

User: Make a file
Output: UNSAFE

User: Delete all files
Output: UNSAFE

User: Thank you
Output: NO_COMMAND

User: hello
Output: NO_COMMAND

User: okay
Output: NO_COMMAND

Return only the command.
"""

    try:
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

        if command not in ("UNSAFE", "NO_COMMAND") and (
            "\n" in command
            or ";" in command
            or "&&" in command
            or "||" in command
            or "|" in command
            or ">" in command
            or "<" in command
        ):
            command = "UNSAFE"

        print(command)

    except Exception:
        print("UNSAFE")


def route_request(user_input):
    text = user_input.lower().strip()

    # Clearly educational or conversational questions
    chat_patterns = [
        r"^(what is|what are|who is|who are)\b",
        r"^(why|how|when|where|which)\b",
        r"^(explain|define|describe|tell me about)\b",
        r"^(kya hai|kaun hai|kyun|kyu|kaise|kab|samjhao)\b",
        r"^(linux kya|operating system kya|iska matlab)\b",
        r"^(hello|hi|hey|thanks|thank you|okay|ok|done)\b"
    ]

    for pattern in chat_patterns:
        if re.search(pattern, text):
            print("CHAT")
            return

    # File creation without a filename
    if re.search(
        r"\b(create|make|banao|banana|banani)\b",
        text
    ) and re.search(
        r"\b(file|folder|directory)\b",
        text
    ):
        has_name = re.search(
            r"\b(named|called|name|naam|named as)\b",
            text
        )

        if not has_name:
            print("CLARIFY: What should the file or folder be named?")
            return

    system_prompt = """
You are the intent router for ShellSense AI.

Classify the user's request into exactly one format:

CHAT
COMMAND
CLARIFY: question
UNSAFE

CHAT:
Meaningful questions, explanations, greetings and conversation.

COMMAND:
Only clear requests to perform Linux operations.

CLARIFY:
File or folder creation requests without a name.

UNSAFE:
Destructive or dangerous operations.

UNKNOWN:
Random text, meaningless input, gibberish or requests whose intent
cannot reasonably be understood.



Understand English, Hindi and Hinglish.
Do not confuse questions with commands.
Examples:
User: what is Linux
Output: CHAT

User: show hidden files
Output: COMMAND

User: hfg
Output: UNKNOWN

User: asdfgh
Output: UNKNOWN

User: make a file
Output: CLARIFY: What should the file be named?
Return only one classification.
"""

    try:
        result = get_ai_response(
            user_input,
            system_prompt,
            max_tokens=60
        ).strip()

        result = result.splitlines()[0].strip()

        if result.upper() == "CHAT":
            print("CHAT")

        elif result.upper() == "COMMAND":
            generate_command(user_input)

        elif result.upper().startswith("CLARIFY:"):
            print(result)

        elif result.upper() == "UNSAFE":
            print("UNSAFE")

        elif result.upper() == "UNKNOWN":
            print("UNKNOWN")

        else:
            print("UNKNOWN")

    except Exception:
        print("UNKNOWN")

def main():
    if len(sys.argv) < 3:
        print(
            "Usage: ai_assistant.py "
            "[auto|ask|explain|suggest|diagnose|command] input"
        )
        return

    mode = sys.argv[1].lower()
    user_input = " ".join(sys.argv[2:]).strip()

    if mode == "auto":
        route_request(user_input)
        return

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
