# ShellSense AI
### Linux Shell & System Intelligence

ShellSense AI is a custom Linux shell developed using C++17 and Python. It combines traditional shell functionality with a locally running AI assistant to understand natural-language instructions and generate Linux commands.

## Features

### 1. Custom Linux Shell
- Interactive command-line interface
- Built-in commands: `help`, `pwd`, `cd`, `history`, `exit`
- Execute external Linux commands using process creation and execution

### 2. Process Management
- Process listing and monitoring
- Background job execution
- Job management
- Process priority management using `renice`
- Signal handling and child-process management

### 3. Shell Operations
- Command pipelines
- Input and output redirection
- Background execution using `&`

### 4. System Monitoring
- CPU and memory information
- System resource monitoring using Python and psutil
- Real-time process monitoring

### 5. Inter-Process Communication
- Named pipes (FIFO)
- FIFO message sending and receiving

### 6. AI Integration
- Local AI integration using Ollama
- Natural-language command generation
- English and Hinglish instruction support
- AI-powered command explanations
- Command suggestions and error diagnosis
- Command validation before execution
- User confirmation for supported filesystem modifications

## Technologies Used

| Technology | Purpose |
|---|---|
| C++17 | Core shell implementation |
| Python | AI integration and system monitoring |
| Ollama | Local language model execution |
| Qwen 2.5 (0.5B) | Natural-language command generation |
| Linux / WSL Ubuntu | Development and execution |
| psutil | System monitoring |

## Project Structure

```text
ShellSenseAI/
├── src/
│   ├── shell/
│   │   └── main.cpp
│   ├── ai/
│   │   └── ai_assistant.py
│   └── monitor/
│       └── sysinfo.py
├── config/
├── docs/
├── logs/
├── tests/
└── README.md
```

## Installation and Setup

### Prerequisites
- Ubuntu Linux or WSL Ubuntu
- GCC / G++ with C++17 support
- Python 3
- Ollama

### 1. Clone the repository

```bash
git clone YOUR_REPOSITORY_URL
cd ShellSenseAI
```

### 2. Install dependencies

```bash
sudo apt update
sudo apt install g++ python3 python3-venv python3-pip
```

### 3. Create and activate virtual environment

```bash
python3 -m venv .venv
source .venv/bin/activate
```

### 4. Install Python dependency

```bash
pip install psutil
```

### 5. Set up Ollama

Install Ollama, then download the model:

```bash
ollama pull qwen2.5:0.5b
```

Ensure the Ollama service is running.

### 6. Compile the project

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic src/shell/main.cpp -o shellsense
```

### 7. Run ShellSense AI

```bash
./shellsense
```

## Example Usage

### Natural-language commands

```text
ShellSense:/home/user$ Mujhe hidden files dikhao
AI generated: ls -la
```

```text
ShellSense:/home/user$ Abhi mai kis folder me hu
AI generated: pwd
```

```text
ShellSense:/home/user$ Mujhe ek folder banana hai test123
AI generated: mkdir test123
```

Filesystem modifications require user confirmation.

### AI Assistant

```text
ai ask What is a Linux process?
ai explain fork()
ai suggest
ai diagnose
```

### Process Management

```text
sleep 100 &
jobs
ps
top
```

### Pipeline and Redirection

```text
ls | sort
ls -l > output.txt
cat output.txt
```

### System Monitoring

```text
sysinfo
```

## Operating System Concepts Implemented

- Process creation using `fork()`
- Program execution using `execvp()`
- Process synchronization using `waitpid()`
- Signal handling
- Background process management
- Pipes and I/O redirection
- Inter-process communication using FIFO
- Process priority management
- System monitoring

## Safety Considerations

AI-generated commands are treated as untrusted input. ShellSense AI applies command validation and restricts the commands accepted by its natural-language execution feature.

Some filesystem modifications require explicit user confirmation. This is a basic safety mechanism, not a complete security sandbox. Users should review commands before approving them.

## Future Improvements

- Improved natural-language understanding
- More advanced command validation
- Better error handling
- Enhanced system monitoring dashboard
- Expanded test coverage

## Author

Developed as an Operating Systems project to explore Linux shell internals, process management, inter-process communication, system monitoring, and local AI integration.
